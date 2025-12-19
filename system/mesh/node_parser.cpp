// node_parser.cpp
#include "mesh/node_parser.h"
#include "spdlog/spdlog.h"
#include "parser_base/string_utils.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


namespace NodeParser {

void parse(std::ifstream& file, entt::registry& registry, std::unordered_map<NodeID, entt::entity>& node_id_map) {
    std::string line;
    std::vector<std::string> node_lines; // Vector to store the data lines

    spdlog::debug("--> Entering NodeParser, pre-scanning block...");

    // --- Step 1: Pre-scan and store relevant lines ---
    // This loop reads the whole block and saves only the actual data lines.
    while (get_logical_line(file, line)) {
        // preprocess_line(line) is now handled inside get_logical_line
        
        if (line.find("*node end") != std::string::npos) {
            break; // Found the end of the block
        }

        if (line.empty()) {
            continue; // Skip empty/comment lines
        }

        node_lines.push_back(line); // Store the clean data line
    }

    // --- Step 2: Reserve capacity in the node_id_map ---
    if (!node_lines.empty()) {
        spdlog::debug("Pre-reserved for {} nodes.", node_lines.size());
        node_id_map.reserve(node_id_map.size() + node_lines.size());
    }

    // --- Step 3: Parse from the in-memory vector and create entities ---
    for (const auto& data_line : node_lines) {
        std::stringstream ss(data_line);
        std::string segment;
        NodeID id;
        double x, y, z;

        try {
            std::getline(ss, segment, ','); id = std::stoi(segment);
            std::getline(ss, segment, ','); x = std::stod(segment);
            std::getline(ss, segment, ','); y = std::stod(segment);
            std::getline(ss, segment, ','); z = std::stod(segment);
        } catch (const std::exception& e) {
            spdlog::warn("NodeParser skipping malformed line: {}", data_line);
            continue;
        }

        if (node_id_map.count(id)) {
            spdlog::warn("Duplicate node ID {}. Skipping.", id);
            continue;
        }

        // Create a node entity
        const entt::entity node_entity = registry.create();
        
        // Attach components
        registry.emplace<Component::Position>(node_entity, x, y, z);
        registry.emplace<Component::OriginalID>(node_entity, id);
        
        // Store in the ID map for use by ElementParser
        node_id_map[id] = node_entity;
    }

    spdlog::debug("<-- Exiting NodeParser. Created {} node entities.", node_id_map.size());
}

} // namespace NodeParser