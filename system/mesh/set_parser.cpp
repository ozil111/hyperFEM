// system/mesh/set_parser.cpp
#include "set_parser.h"
#include "parser_base/string_utils.h"
#include "spdlog/spdlog.h"
#include <sstream>
#include <string>
#include <vector>
#include <unordered_map>

namespace SetParser {

// --- Internal Helper Function (Template) ---
template<typename IDType>
void parse_generic_set(
    std::ifstream& file, 
    Mesh& mesh, 
    std::unordered_map<SetID, std::vector<IDType>>& sets_map,
    const std::string& end_keyword,
    const std::string& parser_name) 
{
    std::string line;
    spdlog::debug("--> Entering {}", parser_name);

    while (get_logical_line(file, line)) {
        // preprocess_line(line) is now handled inside get_logical_line

        if (line.find(end_keyword) != std::string::npos) {
            break; // Found the end of the block
        }
        if (line.empty()) {
            continue; // Skip empty/comment lines
        }

        std::stringstream ss(line);
        std::string segment;
        
        try {
            // 1. Parse user-defined ID (we ignore it as per the plan to use names)
            if (!std::getline(ss, segment, ',')) continue;
            // int user_id = std::stoi(segment); // Optional: could be used for validation

            // 2. Parse Set Name
            if (!std::getline(ss, segment, ',')) {
                 throw std::runtime_error("Missing set name.");
            }
            std::string set_name = segment;

            // 3. Get or create a unique, internal SetID for this name
            SetID internal_set_id;
            auto it = mesh.set_name_to_id.find(set_name);
            if (it == mesh.set_name_to_id.end()) {
                // Name not found, create a new entry
                internal_set_id = mesh.next_set_id++;
                mesh.set_name_to_id[set_name] = internal_set_id;
                mesh.set_id_to_name.push_back(set_name);
                spdlog::debug("New set '{}' created with internal ID {}", set_name, internal_set_id);
            } else {
                // Name exists, get its ID. Note: You might want to decide if this should be an error/warning.
                internal_set_id = it->second;
                spdlog::warn("Set name '{}' already exists. Appending contents.", set_name);
            }

            // 4. Parse all remaining entity IDs and add to the set
            std::vector<IDType> entity_ids;
            while (std::getline(ss, segment, ',')) {
                trim(segment);
                if (!segment.empty()) {
                    entity_ids.push_back(static_cast<IDType>(std::stoi(segment)));
                }
            }

            if (entity_ids.empty()) {
                throw std::runtime_error("Set definition contains no entity IDs.");
            }
            
            // 5. Store the IDs in the correct map
            sets_map[internal_set_id].insert(
                sets_map[internal_set_id].end(), 
                entity_ids.begin(), 
                entity_ids.end()
            );

        } catch (const std::exception& e) {
            spdlog::warn("{} skipping line due to error: '{}'. Details: {}", parser_name, line, e.what());
            continue;
        }
    }
    spdlog::debug("<-- Exiting {}", parser_name);
}


// --- Public Interface Implementations ---

void parse_node_sets(std::ifstream& file, Mesh& mesh) {
    parse_generic_set<NodeID>(file, mesh, mesh.node_sets, "*nodeset end", "NodeSetParser");
}

void parse_element_sets(std::ifstream& file, Mesh& mesh) {
    parse_generic_set<ElementID>(file, mesh, mesh.element_sets, "*eleset end", "ElementSetParser");
}

} // namespace SetParser
