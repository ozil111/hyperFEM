// element_parser.cpp
#include "mesh/element_parser.h"
#include "ElementRegistry.h" // Important: For getting element properties
#include "parser_base/string_utils.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <vector>
#include <string>

namespace ElementParser {

void parse(std::ifstream& file, entt::registry& registry, const std::unordered_map<NodeID, entt::entity>& node_id_map) {
    std::string line;
    std::vector<std::string> element_lines; // Vector to store the data lines

    spdlog::debug("--> Entering ElementParser, pre-scanning block...");

    // --- Step 1: Pre-scan and store all relevant data lines ---
    while (get_logical_line(file, line)) {
        // preprocess_line(line) is now handled inside get_logical_line
        
        if (line.find("*element end") != std::string::npos) {
            break; // Found the end of the block
        }

        if (line.empty()) {
            continue; // Skip empty/comment lines
        }

        element_lines.push_back(line); // Store the clean data line
    }

    if (!element_lines.empty()) {
        spdlog::debug("Pre-scanning complete. Found {} elements.", element_lines.size());
    }

    // --- Step 2: Parse from the in-memory vector and create entities ---
    
    // Caching variables for performance optimization
    int cachedTypeId = -1;
    const ElementProperties* cachedProps = nullptr;
    
    // Track duplicate IDs
    std::unordered_map<ElementID, entt::entity> element_id_to_entity;

    for (const auto& data_line : element_lines) {
        std::stringstream ss(data_line);
        std::string segment;
        ElementID id;
        int typeId;

        try {
            // Parse Element ID and Type ID
            std::getline(ss, segment, ','); id = std::stoi(segment);
            std::getline(ss, segment, ','); typeId = std::stoi(segment);

            // --- Caching Logic ---
            if (typeId != cachedTypeId) {
                // If type is not cached, look it up in the registry
                cachedProps = &ElementRegistry::getInstance().getProperties(typeId);
                cachedTypeId = typeId; // Update the cache
            }

            int nodesToRead = cachedProps->numNodes;
            std::vector<NodeID> raw_node_ids;
            raw_node_ids.reserve(nodesToRead);

            for (int i = 0; i < nodesToRead; ++i) {
                if (!std::getline(ss, segment, ',')) {
                    // Not enough node IDs on the line
                    throw std::runtime_error("Malformed line: not enough node IDs for its type.");
                }
                raw_node_ids.push_back(std::stoi(segment));
            }

            if (element_id_to_entity.count(id)) {
                spdlog::warn("Duplicate element ID {}. Skipping.", id);
                continue;
            }

            // --- Create element entity and attach components ---
            const entt::entity element_entity = registry.create();
            
            // Attach OriginalID and ElementType
            registry.emplace<Component::OriginalID>(element_entity, id);
            registry.emplace<Component::ElementType>(element_entity, typeId);
            
            // Build Connectivity using node_id_map
            auto& connectivity = registry.emplace<Component::Connectivity>(element_entity);
            connectivity.nodes.reserve(raw_node_ids.size());
            
            for (const auto& node_id : raw_node_ids) {
                auto it = node_id_map.find(node_id);
                if (it == node_id_map.end()) {
                    throw std::runtime_error("Element references undefined node ID: " + std::to_string(node_id));
                }
                connectivity.nodes.push_back(it->second);
            }
            
            element_id_to_entity[id] = element_entity;

        } catch (const std::exception& e) {
            spdlog::warn("ElementParser skipping line due to error: '{}'. Details: {}", data_line, e.what());
            continue;
        }
    }
    
    spdlog::debug("<-- Exiting ElementParser. Created {} element entities.", element_id_to_entity.size());
}

} // namespace ElementParser
