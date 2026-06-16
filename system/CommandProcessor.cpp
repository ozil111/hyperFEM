/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandProcessor.h"
#include <spdlog/spdlog.h>
#include "parser_base/parserBase.h"
#include "parser_json/JsonParser.h"
#include "exporter_base/exporterBase.h"
#include "DataContext.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "TopologyData.h"
#include "mesh/TopologySystems.h"
#include "parser_simdroid/SimdroidParser.h"
#include "exporter_simdroid/SimdroidExporter.h"
#include "analysis/GraphBuilder.h"
#include "analysis/MermaidReporter.h"
#include "remesh/ConnectionPreservingRemesher.h"
#include "tui/ComponentTUI.h"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>
#include <cstdlib>
#include <algorithm>
#include <unordered_map>

namespace {

// Basic lookup helpers: nodes / elements / sets
entt::entity find_node_by_id(entt::registry& registry, int nid) {
    auto view = registry.view<const Component::NodeID>();
    for (auto e : view) {
        if (view.get<const Component::NodeID>(e).value == nid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_element_by_id(entt::registry& registry, int eid) {
    auto view = registry.view<const Component::ElementID>();
    for (auto e : view) {
        if (view.get<const Component::ElementID>(e).value == eid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_set_by_name(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto e : view) {
        if (view.get<const Component::SetName>(e).value == name) {
            return e;
        }
    }
    return entt::null;
}

int allocate_next_node_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::NodeID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::NodeID>(e).value);
    }
    return max_id + 1;
}

int allocate_next_element_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::ElementID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::ElementID>(e).value);
    }
    return max_id + 1;
}

entt::entity find_material_by_id(entt::registry& registry, int mid) {
    auto view = registry.view<const Component::MaterialID>();
    for (auto e : view) {
        if (view.get<const Component::MaterialID>(e).value == mid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_property_by_id(entt::registry& registry, int pid) {
    auto view = registry.view<const Component::PropertyID>();
    for (auto e : view) {
        if (view.get<const Component::PropertyID>(e).value == pid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity get_or_create_set_entity(entt::registry& registry, const std::string& name) {
    entt::entity e = find_set_by_name(registry, name);
    if (e != entt::null) return e;
    e = registry.create();
    registry.emplace<Component::SetName>(e, name);
    return e;
}

[[maybe_unused]] int infer_element_type_from_node_count(std::size_t count) {
    if (count == 2) return 102;   // Line2
    if (count == 3) return 203;   // Tri3
    if (count == 4) return 304;   // Tet4 / Quad4, default to volume element
    if (count == 8) return 308;   // Hex8
    if (count == 10) return 310;  // Tet10
    if (count == 20) return 320;  // Hex20
    return 0;
}

void invalidate_topology_if_needed(AppSession& session) {
    auto& registry = session.data.registry;
    if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        registry.ctx().erase<std::unique_ptr<TopologyData>>();
    }
    session.topology_built = false;
}

void rebuild_inspector_if_mesh_loaded(AppSession& session) {
    if (!session.mesh_loaded) return;
    session.inspector.build(session.data.registry);
}

/**
 * Parse the next argument from the stringstream.
 * Supports quoted strings: "path with spaces" or 'path with spaces'.
 * Unquoted tokens are read until whitespace.
 */
std::string parse_next_arg(std::stringstream& ss) {
    ss >> std::ws;           // skip leading whitespace
    if (ss.eof()) return {};

    int ch = ss.peek();
    if (ch == '"' || ch == '\'') {
        // Quoted string — read until matching closing quote
        char quote = static_cast<char>(ch);
        ss.get();            // consume opening quote
        std::string result;
        char c;
        while (ss.get(c)) {
            if (c == quote) break;
            result.push_back(c);
        }
        return result;
    } else {
        // Unquoted — read until whitespace
        std::string result;
        ss >> result;
        return result;
    }
}

/**
 * Read the remainder of the line, trim whitespace, and strip matching
 * outer quotes (single or double). Suitable for commands with a single
 * file-path argument (e.g. import, import_simdroid, save).
 */
std::string read_rest_of_line(std::stringstream& ss) {
    std::string rest;
    std::getline(ss, rest);

    // Trim leading/trailing whitespace
    const std::string whitespace = " \t\r\n";
    size_t start = rest.find_first_not_of(whitespace);
    if (start == std::string::npos) return {};
    size_t end = rest.find_last_not_of(whitespace);
    rest = rest.substr(start, end - start + 1);

    // Strip matching outer single or double quotes
    if (rest.size() >= 2) {
        char first = rest.front();
        char last  = rest.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            rest = rest.substr(1, rest.size() - 2);
        }
    }
    return rest;
}

} // anonymous namespace

void process_command(const std::string& command_line, AppSession& session) {
    std::stringstream ss(command_line);
    std::string command;
    ss >> command;

    if (command == "quit" || command == "exit") {
        session.is_running = false;
        spdlog::info("Exiting NovaFEA. Goodbye!");
    }
    else if (command == "help") {
        spdlog::info("Available commands: import, import_simdroid, export_simdroid, json_apply, "
                     "info, build_topology, list_bodies, show_body, "
                     "list_parts, delete_part, graph, remesh_plan, validate_constraints, list_constraint_warnings, "
                     "panel node <nid>, panel elem <eid>, panel part <name>, panel set <name>, "
                     "node, list_nodes, node_add, node_move, node_delete, "
                     "elem, elem_add, elem_delete, "
                     "list_elements, "
                     "list_sets, set_info, set_addnode, set_addelem, set_removenode, set_removeelem, "
                     "set_material <mid> <component> <param> <value>, "
                     "set_section <sid> <type> <param> <value>, "
                     "save, help, quit");
    }
    else if (command == "import") {
        std::string file_path = read_rest_of_line(ss);
        if (file_path.empty()) {
            spdlog::error("Usage: import <path_to_file>");
            return;
        }
        
        // Check if file exists
        if (!std::filesystem::exists(file_path)) {
            spdlog::error("File does not exist: {}", file_path);
            return;
        }
        
        session.clear_data();
        spdlog::info("Importing mesh from: {}", file_path);
        
        // Automatically select parser based on file extension
        std::filesystem::path path(file_path);
        std::string extension = path.extension().string();
        bool parse_success = false;
        
        if (extension == ".json" || extension == ".jsonc") {
            spdlog::info("Detected JSON format, using JsonParser...");
            parse_success = JsonParser::parse(file_path, session.data);
        } else if (extension == ".xfem") {
            spdlog::info("Detected XFEM format, using FemParser (legacy)...");
            parse_success = FemParser::parse(file_path, session.data);
        } else {
            spdlog::error("Unsupported file format: {}. Supported: .json, .jsonc, .xfem", extension);
            return;
        }
        
        if (parse_success) {
            session.mesh_loaded = true;
            rebuild_inspector_if_mesh_loaded(session);
            // Count entities using views
            auto node_count = session.data.registry.view<Component::Position>().size();
            auto element_count = session.data.registry.view<Component::Connectivity>().size();
            spdlog::info("Successfully imported mesh. {} nodes, {} elements.", node_count, element_count);
        } else {
            spdlog::error("Failed to import mesh from: {}", file_path);
        }
    }
    else if (command == "json_apply") {
        std::string file_path = read_rest_of_line(ss);
        if (file_path.empty()) {
            spdlog::error("Usage: json_apply <path_to_fragment.json|.jsonc>");
            spdlog::info("Merges a JSON fragment into the current model using the same keys/schema as import "
                         "(material, property, mesh, nodeset, eleset, curve, load, boundary, analysis, output). "
                         "Does not clear existing data; duplicate IDs in the fragment are skipped.");
            return;
        }
        if (!std::filesystem::exists(file_path)) {
            spdlog::error("File does not exist: {}", file_path);
            return;
        }
        const std::filesystem::path path(file_path);
        const std::string ext = path.extension().string();
        if (ext != ".json" && ext != ".jsonc") {
            spdlog::error("json_apply expects .json or .jsonc, got: {}", ext);
            return;
        }
        spdlog::info("Applying JSON fragment from: {}", file_path);
        if (!JsonParser::apply_fragment(file_path, session.data)) {
            spdlog::error("json_apply failed for: {}", file_path);
            return;
        }
        if (!session.mesh_loaded) {
            session.mesh_loaded = session.data.registry.view<Component::Position>().size() > 0;
        }
        std::ifstream fragment(file_path);
        std::string frag_text((std::istreambuf_iterator<char>(fragment)), std::istreambuf_iterator<char>());
        const bool mentions_mesh = frag_text.find("\"mesh\"") != std::string::npos;
        const bool mentions_property = frag_text.find("\"property\"") != std::string::npos;
        if (mentions_mesh || mentions_property) {
            invalidate_topology_if_needed(session);
            rebuild_inspector_if_mesh_loaded(session);
        }
        spdlog::info("json_apply completed.");
    }
    // =======================================================
    // New: Simdroid import command
    // =======================================================
    else if (command == "import_simdroid") {
        std::string control_path_str = read_rest_of_line(ss);
        if (control_path_str.empty()) {
            spdlog::error("Usage: import_simdroid <path_to_control.json>");
            return;
        }

        std::filesystem::path control_path(control_path_str);
        if (!std::filesystem::exists(control_path)) {
            spdlog::error("Control file not found: {}", control_path_str);
            return;
        }

        // Automatically infer mesh.dat path (assuming in the same directory)
        std::filesystem::path mesh_path = control_path.parent_path() / "mesh.dat";
        if (!std::filesystem::exists(mesh_path)) {
            spdlog::error("Mesh file not found at expected location: {}", mesh_path.string());
            spdlog::info("Tip: mesh.dat must be in the same directory as control.json");
            return;
        }

        spdlog::info("Importing Simdroid model...");
        spdlog::info("  Control: {}", control_path.string());
        spdlog::info("  Mesh:    {}", mesh_path.string());

        session.clear_data();

        // Call Parser
        try {
            if (SimdroidParser::parse(mesh_path.string(), control_path.string(), session.data)) {
                session.mesh_loaded = true;

                // Core step: After successful import, immediately build Inspector index
                session.inspector.build(session.data.registry);

                spdlog::info("Simdroid import successful. Entered Simdroid Interactive Mode.");
            } else {
                spdlog::error("Simdroid import failed.");
            }
        } catch (const std::exception& e) {
            spdlog::error("Exception during import: {}", e.what());
        }
    }
    // =======================================================
    // New: Simdroid export command (Blueprint Strategy)
    // =======================================================
    else if (command == "export_simdroid") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import' or 'import_simdroid' first.");
            return;
        }

        std::string arg1 = parse_next_arg(ss);
        std::string arg2 = parse_next_arg(ss);

        if (arg1.empty()) {
            spdlog::error("Usage: export_simdroid <output_dir | mesh.dat | control.json> [control.json]");
            return;
        }

        try {
            std::filesystem::path mesh_path;
            std::filesystem::path control_path;

            if (!arg2.empty()) {
                // Explicitly specify two paths: export_simdroid <mesh.dat> <control.json>
                mesh_path = std::filesystem::path(arg1);
                control_path = std::filesystem::path(arg2);
            } else {
                // When only one parameter is given, infer based on extension
                std::filesystem::path out(arg1);
                const std::string ext = out.extension().string();

                if (ext == ".json" || ext == ".jsonc") {
                    control_path = out;
                    mesh_path = out.parent_path() / "mesh.dat";
                } else if (ext == ".dat") {
                    mesh_path = out;
                    control_path = out.parent_path() / "control.json";
                } else {
                    // Treat as output directory
                    mesh_path = out / "mesh.dat";
                    control_path = out / "control.json";
                }
            }

            if (!mesh_path.parent_path().empty()) {
                std::filesystem::create_directories(mesh_path.parent_path());
            }
            if (!control_path.parent_path().empty()) {
                std::filesystem::create_directories(control_path.parent_path());
            }

            spdlog::info("Exporting Simdroid project...");
            spdlog::info("  Mesh:    {}", mesh_path.string());
            spdlog::info("  Control: {}", control_path.string());

            if (SimdroidExporter::save(mesh_path.string(), control_path.string(), session.data)) {
                spdlog::info("Simdroid export successful.");
            } else {
                spdlog::error("Simdroid export failed.");
            }

        } catch (const std::exception& e) {
            spdlog::error("Exception during export: {}", e.what());
        }
    }
    else if (command == "build_topology") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import' a mesh first.");
            return;
        }
        spdlog::info("Building topology data...");
        TopologySystems::extract_topology(session.data.registry);
        session.topology_built = true;
        
        // Get the topology from context to report statistics
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        spdlog::info("Topology built successfully. Found {} unique faces.", topology.faces.size());
    }
    else if (command == "list_bodies") {
        if (!session.topology_built) {
            spdlog::error("Topology not built. Please run 'build_topology' first.");
            return;
        }
        spdlog::info("Finding continuous bodies...");
        TopologySystems::find_continuous_bodies(session.data.registry);
        
        // Get the topology from context
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        spdlog::info("Found {} continuous body/bodies:", topology.body_to_elements.size());
        for (const auto& pair : topology.body_to_elements) {
            spdlog::info("  - Body {}: {} elements", pair.first, pair.second.size());
        }
    }
    else if (command == "show_body") {
        if (!session.topology_built) {
            spdlog::error("Topology not built. Please run 'build_topology' first.");
            return;
        }

        int body_id_to_show;
        if (!(ss >> body_id_to_show)) {
            spdlog::error("Usage: show_body <body_id>");
            return;
        }

        // Get the topology from context
        auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
        
        // Check if the requested BodyID exists
        auto it = topology.body_to_elements.find(body_id_to_show);
        if (it == topology.body_to_elements.end()) {
            spdlog::error("Body with ID {} not found. Use 'list_bodies' to see available bodies.", body_id_to_show);
            return;
        }

        const std::vector<entt::entity>& element_entities = it->second;
        
        // Build the output list
        std::stringstream element_list_ss;
        for (size_t i = 0; i < element_entities.size(); ++i) {
            entt::entity elem_entity = element_entities[i];
            // Get the OriginalID component to display external ID
            const auto& orig_id = session.data.registry.get<Component::OriginalID>(elem_entity);
            element_list_ss << orig_id.value << (i == element_entities.size() - 1 ? "" : ", ");
        }

        spdlog::info("Elements in Body {}:", body_id_to_show);
        spdlog::info("{}", element_list_ss.str());
    }
    else if (command == "save") {
        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded to save. Please 'import' a mesh first.");
            return;
        }
        std::string file_path = read_rest_of_line(ss);
        if (file_path.empty()) {
            spdlog::error("Usage: save <path_to_output_file.xfem>");
            return;
        }
        spdlog::info("Exporting mesh data to: {}", file_path);
        if (FemExporter::save(file_path, session.data)) {
            spdlog::info("Successfully exported mesh data.");
        } else {
            spdlog::error("Failed to export mesh data to: {}", file_path);
        }
    }
    else if (command == "info") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
        } else {
            // Count entities using views
            auto node_count = session.data.registry.view<Component::Position>().size();
            auto element_count = session.data.registry.view<Component::Connectivity>().size();
            auto set_count = session.data.registry.view<Component::SetName>().size();
            
            spdlog::info("Mesh loaded: {} nodes, {} elements, {} sets",
                         node_count, element_count, set_count);
            
            if (session.topology_built) {
                auto& topology = *session.data.registry.ctx().get<std::unique_ptr<TopologyData>>();
                spdlog::info("Topology built: {} unique faces, {} bodies",
                             topology.faces.size(),
                             topology.body_to_elements.size());
            } else {
                spdlog::info("Topology not built yet.");
            }
        }
    }
    // =======================================================
    // New: Simdroid interactive inspection commands
    // =======================================================
    else if (command == "list_parts") {
        if (!session.mesh_loaded) { spdlog::warn("No mesh loaded."); return; }
        tui::render_parts_list(session.data.registry);
    }
    else if (command == "delete_part") {
        std::vector<std::string> part_names;
        for (std::string name; ss >> name;) {
            part_names.push_back(name);
        }
        if (part_names.empty()) {
            spdlog::error("Usage: delete_part <part_name> [part_name2 ...]");
            return;
        }

        if (!session.mesh_loaded) {
            spdlog::error("No mesh loaded. Please 'import_simdroid' first.");
            return;
        }

        size_t deleted = 0;
        size_t failed = 0;
        for (const auto& part_name : part_names) {
            // delete_part() clears the inspector index; rebuild before each delete for stable multi-delete
            session.inspector.build(session.data.registry);

            if (session.inspector.delete_part(session.data.registry, part_name)) {
                spdlog::info("Part '{}' deleted successfully.", part_name);
                ++deleted;
            } else {
                spdlog::error("Failed to delete part '{}'. Part not found?", part_name);
                ++failed;
            }
        }

        if (deleted > 0) {
            // Rebuild index after deletes or eid_to_part and similar maps go stale and may crash
            session.inspector.build(session.data.registry);
            // Topology is invalid after entity removal; clear to avoid stale use
            if (session.data.registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
                session.data.registry.ctx().erase<std::unique_ptr<TopologyData>>();
            }
            session.topology_built = false;
        }
        spdlog::info("delete_part done. Deleted={}, Failed={}", deleted, failed);
    }
    else if (command == "graph") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
            return;
        }

        std::string output_filename = read_rest_of_line(ss);
        if (output_filename.empty()) output_filename = "connectivity.html";

        spdlog::info("Analyzing connectivity...");

        // 1. Build part connectivity graph
        PartGraph graph = GraphBuilder::build(session.data.registry, session.inspector);

        // 2. Optional: simple stats (e.g. isolated parts)
        int isolated_count = 0;
        for (const auto& [n, node] : graph.nodes) {
            if (node.edges.empty()) isolated_count++;
        }
        spdlog::info("Analysis complete. Parts: {}, Isolated: {}", graph.nodes.size(), isolated_count);

        // 3. Generate report
        MermaidReporter::generate_interactive_html(graph, output_filename);

        // 4. Optional: open default browser (Windows only here)
#ifdef _WIN32
        std::string cmd = "start " + output_filename;
        system(cmd.c_str());
#endif
    }
    else if (command == "remesh_plan") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
            return;
        }

        std::string output_filename = parse_next_arg(ss);
        if (output_filename.empty()) output_filename = "remesh_plan.json";

        double ratio = 100.0;
        if (!(ss >> ratio)) {
            ratio = 100.0;
        }

        RemeshOptions options;
        options.target_compression_ratio = ratio;

        spdlog::info("Building connection-preserving remesh plan...");
        RemeshPlan plan = ConnectionPreservingRemesher::build_plan(
            session.data.registry, session.inspector, options);

        if (!ConnectionPreservingRemesher::write_plan_json(plan, output_filename)) {
            spdlog::error("Failed to write remesh plan: {}", output_filename);
            return;
        }

        spdlog::info("Remesh plan written: {}", output_filename);
        spdlog::info("Elements: {} -> {} (estimated ratio {:.2f}x), Parts: {}, Interfaces: {}",
                     plan.original_element_count,
                     plan.target_element_count,
                     plan.target_element_count == 0
                         ? 0.0
                         : static_cast<double>(plan.original_element_count) /
                               static_cast<double>(plan.target_element_count),
                     plan.parts.size(),
                     plan.interfaces.size());
    }
    else if (command == "validate_constraints") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
            return;
        }

        spdlog::info("Re-running Simdroid constraint/contact validations...");
        auto& registry = session.data.registry;
        SimdroidParser::validate_constraints(registry);
    }
    else if (command == "list_constraint_warnings") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
            return;
        }
        auto& registry = session.data.registry;
        SimdroidParser::list_constraint_warnings(registry);
    }
    // =======================================================
    // Basic node / element operations
    // =======================================================
    else if (command == "node_add") {
        double x, y, z;
        if (!(ss >> x >> y >> z)) {
            spdlog::error("Usage: node_add <x> <y> <z>");
            return;
        }
        auto& registry = session.data.registry;
        int nid = allocate_next_node_id(registry);
        auto e = registry.create();
        registry.emplace<Component::Position>(e, x, y, z);
        registry.emplace<Component::NodeID>(e, nid);
        registry.emplace<Component::OriginalID>(e, nid);
        spdlog::info("Node {} created at ({}, {}, {}).", nid, x, y, z);
        session.mesh_loaded = true;
        invalidate_topology_if_needed(session);
        rebuild_inspector_if_mesh_loaded(session);
    }
    else if (command == "node_move") {
        int nid;
        double x, y, z;
        if (!(ss >> nid >> x >> y >> z)) {
            spdlog::error("Usage: node_move <nid> <x> <y> <z>");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity e = find_node_by_id(registry, nid);
        if (e == entt::null) {
            spdlog::error("Node {} not found.", nid);
            return;
        }
        auto& pos = registry.get<Component::Position>(e);
        pos.x = x;
        pos.y = y;
        pos.z = z;
        spdlog::info("Node {} moved to ({}, {}, {}).", nid, x, y, z);
        session.mesh_loaded = true;
        // Topology unchanged (coordinates only); no topology / inspector rebuild needed
    }
    else if (command == "node_delete") {
        int nid;
        if (!(ss >> nid)) {
            spdlog::error("Usage: node_delete <nid>");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity node_e = find_node_by_id(registry, nid);
        if (node_e == entt::null) {
            spdlog::error("Node {} not found.", nid);
            return;
        }
        // Safety: refuse delete if the node is still referenced by any element
        auto view_elems = registry.view<const Component::ElementID, const Component::Connectivity>();
        for (auto elem_e : view_elems) {
            const auto& conn = view_elems.get<const Component::Connectivity>(elem_e);
            if (std::find(conn.nodes.begin(), conn.nodes.end(), node_e) != conn.nodes.end()) {
                int eid = view_elems.get<const Component::ElementID>(elem_e).value;
                spdlog::error("Cannot delete node {}: used by element {}.", nid, eid);
                return;
            }
        }
        registry.destroy(node_e);
        spdlog::info("Node {} deleted.", nid);
        session.mesh_loaded = true;
        invalidate_topology_if_needed(session);
        rebuild_inspector_if_mesh_loaded(session);
    }
    else if (command == "elem_add") {
        int type_id;
        if (!(ss >> type_id)) {
            spdlog::error("Usage: elem_add <typeid> <nid1> <nid2> ...");
            return;
        }
        if (type_id <= 0) {
            spdlog::error("elem_add requires a positive typeid.");
            return;
        }
        std::vector<int> node_ids;
        for (int nid; ss >> nid; ) {
            node_ids.push_back(nid);
        }
        if (node_ids.size() < 2) {
            spdlog::error("elem_add requires at least 2 node IDs.");
            return;
        }
        auto& registry = session.data.registry;
        int eid = allocate_next_element_id(registry);
        std::vector<entt::entity> node_entities;
        node_entities.reserve(node_ids.size());
        for (int nid : node_ids) {
            entt::entity ne = find_node_by_id(registry, nid);
            if (ne == entt::null) {
                spdlog::error("Node {} not found. Aborting elem_add.", nid);
                return;
            }
            node_entities.push_back(ne);
        }
        auto e = registry.create();
        registry.emplace<Component::ElementID>(e, eid);
        registry.emplace<Component::OriginalID>(e, eid);
        registry.emplace<Component::ElementType>(e, type_id);
        auto& conn = registry.emplace<Component::Connectivity>(e);
        conn.nodes = std::move(node_entities);
        spdlog::info("Element {} created with {} nodes (type_id={}).", eid, conn.nodes.size(), type_id);
        session.mesh_loaded = true;
        invalidate_topology_if_needed(session);
        rebuild_inspector_if_mesh_loaded(session);
    }
    else if (command == "elem_delete") {
        int eid;
        if (!(ss >> eid)) {
            spdlog::error("Usage: elem_delete <eid>");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity elem_e = find_element_by_id(registry, eid);
        if (elem_e == entt::null) {
            spdlog::error("Element {} not found.", eid);
            return;
        }
        // Remove Surfaces tied to this element
        {
            std::vector<entt::entity> surfaces_to_delete;
            auto surf_view = registry.view<const Component::SurfaceParentElement>();
            for (auto se : surf_view) {
                const auto& pe = surf_view.get<const Component::SurfaceParentElement>(se).element;
                if (pe == elem_e) {
                    surfaces_to_delete.push_back(se);
                }
            }
            if (!surfaces_to_delete.empty()) {
                // Remove from all SurfaceSetMembers
                std::unordered_set<entt::entity> surf_set(surfaces_to_delete.begin(), surfaces_to_delete.end());
                auto sset_view = registry.view<Component::SurfaceSetMembers>();
                for (auto set_e : sset_view) {
                    auto& mem = registry.get<Component::SurfaceSetMembers>(set_e).members;
                    mem.erase(
                        std::remove_if(mem.begin(), mem.end(), [&](entt::entity x) {
                            return surf_set.find(x) != surf_set.end();
                        }),
                        mem.end()
                    );
                }
                for (auto se : surfaces_to_delete) {
                    if (registry.valid(se)) registry.destroy(se);
                }
            }
        }
        // Remove this element from all ElementSetMembers
        {
            auto eset_view = registry.view<Component::ElementSetMembers>();
            for (auto set_e : eset_view) {
                auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
                mem.erase(
                    std::remove(mem.begin(), mem.end(), elem_e),
                    mem.end()
                );
            }
        }
        registry.destroy(elem_e);
        spdlog::info("Element {} deleted.", eid);
        session.mesh_loaded = true;
        invalidate_topology_if_needed(session);
        rebuild_inspector_if_mesh_loaded(session);
    }
    // =======================================================
    // Basic set operations
    // =======================================================
    else if (command == "list_sets") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
            return;
        }
        auto& registry = session.data.registry;
        auto view = registry.view<const Component::SetName>();
        spdlog::info("Sets (total {}):", view.size());
        for (auto e : view) {
            const auto& name = view.get<const Component::SetName>(e).value;
            bool has_node = registry.all_of<Component::NodeSetMembers>(e);
            bool has_elem = registry.all_of<Component::ElementSetMembers>(e);
            bool has_surf = registry.all_of<Component::SurfaceSetMembers>(e);
            std::string type = "generic";
            std::size_t count = 0;
            if (has_node && !has_elem && !has_surf) {
                type = "node";
                count = registry.get<Component::NodeSetMembers>(e).members.size();
            } else if (!has_node && has_elem && !has_surf) {
                type = "element";
                count = registry.get<Component::ElementSetMembers>(e).members.size();
            } else if (!has_node && !has_elem && has_surf) {
                type = "surface";
                count = registry.get<Component::SurfaceSetMembers>(e).members.size();
            } else if (has_node || has_elem || has_surf) {
                type = "mixed";
            }
            spdlog::info("  - {} (type={}, size={})", name, type, count);
        }
    }
    else if (command == "set_info") {
        if (!session.mesh_loaded) {
            spdlog::warn("No mesh loaded.");
            return;
        }
        std::string set_name;
        ss >> set_name;
        if (set_name.empty()) {
            spdlog::error("Usage: set_info <set_name>");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity set_e = find_set_by_name(registry, set_name);
        if (set_e == entt::null) {
            spdlog::error("Set '{}' not found.", set_name);
            return;
        }
        spdlog::info("Set '{}':", set_name);
        if (registry.all_of<Component::NodeSetMembers>(set_e)) {
            const auto& mem = registry.get<Component::NodeSetMembers>(set_e).members;
            spdlog::info("  Node members ({}):", mem.size());
            std::string line;
            int printed = 0;
            for (auto n : mem) {
                if (!registry.valid(n) || !registry.all_of<Component::NodeID>(n)) continue;
                int nid = registry.get<Component::NodeID>(n).value;
                if (!line.empty()) line += ", ";
                line += std::to_string(nid);
                ++printed;
                if (printed >= 32) {
                    spdlog::info("    {}", line);
                    line.clear();
                    printed = 0;
                }
            }
            if (!line.empty()) spdlog::info("    {}", line);
        }
        if (registry.all_of<Component::ElementSetMembers>(set_e)) {
            const auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
            spdlog::info("  Element members ({}):", mem.size());
            std::string line;
            int printed = 0;
            for (auto el : mem) {
                if (!registry.valid(el) || !registry.all_of<Component::ElementID>(el)) continue;
                int eid = registry.get<Component::ElementID>(el).value;
                if (!line.empty()) line += ", ";
                line += std::to_string(eid);
                ++printed;
                if (printed >= 32) {
                    spdlog::info("    {}", line);
                    line.clear();
                    printed = 0;
                }
            }
            if (!line.empty()) spdlog::info("    {}", line);
        }
    }
    else if (command == "set_addnode") {
        std::string set_name;
        ss >> set_name;
        if (set_name.empty()) {
            spdlog::error("Usage: set_addnode <set_name> <nid1> [nid2 ...]");
            return;
        }
        std::vector<int> node_ids;
        for (int nid; ss >> nid; ) {
            node_ids.push_back(nid);
        }
        if (node_ids.empty()) {
            spdlog::error("set_addnode requires at least one node id.");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity set_e = get_or_create_set_entity(registry, set_name);
        auto& mem = registry.get_or_emplace<Component::NodeSetMembers>(set_e);
        std::size_t added = 0;
        for (int nid : node_ids) {
            entt::entity ne = find_node_by_id(registry, nid);
            if (ne == entt::null) {
                spdlog::warn("Node {} not found. Skipped in set_addnode.", nid);
                continue;
            }
            if (std::find(mem.members.begin(), mem.members.end(), ne) == mem.members.end()) {
                mem.members.push_back(ne);
                ++added;
            }
        }
        spdlog::info("set_addnode '{}' : added {} nodes.", set_name, added);
    }
    else if (command == "set_addelem") {
        std::string set_name;
        ss >> set_name;
        if (set_name.empty()) {
            spdlog::error("Usage: set_addelem <set_name> <eid1> [eid2 ...]");
            return;
        }
        std::vector<int> elem_ids;
        for (int eid; ss >> eid; ) {
            elem_ids.push_back(eid);
        }
        if (elem_ids.empty()) {
            spdlog::error("set_addelem requires at least one element id.");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity set_e = get_or_create_set_entity(registry, set_name);
        auto& mem = registry.get_or_emplace<Component::ElementSetMembers>(set_e);
        std::size_t added = 0;
        for (int eid : elem_ids) {
            entt::entity ee = find_element_by_id(registry, eid);
            if (ee == entt::null) {
                spdlog::warn("Element {} not found. Skipped in set_addelem.", eid);
                continue;
            }
            if (std::find(mem.members.begin(), mem.members.end(), ee) == mem.members.end()) {
                mem.members.push_back(ee);
                ++added;
            }
        }
        spdlog::info("set_addelem '{}' : added {} elements.", set_name, added);
    }
    else if (command == "set_removenode") {
        std::string set_name;
        ss >> set_name;
        if (set_name.empty()) {
            spdlog::error("Usage: set_removenode <set_name> <nid1> [nid2 ...]");
            return;
        }
        std::vector<int> node_ids;
        for (int nid; ss >> nid; ) {
            node_ids.push_back(nid);
        }
        if (node_ids.empty()) {
            spdlog::error("set_removenode requires at least one node id.");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity set_e = find_set_by_name(registry, set_name);
        if (set_e == entt::null || !registry.all_of<Component::NodeSetMembers>(set_e)) {
            spdlog::error("Node set '{}' not found.", set_name);
            return;
        }
        auto& mem = registry.get<Component::NodeSetMembers>(set_e).members;
        std::size_t removed = 0;
        for (int nid : node_ids) {
            entt::entity ne = find_node_by_id(registry, nid);
            if (ne == entt::null) continue;
            auto it = std::remove(mem.begin(), mem.end(), ne);
            if (it != mem.end()) {
                removed += static_cast<std::size_t>(mem.end() - it);
                mem.erase(it, mem.end());
            }
        }
        spdlog::info("set_removenode '{}' : removed {} entries.", set_name, removed);
    }
    else if (command == "set_removeelem") {
        std::string set_name;
        ss >> set_name;
        if (set_name.empty()) {
            spdlog::error("Usage: set_removeelem <set_name> <eid1> [eid2 ...]");
            return;
        }
        std::vector<int> elem_ids;
        for (int eid; ss >> eid; ) {
            elem_ids.push_back(eid);
        }
        if (elem_ids.empty()) {
            spdlog::error("set_removeelem requires at least one element id.");
            return;
        }
        auto& registry = session.data.registry;
        entt::entity set_e = find_set_by_name(registry, set_name);
        if (set_e == entt::null || !registry.all_of<Component::ElementSetMembers>(set_e)) {
            spdlog::error("Element set '{}' not found.", set_name);
            return;
        }
        auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
        std::size_t removed = 0;
        for (int eid : elem_ids) {
            entt::entity ee = find_element_by_id(registry, eid);
            if (ee == entt::null) continue;
            auto it = std::remove(mem.begin(), mem.end(), ee);
            if (it != mem.end()) {
                removed += static_cast<std::size_t>(mem.end() - it);
                mem.erase(it, mem.end());
            }
        }
        spdlog::info("set_removeelem '{}' : removed {} entries.", set_name, removed);
    }
    // =======================================================
    // Material parameter editing
    // =======================================================
    else if (command == "set_material") {
        int mid;
        std::string component_name, param_name;
        double value;
        if (!(ss >> mid >> component_name >> param_name >> value)) {
            spdlog::error("Usage: set_material <material_id> <component> <param> <value>");
            spdlog::info("  Component: LinearElastic | IsotropicPlastic | RateDependentPlastic | HyperelasticMode");
            spdlog::info("  Params (LinearElastic): rho, E, nu");
            spdlog::info("  Params (IsotropicPlastic): yield_stress_A, hardening_coef_B, hardening_exp_n, rate_coef_C, hardening_mode, temperature_exp_m, melt_temperature, env_temperature, ref_strain_rate, specific_heat");
            spdlog::info("  Params (RateDependentPlastic): hardening_mode, failure_plastic_strain, fail_begin_tensile_strain, fail_end_tensile_strain, elem_del_tensile_strain");
            spdlog::info("  Params (HyperelasticMode): order, nu");
            return;
        }

        auto& registry = session.data.registry;
        entt::entity mat_e = find_material_by_id(registry, mid);
        if (mat_e == entt::null) {
            spdlog::error("Material {} not found.", mid);
            return;
        }

        auto print_current = [&]() {
            const auto& model = registry.get<::Component::MaterialModel>(mat_e).value;
            spdlog::info("Material {} (Model: {}): parameter updated.", mid, model);
        };

        if (component_name == "LinearElastic") {
            if (!registry.all_of<::Component::LinearElasticParams>(mat_e)) {
                spdlog::error("Material {} does not have LinearElasticParams.", mid);
                return;
            }
            auto& p = registry.get<::Component::LinearElasticParams>(mat_e);
            if (param_name == "rho")      { p.rho = value; }
            else if (param_name == "E")   { p.E = value; }
            else if (param_name == "nu")  { p.nu = value; }
            else { spdlog::error("Unknown LinearElastic param: '{}'. Valid: rho, E, nu", param_name); return; }
            spdlog::info("Material {} LinearElastic.{} = {}", mid, param_name, value);
            print_current();
        }
        else if (component_name == "IsotropicPlastic") {
            if (!registry.all_of<::Component::IsotropicPlasticParams>(mat_e)) {
                spdlog::error("Material {} does not have IsotropicPlasticParams.", mid);
                return;
            }
            auto& p = registry.get<::Component::IsotropicPlasticParams>(mat_e);
            if (param_name == "yield_stress_A")            p.yield_stress_A = value;
            else if (param_name == "hardening_coef_B")     p.hardening_coef_B = value;
            else if (param_name == "hardening_exp_n")      p.hardening_exp_n = value;
            else if (param_name == "rate_coef_C")          p.rate_coef_C = value;
            else if (param_name == "hardening_mode")       p.hardening_mode = value;
            else if (param_name == "temperature_exp_m")    p.temperature_exp_m = value;
            else if (param_name == "melt_temperature")     p.melt_temperature = value;
            else if (param_name == "env_temperature")      p.env_temperature = value;
            else if (param_name == "ref_strain_rate")      p.ref_strain_rate = value;
            else if (param_name == "specific_heat")        p.specific_heat = value;
            else { spdlog::error("Unknown IsotropicPlastic param: '{}'.", param_name); return; }
            spdlog::info("Material {} IsotropicPlastic.{} = {}", mid, param_name, value);
            print_current();
        }
        else if (component_name == "RateDependentPlastic") {
            if (!registry.all_of<::Component::RateDependentPlasticParams>(mat_e)) {
                spdlog::error("Material {} does not have RateDependentPlasticParams.", mid);
                return;
            }
            auto& p = registry.get<::Component::RateDependentPlasticParams>(mat_e);
            if (param_name == "hardening_mode")               p.hardening_mode = value;
            else if (param_name == "failure_plastic_strain")  p.failure_plastic_strain = value;
            else if (param_name == "fail_begin_tensile_strain") p.fail_begin_tensile_strain = value;
            else if (param_name == "fail_end_tensile_strain") p.fail_end_tensile_strain = value;
            else if (param_name == "elem_del_tensile_strain") p.elem_del_tensile_strain = value;
            else { spdlog::error("Unknown RateDependentPlastic param: '{}'.", param_name); return; }
            spdlog::info("Material {} RateDependentPlastic.{} = {}", mid, param_name, value);
            print_current();
        }
        else if (component_name == "HyperelasticMode") {
            if (!registry.all_of<::Component::HyperelasticMode>(mat_e)) {
                spdlog::error("Material {} does not have HyperelasticMode.", mid);
                return;
            }
            auto& p = registry.get<::Component::HyperelasticMode>(mat_e);
            if (param_name == "order")   { p.order = static_cast<int>(value); }
            else if (param_name == "nu") { p.nu = value; }
            else { spdlog::error("Unknown HyperelasticMode param: '{}'. Valid: order, nu", param_name); return; }
            spdlog::info("Material {} HyperelasticMode.{} = {}", mid, param_name, value);
            print_current();
        }
        else {
            spdlog::error("Unknown component: '{}'. Valid: LinearElastic, IsotropicPlastic, RateDependentPlastic, HyperelasticMode", component_name);
            return;
        }
    }
    // =======================================================
    // Section property editing
    // =======================================================
    else if (command == "set_section") {
        int sid;
        std::string section_type, param_name, value_str;
        if (!(ss >> sid >> section_type >> param_name >> value_str)) {
            spdlog::error("Usage: set_section <section_id> <section_type> <param> <value>");
            spdlog::info("  Section types: Solid, Truss, Shell, SolidAdvanced, SolidShell, SolidShComp, Beam, FiberBeam, Cohesive, AxialSpringDamper, BeamSpring");
            return;
        }

        auto& registry = session.data.registry;
        entt::entity prop_e = find_property_by_id(registry, sid);
        if (prop_e == entt::null) {
            spdlog::error("Section {} not found.", sid);
            return;
        }

        auto parse_bool = [](const std::string& s) -> bool {
            return s == "true" || s == "1" || s == "yes" || s == "on";
        };

        if (section_type == "Solid") {
            if (!registry.all_of<Component::SolidProperty>(prop_e)) {
                spdlog::error("Section {} does not have SolidProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::SolidProperty>(prop_e);
            if (param_name == "type_id")                  { p.type_id = std::stoi(value_str); }
            else if (param_name == "integration_network") { p.integration_network = std::stoi(value_str); }
            else if (param_name == "hourglass_control")   { p.hourglass_control = value_str; }
            else {
                spdlog::error("Unknown Solid param: '{}'. Valid: type_id, integration_network, hourglass_control", param_name);
                return;
            }
        }
        else if (section_type == "Truss") {
            if (!registry.all_of<Component::TrussProperty>(prop_e)) {
                spdlog::error("Section {} does not have TrussProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::TrussProperty>(prop_e);
            if (param_name == "area") { p.area = std::stod(value_str); }
            else {
                spdlog::error("Unknown Truss param: '{}'. Valid: area", param_name);
                return;
            }
        }
        else if (section_type == "Shell") {
            if (!registry.all_of<Component::ShellProperty>(prop_e)) {
                spdlog::error("Section {} does not have ShellProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::ShellProperty>(prop_e);
            if (param_name == "type_id")              { p.type_id = std::stoi(value_str); }
            else if (param_name == "thick0")          { p.thickness[0] = std::stod(value_str); }
            else if (param_name == "thick1")          { p.thickness[1] = std::stod(value_str); }
            else if (param_name == "thick2")          { p.thickness[2] = std::stod(value_str); }
            else if (param_name == "thick3")          { p.thickness[3] = std::stod(value_str); }
            else if (param_name == "thickness_change") { p.thickness_change = parse_bool(value_str); }
            else if (param_name == "drill_dof")       { p.drill_dof = parse_bool(value_str); }
            else if (param_name == "shear_factor")    { p.shear_factor = std::stod(value_str); }
            else if (param_name == "integration_points") { p.integration_points = std::stoi(value_str); }
            else if (param_name == "inp_rule")        { p.inp_rule = value_str; }
            else if (param_name == "fail_thick")      { p.fail_thick = std::stod(value_str); }
            else if (param_name == "hg_hm")           { p.hourglass_coefs[0] = std::stod(value_str); }
            else if (param_name == "hg_hf")           { p.hourglass_coefs[1] = std::stod(value_str); }
            else if (param_name == "hg_hr")           { p.hourglass_coefs[2] = std::stod(value_str); }
            else if (param_name == "plastic_return")  { p.plastic_plane_stress_return = value_str; }
            else if (param_name == "mid_shell_flag")  { p.mid_shell_flag = value_str; }
            else {
                spdlog::error("Unknown Shell param: '{}'.", param_name);
                return;
            }
        }
        else if (section_type == "SolidAdvanced") {
            if (!registry.all_of<Component::SolidAdvancedProperty>(prop_e)) {
                spdlog::error("Section {} does not have SolidAdvancedProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::SolidAdvancedProperty>(prop_e);
            if (param_name == "formulation")         { p.formulation = value_str; }
            else if (param_name == "small_strain")   { p.small_strain = value_str; }
            else if (param_name == "const_pressure") { p.const_pressure = value_str; }
            else if (param_name == "co_rotation")    { p.co_rotation_flag = value_str; }
            else if (param_name == "visco_hourglass_k") { p.visco_hourglass_k = std::stod(value_str); }
            else if (param_name == "qa")             { p.bulk_viscosity.quadratic = std::stod(value_str); }
            else if (param_name == "qb")             { p.bulk_viscosity.linear = std::stod(value_str); }
            else if (param_name == "dtmin")          { p.dtmin = std::stod(value_str); }
            else if (param_name == "numeric_damping") { p.numeric_damping = std::stod(value_str); }
            else if (param_name == "distortion_control") { p.distortion_control = parse_bool(value_str); }
            else if (param_name == "dc0")            { p.distortion_coeffs[0] = std::stod(value_str); }
            else if (param_name == "dc1")            { p.distortion_coeffs[1] = std::stod(value_str); }
            else if (param_name == "dc2")            { p.distortion_coeffs[2] = std::stod(value_str); }
            else if (param_name == "disp_hg_factor") { p.disp_hourglass_factor = std::stod(value_str); }
            else if (param_name == "hourglass_type") { p.hourglass_type = value_str; }
            else if (param_name == "ele_charac_length") { p.ele_charac_length = parse_bool(value_str); }
            else {
                spdlog::error("Unknown SolidAdvanced param: '{}'.", param_name);
                return;
            }
        }
        else if (section_type == "SolidShell") {
            if (!registry.all_of<Component::SolidShellProperty>(prop_e)) {
                spdlog::error("Section {} does not have SolidShellProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::SolidShellProperty>(prop_e);
            if (param_name == "formulation")         { p.formulation.value = value_str; }
            else if (param_name == "small_strain")   { p.small_strain.value = value_str; }
            else if (param_name == "inpts0")         { p.integration_points[0] = std::stoi(value_str); }
            else if (param_name == "inpts1")         { p.integration_points[1] = std::stoi(value_str); }
            else if (param_name == "inpts2")         { p.integration_points[2] = std::stoi(value_str); }
            else if (param_name == "visco_hourglass_k") { p.visco_hourglass_k = std::stod(value_str); }
            else if (param_name == "qa")             { p.bulk_viscosity.quadratic = std::stod(value_str); }
            else if (param_name == "qb")             { p.bulk_viscosity.linear = std::stod(value_str); }
            else if (param_name == "dtmin")          { p.dtmin = std::stod(value_str); }
            else if (param_name == "thickness_penalty") { p.thickness_penalty = std::stod(value_str); }
            else if (param_name == "dc0")            { p.distortion_coeffs[0] = std::stod(value_str); }
            else if (param_name == "dc1")            { p.distortion_coeffs[1] = std::stod(value_str); }
            else if (param_name == "dc2")            { p.distortion_coeffs[2] = std::stod(value_str); }
            else {
                spdlog::error("Unknown SolidShell param: '{}'.", param_name);
                return;
            }
        }
        else if (section_type == "SolidShComp") {
            if (!registry.all_of<Component::SolidShCompProperty>(prop_e)) {
                spdlog::error("Section {} does not have SolidShCompProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::SolidShCompProperty>(prop_e);
            if (param_name == "formulation")         { p.formulation.value = value_str; }
            else if (param_name == "small_strain")   { p.small_strain.value = value_str; }
            else if (param_name == "inpts0")         { p.integration_points[0] = std::stoi(value_str); }
            else if (param_name == "inpts1")         { p.integration_points[1] = std::stoi(value_str); }
            else if (param_name == "inpts2")         { p.integration_points[2] = std::stoi(value_str); }
            else if (param_name == "numeric_damping") { p.numeric_damping = std::stod(value_str); }
            else if (param_name == "qa")             { p.bulk_viscosity.quadratic = std::stod(value_str); }
            else if (param_name == "qb")             { p.bulk_viscosity.linear = std::stod(value_str); }
            else if (param_name == "thickness_penalty") { p.thickness_penalty = std::stod(value_str); }
            else if (param_name == "coord_sys")      { p.coord_sys.value = value_str; }
            else {
                spdlog::error("Unknown SolidShComp param: '{}'.", param_name);
                return;
            }
        }
        else if (section_type == "Beam") {
            if (!registry.all_of<Component::BeamProperty>(prop_e)) {
                spdlog::error("Section {} does not have BeamProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::BeamProperty>(prop_e);
            if (param_name == "small_strain") { p.small_strain.value = value_str; }
            else if (param_name == "area")    { p.area = std::stod(value_str); }
            else if (param_name == "ixx")     { p.ixx = std::stod(value_str); }
            else if (param_name == "iyy")     { p.iyy = std::stod(value_str); }
            else if (param_name == "izz")     { p.izz = std::stod(value_str); }
            else if (param_name == "shear_flag") { p.shear_flag = parse_bool(value_str); }
            else {
                spdlog::error("Unknown Beam param: '{}'. Valid: small_strain, area, ixx, iyy, izz, shear_flag", param_name);
                return;
            }
        }
        else if (section_type == "FiberBeam") {
            if (!registry.all_of<Component::FiberBeamProperty>(prop_e)) {
                spdlog::error("Section {} does not have FiberBeamProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::FiberBeamProperty>(prop_e);
            if (param_name == "pattern")            { p.pattern = value_str; }
            else if (param_name == "small_strain")  { p.small_strain.value = value_str; }
            else if (param_name == "integration_points") { p.integration_points = std::stoi(value_str); }
            else {
                spdlog::error("Unknown FiberBeam param: '{}'. Valid: pattern, small_strain, integration_points", param_name);
                return;
            }
        }
        else if (section_type == "Cohesive") {
            if (!registry.all_of<Component::CohesiveProperty>(prop_e)) {
                spdlog::error("Section {} does not have CohesiveProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::CohesiveProperty>(prop_e);
            if (param_name == "small_strain") { p.small_strain.value = value_str; }
            else if (param_name == "thickness") { p.thickness = std::stod(value_str); }
            else {
                spdlog::error("Unknown Cohesive param: '{}'. Valid: small_strain, thickness", param_name);
                return;
            }
        }
        else if (section_type == "AxialSpringDamper") {
            if (!registry.all_of<Component::AxialSpringDamperProperty>(prop_e)) {
                spdlog::error("Section {} does not have AxialSpringDamperProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::AxialSpringDamperProperty>(prop_e);
            if (param_name == "mass")               { p.mass = std::stod(value_str); }
            else if (param_name == "stiffness")     { p.stiffness = std::stod(value_str); }
            else if (param_name == "damping")       { p.damping = std::stod(value_str); }
            else if (param_name == "hardening_flag") { p.hardening_flag = value_str; }
            else if (param_name == "nonlinear_spring") { p.nonlinear_spring = parse_bool(value_str); }
            else if (param_name == "nonlinear_damper") { p.nonlinear_damper = parse_bool(value_str); }
            else {
                spdlog::error("Unknown AxialSpringDamper param: '{}'. Valid: mass, stiffness, damping, hardening_flag, nonlinear_spring, nonlinear_damper", param_name);
                return;
            }
        }
        else if (section_type == "BeamSpring") {
            if (!registry.all_of<Component::BeamSpringProperty>(prop_e)) {
                spdlog::error("Section {} does not have BeamSpringProperty.", sid);
                return;
            }
            auto& p = registry.get<Component::BeamSpringProperty>(prop_e);
            if (param_name == "mass")               { p.mass = std::stod(value_str); }
            else if (param_name == "inertia")       { p.inertia = std::stod(value_str); }
            else if (param_name == "coord_sys")     { p.coord_sys.value = value_str; }
            else if (param_name == "ref_tran_vel")  { p.ref_tran_vel = std::stod(value_str); }
            else if (param_name == "ref_rot_vel")   { p.ref_rot_vel = std::stod(value_str); }
            else if (param_name == "smooth_strain_rate") { p.smooth_strain_rate = parse_bool(value_str); }
            else if (param_name == "failure_criteria")  { p.failure_criteria = value_str; }
            else if (param_name == "length_flag")       { p.length_flag = value_str; }
            else if (param_name == "failure_model")     { p.failure_model = value_str; }
            else if (param_name == "k0") { p.linear_stiffness[0] = std::stod(value_str); }
            else if (param_name == "k1") { p.linear_stiffness[1] = std::stod(value_str); }
            else if (param_name == "k2") { p.linear_stiffness[2] = std::stod(value_str); }
            else if (param_name == "k3") { p.linear_stiffness[3] = std::stod(value_str); }
            else if (param_name == "k4") { p.linear_stiffness[4] = std::stod(value_str); }
            else if (param_name == "k5") { p.linear_stiffness[5] = std::stod(value_str); }
            else if (param_name == "c0") { p.linear_damping[0] = std::stod(value_str); }
            else if (param_name == "c1") { p.linear_damping[1] = std::stod(value_str); }
            else if (param_name == "c2") { p.linear_damping[2] = std::stod(value_str); }
            else if (param_name == "c3") { p.linear_damping[3] = std::stod(value_str); }
            else if (param_name == "c4") { p.linear_damping[4] = std::stod(value_str); }
            else if (param_name == "c5") { p.linear_damping[5] = std::stod(value_str); }
            else {
                spdlog::error("Unknown BeamSpring param: '{}'.", param_name);
                return;
            }
        }
        else {
            spdlog::error("Unknown section type: '{}'. Valid: Solid, Truss, Shell, SolidAdvanced, SolidShell, SolidShComp, Beam, FiberBeam, Cohesive, AxialSpringDamper, BeamSpring", section_type);
            return;
        }

        spdlog::info("Section {} {}.{} = {}", sid, section_type, param_name, value_str);
    }
    else if (command == "list_nodes") {
        auto& registry = session.data.registry;
        tui::render_nodes_list(registry);
    }
    else if (command == "list_elements") {
        auto& registry = session.data.registry;
        const int selected_eid = tui::render_elements_list_select(registry);
        if (selected_eid >= 0) {
            session.inspector.inspect_element(registry, selected_eid);
        }
    }
    else if (command == "node") {
        int nid;
        if (ss >> nid) {
            session.inspector.inspect_node(session.data.registry, nid);
        } else {
            spdlog::error("Usage: node <node_id>");
        }
    }
    else if (command == "elem" || command == "element") {
        int eid;
        if (ss >> eid) {
            session.inspector.inspect_element(session.data.registry, eid);
        } else {
            spdlog::error("Usage: elem <element_id>");
        }
    }
    else if (command == "panel") {
        std::string type, id_or_name;
        if (!(ss >> type >> id_or_name)) {
            spdlog::error("Usage: panel <type> <id_or_name>  (type: node|elem|element|part|set)");
            return;
        }
        tui::PanelEntityKind kind = tui::PanelEntityKind::Unknown;
        std::string display_id;
        entt::entity e = tui::resolve_panel_entity(
            session.data.registry, &session.inspector, type, id_or_name, &kind, &display_id);
        if (e == entt::null) {
            spdlog::error("Panel: '{}' '{}' not found. Ensure mesh is loaded and index built.", type, id_or_name);
            return;
        }
        tui::render_panel(session.data.registry, e, &session.inspector, kind, display_id);
    }
    else {
        spdlog::warn("Unknown command: '{}'. Type 'help' for a list of commands.", command);
    }
}
