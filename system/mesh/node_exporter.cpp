// system/mesh/node_exporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "mesh/node_exporter.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"

namespace NodeExporter {

void save(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering NodeExporter...");
    file << "*node begin\n";

    // Create a view of all entities with Position and OriginalID components
    auto view = registry.view<const Component::Position, const Component::OriginalID>();
    
    for (auto entity : view) {
        const auto& pos = view.get<const Component::Position>(entity);
        const auto& id = view.get<const Component::OriginalID>(entity);
        
        file << id.value << ", " << pos.x << ", " << pos.y << ", " << pos.z << "\n";
    }

    file << "*node end\n\n"; // Add extra newline for readability
    spdlog::debug("<-- Exiting NodeExporter. Exported {} nodes.", view.size_hint());
}

} // namespace NodeExporter
