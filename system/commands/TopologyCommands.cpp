/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include <spdlog/spdlog.h>
#include "components/mesh_components.h"
#include "TopologyData.h"
#include "mesh/TopologySystems.h"
#include "tui/ComponentTUI.h"
#include "analysis/GraphBuilder.h"
#include "analysis/MermaidReporter.h"
#include "remesh/ConnectionPreservingRemesher.h"
#include "parser_simdroid/SimdroidParser.h"
#include "exporter_simdroid/SimdroidExporter.h"
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <vector>
#include <string>

namespace cmd::topology {

using namespace cmd::detail;

void handle_build_topology(std::stringstream& ss, AppSession& session) {
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

void handle_list_bodies(std::stringstream& ss, AppSession& session) {
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

void handle_show_body(std::stringstream& ss, AppSession& session) {
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

// =======================================================
// New: Simdroid interactive inspection commands
// =======================================================
void handle_list_parts(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) { spdlog::warn("No mesh loaded."); return; }
    tui::render_parts_list(session.data.registry);
}

void handle_delete_part(std::stringstream& ss, AppSession& session) {
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

void handle_graph(std::stringstream& ss, AppSession& session) {
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

void handle_remesh_plan(std::stringstream& ss, AppSession& session) {
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

void handle_remesh_generate(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
        return;
    }

    std::string output_dir = parse_next_arg(ss);
    if (output_dir.empty()) output_dir = "remeshed";

    double ratio = 100.0;
    if (!(ss >> ratio)) {
        ratio = 100.0;
    }

    RemeshOptions options;
    options.target_compression_ratio = ratio;

    try {
        std::filesystem::path out_dir(output_dir);
        std::filesystem::create_directories(out_dir);

        spdlog::info("Generating remesh...");
        RemeshExecutionResult result =
            ConnectionPreservingRemesher::remesh(
                session.data, session.inspector, options);

        const auto before_path = out_dir / "remesh_before.json";
        const auto after_path = out_dir / "remesh_after.json";
        const auto validation_path = out_dir / "remesh_validation.json";
        const auto result_path = out_dir / "remesh_result.json";

        ConnectionPreservingRemesher::write_plan_json(result.before, before_path.string());
        ConnectionPreservingRemesher::write_plan_json(result.after, after_path.string());
        {
            std::ofstream out(validation_path);
            out << result.validation.to_json().dump(2) << '\n';
        }
        {
            std::ofstream out(result_path);
            out << result.to_json().dump(2) << '\n';
        }

        if (!result.success) {
            spdlog::error("Remesh generation failed: {}", result.message);
            for (const auto& error : result.validation.errors) {
                spdlog::error("  {}", error);
            }
            return;
        }

        session.mesh_loaded = true;
        session.topology_built = false;

        const auto mesh_path = out_dir / "mesh.dat";
        const auto control_path = out_dir / "control.json";
        if (!SimdroidExporter::save(mesh_path.string(), control_path.string(), session.data)) {
            spdlog::error("Remesh generated, but Simdroid export failed.");
            return;
        }

        spdlog::info("Remesh generated: {}", out_dir.string());
        spdlog::info("Elements: {} -> {}",
                     result.before.original_element_count,
                     result.after.original_element_count);
    } catch (const std::exception& e) {
        spdlog::error("Exception during remesh generation: {}", e.what());
    }
}

void handle_validate_constraints(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
        return;
    }

    spdlog::info("Re-running Simdroid constraint/contact validations...");
    auto& registry = session.data.registry;
    SimdroidParser::validate_constraints(registry);
}

void handle_list_constraint_warnings(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::warn("No mesh loaded. Please 'import_simdroid' first.");
        return;
    }
    auto& registry = session.data.registry;
    SimdroidParser::list_constraint_warnings(registry);
}

} // namespace cmd::topology
