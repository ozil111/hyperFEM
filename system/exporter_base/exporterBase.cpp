// system/exporter_base/exporterBase.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "exporter_base/exporterBase.h"
#include "mesh/node_exporter.h"     // Include the node exporter header
#include "mesh/element_exporter.h"  // Include the element exporter header
#include "mesh/set_exporter.h"      // Include the set exporter header
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <iomanip> // For std::setprecision

bool FemExporter::save(const std::string& filepath, const DataContext& data_context) {
    // std::ios::trunc ensures that if the file exists, it's overwritten.
    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("FemExporter could not open file for writing: {}", filepath);
        return false;
    }

    spdlog::info("FemExporter started for file: {}", filepath);

    // Set precision for floating-point output
    file << std::fixed << std::setprecision(8);

    const auto& registry = data_context.registry;

    try {
        // --- Dispatch tasks to specialized exporters ---

        // 1. Export Nodes
        auto node_count = registry.view<Component::Position>().size();
        if (node_count > 0) {
            NodeExporter::save(file, registry);
        }

        // 2. Export Elements
        auto element_count = registry.view<Component::Connectivity>().size();
        if (element_count > 0) {
            ElementExporter::save(file, registry);
        }

        // 3. Export Node Sets
        auto node_set_view = registry.view<const Component::SetName, const Component::NodeSetMembers>();
        if (node_set_view.size_hint() != 0) {
            SetExporter::save_node_sets(file, registry);
        }

        // 4. Export Element Sets
        auto element_set_view = registry.view<const Component::SetName, const Component::ElementSetMembers>();
        if (element_set_view.size_hint() != 0) {
            SetExporter::save_element_sets(file, registry);
        }

    } catch (const std::exception& e) {
        spdlog::error("A critical error occurred during export: {}", e.what());
        file.close(); // Ensure file is closed on error
        return false;
    }

    file.close();
    spdlog::info("FemExporter finished successfully. Mesh data saved to {}", filepath);
    return true;
}
