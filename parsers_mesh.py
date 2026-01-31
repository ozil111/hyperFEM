from __future__ import annotations

import re
from typing import Dict, List, Tuple

try:
    from .fem_data_model import FEMDataModel, Node, Element
except ImportError:
    from fem_data_model import FEMDataModel, Node, Element


def load_mesh_into_model(mesh_file_path: str, model: FEMDataModel) -> None:
    """
    解析 mesh.dat 的关键部分，将 element_sets、parts 范围（parts_ranges）和 surfaces 填充到 model。
    采用流式/扫描方式，避免整体加载到内存。
    """
    # Call the new full mesh loading function
    load_full_mesh_into_model(mesh_file_path, model)
    return  # Skip the old parsing logic
    
    model.mesh_file_path = mesh_file_path

    element_sets: Dict[str, List[Tuple[int, int, int]]] = {}
    parts_ranges: Dict[str, List[Tuple[int, int, int]]] = {}
    surfaces: List[str] = []

    try:
        with open(mesh_file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"Error: The file '{mesh_file_path}' was not found.")
        return

    # Phase 1: Parse Set section (Element sets, Parts, Surfaces)
    current_block = None
    current_set_name = None
    current_set_ids = ""
    in_set_section = False
    brace_level = 0

    def parse_id_ranges(id_string: str) -> List[Tuple[int, int]]:
        try:
            from .fem_data_model import FEMDataModel as _M
        except ImportError:
            from fem_data_model import FEMDataModel as _M
        return _M.parse_id_ranges(id_string)

    for i, raw in enumerate(lines):
        line = raw.strip()
        if not line:
            continue

        if line == "Set {":
            in_set_section = True
            brace_level = 1
            continue
        elif in_set_section:
            if line.endswith('{'):
                brace_level += 1
            elif line == '}':
                brace_level -= 1
                if brace_level == 0:
                    in_set_section = False
                    break

        if not in_set_section:
            continue

        if line.endswith('{'):
            current_block = line.split('{')[0].strip().lower()
            continue

        if '}' in line:
            if current_block in ['element', 'part'] and current_set_name:
                id_ranges = parse_id_ranges(current_set_ids)
                if current_block == 'element':
                    element_sets[current_set_name] = id_ranges
                elif current_block == 'part':
                    parts_ranges[current_set_name] = id_ranges
            if line.strip() == '}' and current_block:
                current_block = None
                current_set_name = None
                current_set_ids = ""
                continue

        if current_block in ['element', 'part', 'surface']:
            match = re.match(r'(\S+)\s*\[(.*?)\]', line)
            if match:
                if current_block in ['element', 'part'] and current_set_name:
                    id_ranges = parse_id_ranges(current_set_ids)
                    if current_block == 'element':
                        element_sets[current_set_name] = id_ranges
                    else:
                        parts_ranges[current_set_name] = id_ranges

                current_set_name = match.group(1).strip()
                current_set_ids = match.group(2).strip()
                if current_block == 'surface':
                    surfaces.append(current_set_name)
            elif current_set_name:
                current_set_ids += " " + line

    # Parse standalone Part section (always check this)
    for i, raw in enumerate(lines):
        line = raw.strip()
        if line == "Part {":
            for j in range(i + 1, len(lines)):
                part_line = lines[j].strip()
                if part_line == "}":
                    break
                match = re.match(r'(\S+)\s*\[(.*?)\]', part_line)
                if match:
                    part_name = match.group(1)
                    part_ids = match.group(2)
                    parts_ranges[part_name] = parse_id_ranges(part_ids)
                    print(f"Debug: 解析Part {part_name} = {parts_ranges[part_name]}")
            break

    model.element_sets = element_sets
    model.surfaces = surfaces
    # 将 parts_ranges 存入 model 以供 element_set -> part 映射构建
    setattr(model, 'parts_ranges', parts_ranges)


def load_full_mesh_into_model(mesh_file_path: str, model: FEMDataModel) -> None:
    """
    Parses the entire mesh.dat file and builds a complete, in-memory
    representation of the mesh for high-performance lookups.
    """
    if model._is_mesh_loaded:
        print("Mesh already loaded into memory.")
        return

    print("Building full in-memory mesh model from scratch...")
    model.mesh_file_path = mesh_file_path

    # First, ensure we have the basic data structures loaded
    if not model.element_sets:
        _load_basic_structures(mesh_file_path, model)

    # Use temporary dicts for parsing relationships
    temp_element_id_to_part_name: Dict[int, str] = {}
    parts_ranges: Dict[str, List[Tuple[int, int, int]]] = getattr(model, 'parts_ranges', {})

    # Build element ID to part name mapping from parts_ranges
    # First, create a direct mapping from element ID to part name
    for part_name, part_ranges in parts_ranges.items():
        for start, end, step in part_ranges:
            for eid in range(start, end + 1, step):
                temp_element_id_to_part_name[eid] = part_name
    

    try:
        with open(mesh_file_path, 'r', encoding='utf-8') as f:
            _parse_nodes_section(f, model)
            _parse_elements_section(f, model, temp_element_id_to_part_name)
            _parse_node_sets_section(f, model)

        model._is_mesh_loaded = True
        print(f"In-memory mesh model built successfully. Loaded {len(model.nodes)} nodes and {len(model.elements)} elements.")
        print(f"Node-to-elements reverse lookup map contains {len(model.node_to_elements_map)} entries.")
        
    except Exception as exc:
        print(f"Failed to build in-memory mesh model: {exc}")
        model._is_mesh_loaded = False


def _load_basic_structures(mesh_file_path: str, model: FEMDataModel) -> None:
    """Load basic structures if not already loaded."""
    element_sets: Dict[str, List[Tuple[int, int, int]]] = {}
    parts_ranges: Dict[str, List[Tuple[int, int, int]]] = {}
    surfaces: List[str] = []

    def parse_id_ranges(id_string: str) -> List[Tuple[int, int, int]]:
        try:
            from .fem_data_model import FEMDataModel as _M
        except ImportError:
            from fem_data_model import FEMDataModel as _M
        return _M.parse_id_ranges(id_string)

    try:
        with open(mesh_file_path, 'r', encoding='utf-8') as f:
            lines = f.readlines()
    except FileNotFoundError:
        print(f"Error: The file '{mesh_file_path}' was not found.")
        return

    # Parse Set section (Element sets, Parts, Surfaces)
    current_block = None
    current_set_name = None
    current_set_ids = ""
    in_set_section = False
    brace_level = 0

    for i, raw in enumerate(lines):
        line = raw.strip()
        if not line:
            continue

        if line == "Set {":
            in_set_section = True
            brace_level = 1
            continue
        elif in_set_section:
            if line.endswith('{'):
                brace_level += 1
            elif line == '}':
                brace_level -= 1
                if brace_level == 0:
                    in_set_section = False
                    break

        if not in_set_section:
            continue

        if line.endswith('{'):
            current_block = line.split('{')[0].strip().lower()
            continue

        if '}' in line:
            if current_block in ['element', 'part'] and current_set_name:
                id_ranges = parse_id_ranges(current_set_ids)
                if current_block == 'element':
                    element_sets[current_set_name] = id_ranges
                elif current_block == 'part':
                    parts_ranges[current_set_name] = id_ranges
            if line.strip() == '}' and current_block:
                current_block = None
                current_set_name = None
                current_set_ids = ""
                continue

        if current_block in ['element', 'part', 'surface']:
            match = re.match(r'(\S+)\s*\[(.*?)\]', line)
            if match:
                if current_block in ['element', 'part'] and current_set_name:
                    id_ranges = parse_id_ranges(current_set_ids)
                    if current_block == 'element':
                        element_sets[current_set_name] = id_ranges
                    else:
                        parts_ranges[current_set_name] = id_ranges

                current_set_name = match.group(1).strip()
                current_set_ids = match.group(2).strip()
                if current_block == 'surface':
                    surfaces.append(current_set_name)
            elif current_set_name:
                current_set_ids += " " + line

    # Parse standalone Part section (always check this)
    for i, raw in enumerate(lines):
        line = raw.strip()
        if line == "Part {":
            for j in range(i + 1, len(lines)):
                part_line = lines[j].strip()
                if part_line == "}":
                    break
                match = re.match(r'(\S+)\s*\[(.*?)\]', part_line)
                if match:
                    part_name = match.group(1)
                    part_ids = match.group(2)
                    parts_ranges[part_name] = parse_id_ranges(part_ids)
            break

    model.element_sets = element_sets
    model.surfaces = surfaces
    setattr(model, 'parts_ranges', parts_ranges)


def _parse_nodes_section(f, model: FEMDataModel) -> None:
    """Parse the Node section and populate the nodes dictionary."""
    f.seek(0)  # Reset file pointer
    in_node_section = False
    brace_level = 0
    
    for line_raw in f:
        line = line_raw.strip()
        
        if line == 'Node {':
            in_node_section = True
            brace_level = 1
            continue
        elif in_node_section:
            if line.endswith('{'):
                brace_level += 1
            elif line == '}':
                brace_level -= 1
                if brace_level == 0:
                    break
        
        if not in_node_section:
            continue
            
        # Skip node set definitions, we only want individual node coordinates
        # Node sets have names like "Set-2 [...]", individual nodes are "0 [...]" (no leading spaces)
        # Individual nodes start directly with a number
        if not re.match(r'^\s*\d+\s*\[', line):
            continue
            
        # Parse individual node lines like "0 [1,1,1]" or "    0 [1,1,1]"
        node_match = re.match(r'^\s*(\d+)\s*\[([^\]]+)\]', line)
        if node_match:
            node_id = int(node_match.group(1))
            coords_str = node_match.group(2)
            
            # Parse coordinates
            coords = []
            for coord_str in re.split(r'[,\s]+', coords_str.strip()):
                if coord_str:
                    try:
                        coords.append(float(coord_str))
                    except ValueError:
                        pass
            
            # Ensure we have 3 coordinates
            while len(coords) < 3:
                coords.append(0.0)
            
            model.nodes[node_id] = Node(id=node_id, coords=(coords[0], coords[1], coords[2]))


def _parse_elements_section(f, model: FEMDataModel, temp_element_id_to_part_name: Dict[int, str]) -> None:
    """Parse the Element section and populate elements and reverse lookup map."""
    f.seek(0)  # Reset file pointer
    in_element_section = False
    in_element_type_section = False
    current_element_type = ""
    brace_level = 0
    
    for line_raw in f:
        line = line_raw.strip()
        original_line = line_raw  # Keep original line with spaces for element parsing
        
        if line == 'Element {':
            in_element_section = True
            brace_level = 1
            continue
        elif in_element_section:
            if line.endswith('{'):
                brace_level += 1
                # Check if this is an element type section like "Hex8 {"
                if brace_level == 2 and line != 'Element {':
                    current_element_type = line.replace(' {', '').strip()
                    in_element_type_section = True
                    continue
            elif line == '}':
                brace_level -= 1
                if brace_level == 0:
                    in_element_section = False
                    break
                elif brace_level == 1:
                    in_element_type_section = False
                    current_element_type = ""
                    continue
        
        if not (in_element_section and in_element_type_section):
            continue
            
        # Parse element lines like "        0 [4,5,7,6,0,1,3,2]" using original line
        element_match = re.match(r'^\s+(\d+)\s*\[([^\]]+)\]', original_line)
        if element_match:
            element_id = int(element_match.group(1))
            connectivity_str = element_match.group(2)
            
            # Parse node connectivity
            node_ids = []
            for node_str in re.split(r'[,\s]+', connectivity_str.strip()):
                if node_str.isdigit():
                    node_ids.append(int(node_str))
            
            # Determine element type based on number of nodes
            element_type = _determine_element_type(len(node_ids))
            
            # Get part name
            part_name = temp_element_id_to_part_name.get(element_id, "Unknown")
            
            # Create and store the element object
            elem = Element(id=element_id, type=current_element_type, connectivity=node_ids, part_name=part_name)
            model.elements[element_id] = elem
            
            # CRITICAL: Build the reverse-lookup map
            for node_id in node_ids:
                model.node_to_elements_map[node_id].append(element_id)


def _parse_node_sets_section(f, model: FEMDataModel) -> None:
    """Parse node sets and expand ranges to actual node lists for fast lookup."""
    f.seek(0)  # Reset file pointer
    in_set_section = False
    in_node_subsection = False
    brace_level = 0
    current_node_set = None
    expanded_node_sets: Dict[str, List[int]] = {}
    
    for line_raw in f:
        line = line_raw.strip()
        
        # Look for Set { section first
        if line == 'Set {':
            in_set_section = True
            brace_level = 1
            continue
        elif in_set_section:
            if line.endswith('{'):
                brace_level += 1
                # Check if this is Node subsection
                if line == 'Node {':
                    in_node_subsection = True
                    continue
            elif line == '}':
                brace_level -= 1
                if brace_level == 0:
                    in_set_section = False
                    break
                elif in_node_subsection and brace_level == 1:
                    # End of Node subsection
                    in_node_subsection = False
                    continue
        
        # Only process lines inside Set { Node { ... } }
        if not (in_set_section and in_node_subsection):
            continue
            
        # Look for node set definitions like "Set-2 [0,1,4,5]"
        # Allow hyphens in node set names
        node_set_match = re.match(r'^([A-Za-z0-9_-]+)\s*\[([^\]]+)\]', line)
        if node_set_match:
            node_set_name = node_set_match.group(1)
            ranges_str = node_set_match.group(2)
            
            # Parse and expand ranges
            ranges = FEMDataModel.parse_id_ranges(ranges_str)
            node_list = []
            for start, end, step in ranges:
                node_list.extend(range(start, end + 1, step))
            
            expanded_node_sets[node_set_name] = node_list
            current_node_set = None
        # Handle multi-line node set definitions
        elif re.match(r'^[A-Za-z0-9_-]+\s*\[', line):  # Start of a node set
            parts = line.split('[', 1)
            if len(parts) == 2:
                current_node_set = parts[0].strip()
                ranges_str = parts[1]
                if ranges_str.endswith(']'):
                    ranges_str = ranges_str[:-1]
                    ranges = FEMDataModel.parse_id_ranges(ranges_str)
                    node_list = []
                    for start, end, step in ranges:
                        node_list.extend(range(start, end + 1, step))
                    expanded_node_sets[current_node_set] = node_list
                    current_node_set = None
                else:
                    # Multi-line, continue parsing
                    ranges = FEMDataModel.parse_id_ranges(ranges_str)
                    node_list = []
                    for start, end, step in ranges:
                        node_list.extend(range(start, end + 1, step))
                    expanded_node_sets[current_node_set] = node_list
        elif current_node_set and line:
            if line.endswith(']'):
                ranges_str = line[:-1]
                ranges = FEMDataModel.parse_id_ranges(ranges_str)
                for start, end, step in ranges:
                    expanded_node_sets[current_node_set].extend(range(start, end + 1, step))
                current_node_set = None
            else:
                ranges = FEMDataModel.parse_id_ranges(line)
                for start, end, step in ranges:
                    expanded_node_sets[current_node_set].extend(range(start, end + 1, step))
    
    # Store expanded node sets for fast lookup
    setattr(model, 'expanded_node_sets', expanded_node_sets)


def _determine_element_type(num_nodes: int) -> str:
    """Determine element type based on number of nodes."""
    type_map = {
        2: 'Line2',
        3: 'Tri3', 
        4: 'Quad4',
        6: 'Tri6',
        8: 'Hex8',
        9: 'Quad9',
        10: 'Tet10',
        20: 'Hex20'
    }
    return type_map.get(num_nodes, f'Unknown_{num_nodes}_node')


