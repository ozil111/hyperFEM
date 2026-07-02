/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include <spdlog/spdlog.h>
#include "parser_json/JsonParser.h"
#include "parser_base/parserBase.h"
#include "exporter_base/exporterBase.h"
#include "parser_simdroid/SimdroidParser.h"
#include "exporter_simdroid/SimdroidExporter.h"
#include "parser_abaqus/AbaqusParser.h"
#include "exporter_abaqus/AbaqusExporter.h"
#include "components/mesh_components.h"
#include "TopologyData.h"
#include <filesystem>
#include <fstream>
#include <iterator>

namespace cmd::io {

using namespace cmd::detail;

void handle_import(std::stringstream& ss, AppSession& session) {
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
    } else if (extension == ".inp") {
        spdlog::info("Detected Abaqus .inp format, using AbaqusParser...");
        parse_success = AbaqusParser::parse(file_path, session.data);
    } else {
        spdlog::error("Unsupported file format: {}. Supported: .json, .jsonc, .xfem, .inp", extension);
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

void handle_json_apply(std::stringstream& ss, AppSession& session) {
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
void handle_import_simdroid(std::stringstream& ss, AppSession& session) {
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
void handle_export_simdroid(std::stringstream& ss, AppSession& session) {
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

// =======================================================
// New: Abaqus .inp export command
// =======================================================
void handle_export_abaqus(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::error("No mesh loaded. Please 'import' or 'import_simdroid' first.");
        return;
    }

    std::string file_path = read_rest_of_line(ss);
    if (file_path.empty()) {
        spdlog::error("Usage: export_abaqus <path_to_output.inp>");
        return;
    }

    // Ensure .inp extension
    std::filesystem::path out_path(file_path);
    if (out_path.extension().empty()) {
        out_path += ".inp";
    }
    if (!out_path.parent_path().empty()) {
        std::filesystem::create_directories(out_path.parent_path());
    }

    spdlog::info("Exporting Abaqus .inp to: {}", out_path.string());
    if (AbaqusExporter::save(out_path.string(), session.data)) {
        spdlog::info("Abaqus export successful.");
    } else {
        spdlog::error("Abaqus export failed.");
    }
}

void handle_save(std::stringstream& ss, AppSession& session) {
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

void handle_info(std::stringstream& ss, AppSession& session) {
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

} // namespace cmd::io
