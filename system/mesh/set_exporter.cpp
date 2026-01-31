// system/mesh/set_exporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "mesh/set_exporter.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"

namespace SetExporter {

void save_node_sets(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering NodeSetExporter...");
    file << "*nodeset begin\n";

    // Create a view of all node set entities
    auto view = registry.view<const Component::SetName, const Component::NodeSetMembers>();
    
    int set_id = 0; // Simple counter for set IDs in the output file
    for (auto set_entity : view) {
        const auto& set_name = view.get<const Component::SetName>(set_entity);
        const auto& members = view.get<const Component::NodeSetMembers>(set_entity);
        
        if (members.members.empty()) continue; // Don't write empty sets
        
        // Filter invalid members (can happen after entity deletion)
        std::vector<int> ids;
        ids.reserve(members.members.size());
        for (auto node_entity : members.members) {
            if (!registry.valid(node_entity)) continue;
            if (!registry.all_of<Component::OriginalID>(node_entity)) continue;
            ids.push_back(registry.get<Component::OriginalID>(node_entity).value);
        }
        if (ids.empty()) continue;

        // 新格式: set_id, set_name, [member_ids]
        file << set_id << ", " << set_name.value << ", [";
        
        // Write member node IDs inside brackets
        for (size_t i = 0; i < ids.size(); ++i) {
            file << ids[i];
            if (i < ids.size() - 1) {
                file << ", ";
            }
        }
        
        file << "]\n";
        
        ++set_id;
    }

    file << "*nodeset end\n\n";
    spdlog::debug("<-- Exiting NodeSetExporter. Exported {} node sets.", view.size_hint());
}

void save_element_sets(std::ofstream& file, const entt::registry& registry) {
    spdlog::debug("--> Entering ElementSetExporter...");
    file << "*eleset begin\n";

    // Create a view of all element set entities
    auto view = registry.view<const Component::SetName, const Component::ElementSetMembers>();
    
    int set_id = 0; // Simple counter for set IDs in the output file
    for (auto set_entity : view) {
        const auto& set_name = view.get<const Component::SetName>(set_entity);
        const auto& members = view.get<const Component::ElementSetMembers>(set_entity);
        
        if (members.members.empty()) continue; // Don't write empty sets
        
        // Filter invalid members (can happen after entity deletion)
        std::vector<int> ids;
        ids.reserve(members.members.size());
        for (auto elem_entity : members.members) {
            if (!registry.valid(elem_entity)) continue;
            if (!registry.all_of<Component::OriginalID>(elem_entity)) continue;
            ids.push_back(registry.get<Component::OriginalID>(elem_entity).value);
        }
        if (ids.empty()) continue;

        // 新格式: set_id, set_name, [member_ids]
        file << set_id << ", " << set_name.value << ", [";
        
        // Write member element IDs inside brackets
        for (size_t i = 0; i < ids.size(); ++i) {
            file << ids[i];
            if (i < ids.size() - 1) {
                file << ", ";
            }
        }
        
        file << "]\n";
        
        ++set_id;
    }

    file << "*eleset end\n\n";
    spdlog::debug("<-- Exiting ElementSetExporter. Exported {} element sets.", view.size_hint());
}

} // namespace SetExporter
