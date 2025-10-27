// node_parser.cpp
#include "mesh/node_parser.h"
#include "spdlog/spdlog.h"
#include "parser_base/string_utils.h"
#include <iostream>
#include <sstream>
#include <string>


namespace NodeParser {

void parse(std::ifstream& file, Mesh& mesh) {
    std::string line;
    size_t current_index = mesh.get_node_count();

    spdlog::debug("--> Entering NodeParser...");

    while (std::getline(file, line)) {
        trim(line);

        if (line.find("*node end") != std::string::npos) {
            spdlog::debug("<-- Exiting NodeParser.");
            return;
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::stringstream ss(line);
        std::string segment;
        NodeID id;
        double x, y, z;

        try {
            std::getline(ss, segment, ','); id = std::stoi(segment);
            std::getline(ss, segment, ','); x = std::stod(segment);
            std::getline(ss, segment, ','); y = std::stod(segment);
            std::getline(ss, segment, ','); z = std::stod(segment);
        } catch (const std::exception& e) {
            spdlog::warn("NodeParser skipping malformed line: {}", line);
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
    
    throw std::runtime_error("Parser error: Reached end of file without finding *node end.");
}

} // namespace NodeParser