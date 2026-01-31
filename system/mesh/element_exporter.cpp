// system/mesh/element_exporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "mesh/element_exporter.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"

namespace ElementExporter {

void save(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering ElementExporter...");
    file << "*element begin\n";

    // Create a view of all element entities
    auto view = registry.view<const Component::Connectivity, const Component::ElementType, const Component::OriginalID>();
    
    for (auto entity : view) {
        const auto& connectivity = view.get<const Component::Connectivity>(entity);
        const auto& elem_type = view.get<const Component::ElementType>(entity);
        const auto& id = view.get<const Component::OriginalID>(entity);
        
        // 新格式: element_id, element_type, [node_ids]
        file << id.value << ", " << elem_type.type_id << ", [";
        
        // Write connectivity (node IDs) inside brackets
        for (size_t i = 0; i < connectivity.nodes.size(); ++i) {
            const auto& node_id = registry.get<Component::OriginalID>(connectivity.nodes[i]);
            file << node_id.value;
            if (i < connectivity.nodes.size() - 1) {
                file << ", ";
            }
        }
        
        file << "]\n";
    }

    file << "*element end\n\n";
    spdlog::debug("<-- Exiting ElementExporter. Exported {} elements.", view.size_hint());
}

} // namespace ElementExporter
