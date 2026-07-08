// ============================================================
// SimdroidParserMesh.cpp
//
// Mesh DAT file parsing: Node/Element/Surface extraction,
// Set/Part pre-scan, and post-parsing Set member population.
// ============================================================

#include "SimdroidParser.h"
#include "SimdroidParserDetail.h"

using namespace SimdroidParserDetail;

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

// Implementation of collect_set_definitions_from_file (defined in header but needs .cpp body)
namespace SimdroidParserDetail {

void collect_set_definitions_from_file(const std::string& path, MeshSetDefs& defs) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string current_block; 
    std::string current_name;
    std::string current_ids;

    auto flush_current = [&](const std::string& block_type) {
        if (current_name.empty()) return;
        const auto ranges = parse_id_ranges(current_ids);
        const auto refs   = extract_set_refs(current_ids);
        if (ranges.empty() && refs.empty()) return;

        if (block_type == "element") {
            if (!ranges.empty()) { auto& v = defs.element_sets[current_name];     v.insert(v.end(), ranges.begin(), ranges.end()); }
            if (!refs.empty())   { auto& v = defs.element_set_refs[current_name]; v.insert(v.end(), refs.begin(),   refs.end());   }
        } else if (block_type == "part") {
            if (!ranges.empty()) { auto& v = defs.parts_ranges[current_name];     v.insert(v.end(), ranges.begin(), ranges.end()); }
            if (!refs.empty())   { auto& v = defs.part_set_refs[current_name];    v.insert(v.end(), refs.begin(),   refs.end());   }
        } else if (block_type == "node") {
            if (!ranges.empty()) { auto& v = defs.node_sets[current_name];        v.insert(v.end(), ranges.begin(), ranges.end()); }
            if (!refs.empty())   { auto& v = defs.node_set_refs[current_name];     v.insert(v.end(), refs.begin(),   refs.end());   }
        } else if (block_type == "surface") {
            if (!ranges.empty()) { auto& v = defs.surface_sets[current_name];     v.insert(v.end(), ranges.begin(), ranges.end()); }
            if (!refs.empty())   { auto& v = defs.surface_set_refs[current_name]; v.insert(v.end(), refs.begin(),   refs.end());   }
        }
        current_ids.clear();
    };

    std::string line;
    int brace_level = 0;
    enum State { IDLE, IN_SET_BLOCK, IN_PART_BLOCK };
    State state = IDLE;

    while (std::getline(file, line)) {
        preprocess_line(line);
        if (line.empty()) continue;

        if (state == IDLE) {
            if (line == "Set {") {
                state = IN_SET_BLOCK;
                brace_level = 1;
                current_block.clear();
                current_name.clear();
                current_ids.clear();
            } else if (line == "Part {") {
                state = IN_PART_BLOCK;
                brace_level = 1;
                current_block = "part"; 
                current_name.clear();
                current_ids.clear();
            }
            continue;
        }

        if (ends_with(line, "{")) {
            ++brace_level;
            if (state == IN_SET_BLOCK) {
                auto b = line.substr(0, line.size() - 1);
                trim(b);
                current_block = to_lower_copy(b);
            }
            continue;
        }

        if (line == "}") {
            flush_current(current_block);
            current_name.clear();
            --brace_level;
            if (brace_level <= 0) {
                state = IDLE;
                current_block.clear();
            } else if (state == IN_SET_BLOCK && brace_level == 1) {
                current_block.clear();
            }
            continue;
        }

        if (state == IN_SET_BLOCK) {
            bool is_supported = (current_block == "element" || current_block == "part" || current_block == "node" || current_block == "surface");
            if (!is_supported) continue;
        }

        bool has_open_bracket = line.find('[') != std::string::npos;
        if (has_open_bracket) {
            flush_current(current_block);
            current_name.clear(); 
            auto name_opt = extract_prefix_before_bracket(line);
            if (name_opt) current_name = *name_opt;
            
            size_t lb = line.find('[');
            std::string content = line.substr(lb + 1);
            size_t rb = content.rfind(']');
            if (rb != std::string::npos) {
                current_ids = content.substr(0, rb);
                flush_current(current_block); 
                current_name.clear();
            } else {
                current_ids = content; 
            }
        } else if (!current_name.empty()) {
            std::string segment = line;
            size_t rb = segment.rfind(']');
            if (rb != std::string::npos) {
                current_ids += " " + segment.substr(0, rb);
                flush_current(current_block);
                current_name.clear();
            } else {
                current_ids += " " + segment;
            }
        }
    }
}

} // namespace SimdroidParserDetail

// ---------------------------------------------------------------
// Helper: resolve set-to-set references (handles chained refs)
// ---------------------------------------------------------------
template <typename MemberComp>
static void resolve_set_references(entt::registry& registry,
    const std::unordered_map<std::string, std::vector<std::string>>& refs_map)
{
    using namespace SimdroidParserDetail;
    if (refs_map.empty()) return;
    std::unordered_set<std::string> resolved;
    bool changed = true;
    int max_iter = 100; // guard against circular references
    while (changed && max_iter-- > 0) {
        changed = false;
        for (const auto& [name, ref_names] : refs_map) {
            if (resolved.count(name)) continue;
            // Wait until all referenced sets that also have refs are resolved
            bool all_ready = true;
            for (const auto& ref : ref_names) {
                if (refs_map.count(ref) && !resolved.count(ref)) {
                    all_ready = false;
                    break;
                }
            }
            if (!all_ready) continue;

            entt::entity target = get_or_create_set_entity(registry, name);
            auto& target_members = registry.get_or_emplace<MemberComp>(target);
            for (const auto& ref : ref_names) {
                entt::entity ref_entity = get_or_create_set_entity(registry, ref);
                if (registry.all_of<MemberComp>(ref_entity)) {
                    auto& ref_members = registry.get<MemberComp>(ref_entity);
                    target_members.members.insert(target_members.members.end(),
                        ref_members.members.begin(), ref_members.members.end());
                }
            }
            resolved.insert(name);
            changed = true;
        }
    }
}

// ---------------------------------------------------------------
// Main mesh parser: parse_mesh_dat()
// ---------------------------------------------------------------

void SimdroidParser::parse_mesh_dat(const std::string& path, DataContext& ctx) {
    MeshSetDefs defs;
    collect_set_definitions_from_file(path, defs);

    auto& registry = ctx.registry;
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("cannot open mesh file");

    node_lookup.clear();
    element_lookup.clear();
    surface_lookup.clear();

    std::string line;
    std::string pending_element_line;
    std::string pending_surface_line;
    bool in_node_section = false;
    bool in_element_section = false;
    bool in_element_type_section = false;
    bool in_surface_section = false;
    bool in_surface_type_section = false;
    bool in_skip_block = false;     // Skip "Set { ... }" and "Part { ... }" definitions during geometry parsing
    int  skip_brace_level = 0;
    int current_element_type_id = 0;
    std::string current_element_block_name;
    std::string current_surface_block_name;
    
    // Safety reserve
    node_lookup.resize(10000, entt::null);
    element_lookup.resize(10000, entt::null);
    surface_lookup.resize(10000, entt::null);

    while (std::getline(file, line)) {
        preprocess_line(line);
        if (line.empty()) continue;

        // Skip definition blocks (Set/Part) — members are already collected by collect_set_definitions_from_file().
        if (in_skip_block) {
            if (ends_with(line, "{")) ++skip_brace_level;
            if (line == "}") {
                --skip_brace_level;
                if (skip_brace_level <= 0) {
                    in_skip_block = false;
                    skip_brace_level = 0;
                }
            }
            continue;
        } else {
            // Only start skipping when we're not inside core geometry blocks
            if (!in_node_section && !in_element_section && !in_surface_section) {
                if (line == "Set {") {
                    in_skip_block = true;
                    skip_brace_level = 1;
                    continue;
                }
                if (line == "Part {") {
                    in_skip_block = true;
                    skip_brace_level = 1;
                    continue;
                }
            }
        }

        if (line == "Node {") {
            in_node_section = true;
            continue;
        }
        if (line == "Element {") {
            in_element_section = true;
            current_element_block_name.clear();
            continue;
        }
        if (line == "Surface {") {
            in_surface_section = true;
            current_surface_block_name.clear();
            continue;
        }
        if (ends_with(line, "{")) {
            if (in_element_section) {
                in_element_type_section = true;
                // Extract element block name (e.g., "Hex8" from "Hex8 {")
                const size_t end_pos = line.find('{');
                std::string raw_name = (end_pos == std::string::npos) ? line : line.substr(0, end_pos);
                trim(raw_name);
                current_element_block_name = to_lower_copy(raw_name);
            } else if (in_surface_section) {
                in_surface_type_section = true;
                const size_t end_pos = line.find('{');
                std::string raw_name = (end_pos == std::string::npos) ? line : line.substr(0, end_pos);
                trim(raw_name);
                current_surface_block_name = to_lower_copy(raw_name);
            }
            continue;
        }
        if (line == "}") {
            if (in_element_type_section) {
                in_element_type_section = false;
                current_element_block_name.clear();
            } else if (in_surface_type_section) {
                in_surface_type_section = false;
                current_surface_block_name.clear();
            } else {
                in_node_section = false;
                in_element_section = false;
                in_surface_section = false;
            }
            continue;
        }

        // --- Node Parsing ---
        if (in_node_section) {
            std::string clean_line = line;
            std::replace(clean_line.begin(), clean_line.end(), '[', ' ');
            std::replace(clean_line.begin(), clean_line.end(), ']', ' ');
            std::replace(clean_line.begin(), clean_line.end(), ',', ' ');

            std::stringstream ss(clean_line);
            int nid;
            double x, y, z;
            
            if (!(ss >> nid >> x >> y >> z)) {
                continue;
            }
            
            if (nid < 0) continue;

            // Expand lookup
            if (static_cast<size_t>(nid) >= node_lookup.size()) {
                size_t new_size = std::max(static_cast<size_t>(nid) * 2, node_lookup.size() + 10000); 
                node_lookup.resize(new_size, entt::null);
            }

            auto e = registry.create();
            registry.emplace<Component::Position>(e, x, y, z);
            registry.emplace<Component::NodeID>(e, nid);
            registry.emplace<Component::OriginalID>(e, nid); 
            
            node_lookup[nid] = e;
        }
        
        // --- Element Parsing ---
        if (in_element_section) {
            // Support wrapped lists: element connectivity may span multiple lines inside '[ ... ]'
            std::string elem_line = line;
            if (!pending_element_line.empty()) {
                pending_element_line += " " + elem_line;
                if (pending_element_line.find(']') == std::string::npos) continue;
                elem_line = pending_element_line;
                pending_element_line.clear();
            } else {
                const size_t lb0 = elem_line.find('[');
                if (lb0 != std::string::npos && elem_line.find(']') == std::string::npos) {
                    pending_element_line = elem_line;
                    continue;
                }
            }

            size_t lb = elem_line.find('[');
            size_t rb = elem_line.rfind(']');
            if (lb == std::string::npos || rb == std::string::npos) continue;

            // 1. Parse Element ID
            int eid = 0;
            try {
                std::string id_str = elem_line.substr(0, lb);
                std::replace(id_str.begin(), id_str.end(), ',', ' '); 
                eid = std::stoi(id_str);
            } catch (...) { continue; }

            if (eid < 0) continue;

            // 2. Parse Node IDs
            std::string content = elem_line.substr(lb + 1, rb - lb - 1);
            
            std::replace(content.begin(), content.end(), ',', ' ');
            
            std::vector<int> node_ids;
            std::stringstream ss(content);
            int nid;
            while (ss >> nid) {
                node_ids.push_back(nid);
            }

            // 3. Validate node IDs
            std::vector<entt::entity> valid_node_entities;
            valid_node_entities.reserve(node_ids.size());
            bool is_element_broken = false;

            for (int id : node_ids) {
                if (id >= 0 && static_cast<size_t>(id) < node_lookup.size() && node_lookup[id] != entt::null) {
                    valid_node_entities.push_back(node_lookup[id]);
                } else {
                    spdlog::warn("Element {} refers to undefined Node ID: {}", eid, id);
                    is_element_broken = true;
                    break;
                }
            }

            if (is_element_broken) continue;

            // 4. Type inference
            int type_id = 0;
            size_t count = node_ids.size();
            
            if (count == 8) type_id = 308;      // Hex8
            else if (count == 4) type_id = 304; // Tet4 / Quad4
            else if (count == 10) type_id = 310;// Tet10
            else if (count == 20) type_id = 320;// Hex20
            else if (count == 3) type_id = 203; // Tri3
            else if (count == 2) type_id = 102; // Line2
            
            // Special handling: 4-node Quad4 vs Tet4
            if (count == 4) {
                if (current_element_block_name.find("quad") != std::string::npos) type_id = 204; // Quad4
                else type_id = 304; // Default to Tet4
            }

            // Expand lookup
            if (static_cast<size_t>(eid) >= element_lookup.size()) {
                element_lookup.resize(static_cast<size_t>(eid) * 2, entt::null);
            }

            auto e = registry.create();
            registry.emplace<Component::ElementID>(e, eid);
            registry.emplace<Component::OriginalID>(e, eid);
            registry.emplace<Component::ElementType>(e, type_id);
            
            auto& conn = registry.emplace<Component::Connectivity>(e);
            conn.nodes = std::move(valid_node_entities);

            element_lookup[eid] = e;
        }

        // --- Surface Parsing ---
        if (in_surface_section && in_surface_type_section) {
            // Support wrapped lists
            std::string surf_line = line;
            if (!pending_surface_line.empty()) {
                pending_surface_line += " " + surf_line;
                if (pending_surface_line.find(']') == std::string::npos) continue;
                surf_line = pending_surface_line;
                pending_surface_line.clear();
            } else {
                const size_t lb0 = surf_line.find('[');
                if (lb0 != std::string::npos && surf_line.find(']') == std::string::npos) {
                    pending_surface_line = surf_line;
                    continue;
                }
            }

            const size_t lb = surf_line.find('[');
            const size_t rb = surf_line.rfind(']');
            if (lb == std::string::npos || rb == std::string::npos) continue;

            int sid = 0;
            try {
                std::string id_str = surf_line.substr(0, lb);
                std::replace(id_str.begin(), id_str.end(), ',', ' ');
                sid = std::stoi(id_str);
            } catch (...) { continue; }

            if (sid < 0) continue;

            std::string content = surf_line.substr(lb + 1, rb - lb - 1);
            std::replace(content.begin(), content.end(), ',', ' ');

            std::vector<int> ids;
            ids.reserve(8);
            {
                std::stringstream ss(content);
                int v = 0;
                while (ss >> v) ids.push_back(v);
            }
            if (ids.size() < 2) continue; // must have at least 1 node + parent_eid

            const int parent_eid = ids.back();
            ids.pop_back();

            // Validate parent element
            entt::entity parent_elem_entity = entt::null;
            if (parent_eid >= 0 && static_cast<size_t>(parent_eid) < element_lookup.size()) {
                parent_elem_entity = element_lookup[parent_eid];
            }
            if (parent_elem_entity == entt::null) {
                spdlog::warn("Surface {} refers to undefined parent Element ID: {}", sid, parent_eid);
                continue;
            }

            // Validate nodes
            std::vector<entt::entity> valid_node_entities;
            valid_node_entities.reserve(ids.size());
            bool is_broken = false;
            for (int nid : ids) {
                if (nid >= 0 && static_cast<size_t>(nid) < node_lookup.size() && node_lookup[nid] != entt::null) {
                    valid_node_entities.push_back(node_lookup[nid]);
                } else {
                    spdlog::warn("Surface {} refers to undefined Node ID: {}", sid, nid);
                    is_broken = true;
                    break;
                }
            }
            if (is_broken) continue;

            // Expand lookup
            if (static_cast<size_t>(sid) >= surface_lookup.size()) {
                size_t new_size = std::max(static_cast<size_t>(sid) * 2, surface_lookup.size() + 10000);
                surface_lookup.resize(new_size, entt::null);
            }

            auto se = registry.create();
            registry.emplace<Component::SurfaceID>(se, sid);
            registry.emplace<Component::OriginalID>(se, sid);
            auto& sc = registry.emplace<Component::SurfaceConnectivity>(se);
            sc.nodes = std::move(valid_node_entities);
            registry.emplace<Component::SurfaceParentElement>(se, parent_elem_entity);

            surface_lookup[sid] = se;
        }
    }

    // -----------------------------------------------------------------
    // Apply Sets Definition (Post-Parsing)
    // -----------------------------------------------------------------
    
    // Helper to add entities to sets
    auto add_to_set = [&](auto& member_list, const std::vector<IdRange>& ranges, const std::vector<entt::entity>& lookup) {
        for (const auto& r : ranges) {
            if (r.step <= 0) continue;
            for (int id = r.start; id <= r.end; id += r.step) {
                if (id >= 0 && static_cast<size_t>(id) < lookup.size()) {
                    entt::entity e = lookup[id];
                    if (e != entt::null) {
                        member_list.push_back(e);
                    }
                }
            }
        }
    };

    // 1. Node Sets
    for (const auto& [name, ranges] : defs.node_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::NodeSetMembers>(e);
        add_to_set(members.members, ranges, node_lookup);
    }

    // 2. Element Sets
    for (const auto& [name, ranges] : defs.element_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::ElementSetMembers>(e);
        add_to_set(members.members, ranges, element_lookup);
    }

    // 3. Part Sets (treat as element sets)
    for (const auto& [name, ranges] : defs.parts_ranges) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::ElementSetMembers>(e);
        add_to_set(members.members, ranges, element_lookup);
    }

    // 4. Surface Sets
    for (const auto& [name, ranges] : defs.surface_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::SurfaceSetMembers>(e);
        add_to_set(members.members, ranges, surface_lookup);
    }

    // 5. Resolve set-to-set references (handles chained & circular refs)
    resolve_set_references<Component::ElementSetMembers>(registry, defs.element_set_refs);
    resolve_set_references<Component::ElementSetMembers>(registry, defs.part_set_refs);
    resolve_set_references<Component::NodeSetMembers>(registry, defs.node_set_refs);
    resolve_set_references<Component::SurfaceSetMembers>(registry, defs.surface_set_refs);
}
