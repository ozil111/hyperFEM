// node_parser.cpp
#include "mesh/node_parser.h"
#include "spdlog/spdlog.h"
#include "parser_base/string_utils.h"
#include <iostream>
#include <sstream>
#include <string>
#include <vector>


namespace NodeParser {

void parse(std::ifstream& file, Mesh& mesh) {
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

    // --- Step 2: Reserve memory based on the stored lines ---
    if (!node_lines.empty()) {
        spdlog::debug("Pre-reserved for {} nodes.", node_lines.size());
        mesh.node_coordinates.reserve(mesh.node_coordinates.size() + node_lines.size() * 3);
        mesh.node_index_to_id.reserve(mesh.node_index_to_id.size() + node_lines.size());
    }

    // --- Step 3: Parse from the in-memory vector ---
    // This is much faster and avoids all file I/O problems.
    size_t current_index = mesh.getNodeCount();
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

        if (mesh.node_id_to_index.count(id)) {
            spdlog::warn("Duplicate node ID {}. Skipping.", id);
            continue;
        }

        mesh.node_coordinates.push_back(x);
        mesh.node_coordinates.push_back(y);
        mesh.node_coordinates.push_back(z);
        mesh.node_id_to_index[id] = current_index;
        mesh.node_index_to_id.push_back(id);
        current_index++;
    }

    spdlog::debug("<-- Exiting NodeParser.");
    // Note: We no longer need the "throw runtime_error" for a missing "*node end",
    // because the main FemParser will handle cases where the file ends unexpectedly.
}

} // namespace NodeParser