// system/mesh/set_parser.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "set_parser.h"
#include "parser_base/string_utils.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace SetParser {

// --- Helper function to find set entity by name ---
entt::entity find_set_by_name(entt::registry& registry, const std::string& set_name) {
    auto view = registry.view<const Component::SetName>();
    for (auto entity : view) {
        if (view.get<const Component::SetName>(entity).value == set_name) {
            return entity;
        }
    }
    return entt::null;
}

// --- Public Interface: Node Sets ---
void parse_node_sets(std::ifstream& file, entt::registry& registry, const std::unordered_map<NodeID, entt::entity>& node_id_map) {
    std::string line;
    spdlog::debug("--> Entering NodeSetParser");

    while (get_logical_line(file, line)) {
        if (line.find("*nodeset end") != std::string::npos) {
            break; // Found the end of the block
        }
        if (line.empty()) {
            continue; // Skip empty/comment lines
        }

        std::stringstream ss(line);
        std::string segment;
        
        try {
            // 1. Parse user-defined ID (we can ignore it or use for validation)
            if (!std::getline(ss, segment, ',')) continue;
            // int user_id = std::stoi(segment); // Optional

            // 2. Parse Set Name
            if (!std::getline(ss, segment, ',')) {
                 throw std::runtime_error("Missing set name.");
            }
            trim(segment);
            std::string set_name = segment;

            // 3. Check if this set entity already exists
            entt::entity set_entity = find_set_by_name(registry, set_name);
            if (set_entity == entt::null) {
                // Create a new set entity
                set_entity = registry.create();
                registry.emplace<Component::SetName>(set_entity, set_name);
                registry.emplace<Component::NodeSetMembers>(set_entity);
                spdlog::debug("Created new node set: '{}'", set_name);
            } else {
                spdlog::warn("Node set '{}' already exists. Appending members.", set_name);
            }

            // 4. Parse all remaining node IDs and convert to entities
            auto& members = registry.get<Component::NodeSetMembers>(set_entity);
            std::vector<NodeID> raw_node_ids;
            
            while (std::getline(ss, segment, ',')) {
                trim(segment);
                if (!segment.empty()) {
                    raw_node_ids.push_back(std::stoi(segment));
                }
            }

            if (raw_node_ids.empty()) {
                throw std::runtime_error("Set definition contains no entity IDs.");
            }
            
            // 5. Convert NodeIDs to entity handles
            for (const auto& node_id : raw_node_ids) {
                auto it = node_id_map.find(node_id);
                if (it == node_id_map.end()) {
                    spdlog::warn("Node set '{}' references undefined node ID: {}", set_name, node_id);
                    continue;
                }
                members.members.push_back(it->second);
            }

        } catch (const std::exception& e) {
            spdlog::warn("NodeSetParser skipping line due to error: '{}'. Details: {}", line, e.what());
            continue;
        }
    }
    
    spdlog::debug("<-- Exiting NodeSetParser");
}

// --- Public Interface: Element Sets ---
void parse_element_sets(std::ifstream& file, entt::registry& registry) {
    std::string line;
    spdlog::debug("--> Entering ElementSetParser");
    
    // Build a temporary mapping from OriginalID to entity for elements
    std::unordered_map<ElementID, entt::entity> element_id_map;
    auto element_view = registry.view<const Component::OriginalID, const Component::Connectivity>();
    for (auto entity : element_view) {
        const auto& original_id = element_view.get<const Component::OriginalID>(entity);
        element_id_map[original_id.value] = entity;
    }

    while (get_logical_line(file, line)) {
        if (line.find("*eleset end") != std::string::npos) {
            break; // Found the end of the block
        }
        if (line.empty()) {
            continue; // Skip empty/comment lines
        }

        std::stringstream ss(line);
        std::string segment;
        
        try {
            // 1. Parse user-defined ID (we can ignore it)
            if (!std::getline(ss, segment, ',')) continue;

            // 2. Parse Set Name
            if (!std::getline(ss, segment, ',')) {
                 throw std::runtime_error("Missing set name.");
            }
            trim(segment);
            std::string set_name = segment;

            // 3. Check if this set entity already exists
            entt::entity set_entity = find_set_by_name(registry, set_name);
            if (set_entity == entt::null) {
                // Create a new set entity
                set_entity = registry.create();
                registry.emplace<Component::SetName>(set_entity, set_name);
                registry.emplace<Component::ElementSetMembers>(set_entity);
                spdlog::debug("Created new element set: '{}'", set_name);
            } else {
                spdlog::warn("Element set '{}' already exists. Appending members.", set_name);
            }

            // 4. Parse all remaining element IDs and convert to entities
            auto& members = registry.get<Component::ElementSetMembers>(set_entity);
            std::vector<ElementID> raw_element_ids;
            
            while (std::getline(ss, segment, ',')) {
                trim(segment);
                if (!segment.empty()) {
                    raw_element_ids.push_back(std::stoi(segment));
                }
            }

            if (raw_element_ids.empty()) {
                throw std::runtime_error("Set definition contains no entity IDs.");
            }
            
            // 5. Convert ElementIDs to entity handles
            for (const auto& elem_id : raw_element_ids) {
                auto it = element_id_map.find(elem_id);
                if (it == element_id_map.end()) {
                    spdlog::warn("Element set '{}' references undefined element ID: {}", set_name, elem_id);
                    continue;
                }
                members.members.push_back(it->second);
            }

        } catch (const std::exception& e) {
            spdlog::warn("ElementSetParser skipping line due to error: '{}'. Details: {}", line, e.what());
            continue;
        }
    }
    
    spdlog::debug("<-- Exiting ElementSetParser");
}

} // namespace SetParser
