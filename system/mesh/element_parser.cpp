// element_parser.cpp
#include "mesh/element_parser.h"
#include "ElementRegistry.h" // Important: For getting element properties
#include "parser_base/string_utils.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <vector>
#include <string>

namespace ElementParser {

void parse(std::ifstream& file, Mesh& mesh) {
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

    // --- Step 2: Reserve memory to avoid reallocations ---
    if (!element_lines.empty()) {
        spdlog::debug("Pre-reserved for {} elements.", element_lines.size());
        size_t num_elements = element_lines.size();
        mesh.element_types.reserve(mesh.element_types.size() + num_elements);
        mesh.element_offsets.reserve(mesh.element_offsets.size() + num_elements);
        mesh.element_index_to_id.reserve(mesh.element_index_to_id.size() + num_elements);
        // We can also make a rough guess for connectivity to reserve, e.g., average 8 nodes per element
        mesh.element_connectivity.reserve(mesh.element_connectivity.size() + num_elements * 8);
    }

    // --- Step 3: Parse from the in-memory vector ---
    size_t current_index = mesh.getElementCount();
    
    // Caching variables for performance optimization
    int cachedTypeId = -1;
    const ElementProperties* cachedProps = nullptr;

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
            std::vector<NodeID> nodeIds;
            nodeIds.reserve(nodesToRead);

            for (int i = 0; i < nodesToRead; ++i) {
                if (!std::getline(ss, segment, ',')) {
                    // Not enough node IDs on the line
                    throw std::runtime_error("Malformed line: not enough node IDs for its type.");
                }
                nodeIds.push_back(std::stoi(segment));
            }

            if (mesh.element_id_to_index.count(id)) {
                spdlog::warn("Duplicate element ID {}. Skipping.", id);
                continue;
            }

            // --- Store data in SoA format ---
            mesh.element_id_to_index[id] = current_index;
            mesh.element_index_to_id.push_back(id);
            mesh.element_types.push_back(typeId);
            mesh.element_offsets.push_back(mesh.element_connectivity.size());
            mesh.element_connectivity.insert(mesh.element_connectivity.end(), nodeIds.begin(), nodeIds.end());
            
            current_index++;

        } catch (const std::exception& e) {
            spdlog::warn("ElementParser skipping line due to error: '{}'. Details: {}", data_line, e.what());
            continue;
        }
    }
    spdlog::debug("<-- Exiting ElementParser.");
}

} // namespace ElementParser
