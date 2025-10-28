// system/mesh/set_exporter.cpp
#include "mesh/set_exporter.h"
#include "spdlog/spdlog.h"

namespace SetExporter {

void save_node_sets(std::ofstream& file, const Mesh& mesh) {
    spdlog::debug("--> Entering NodeSetExporter...");
    file << "*nodeset begin\n";

    // Iterate through the sets in a consistent order using the ID->Name vector
    for (SetID set_id = 0; set_id < mesh.set_id_to_name.size(); ++set_id) {
        const auto it = mesh.node_sets.find(set_id);
        if (it != mesh.node_sets.end()) {
            const std::string& set_name = mesh.set_id_to_name[set_id];
            const std::vector<NodeID>& node_ids = it->second;

            if (node_ids.empty()) continue; // Don't write empty sets

            // For simplicity, we use the internal SetID as the user-facing ID
            file << set_id << ", " << set_name;
            for (const auto& node_id : node_ids) {
                file << ", " << node_id;
            }
            file << "\n";
        }
    }

    file << "*nodeset end\n\n";
    spdlog::debug("<-- Exiting NodeSetExporter.");
}

void save_element_sets(std::ofstream& file, const Mesh& mesh) {
    spdlog::debug("--> Entering ElementSetExporter...");
    file << "*eleset begin\n";

    for (SetID set_id = 0; set_id < mesh.set_id_to_name.size(); ++set_id) {
        const auto it = mesh.element_sets.find(set_id);
        if (it != mesh.element_sets.end()) {
            const std::string& set_name = mesh.set_id_to_name[set_id];
            const std::vector<ElementID>& element_ids = it->second;

            if (element_ids.empty()) continue;

            file << set_id << ", " << set_name;
            for (const auto& elem_id : element_ids) {
                file << ", " << elem_id;
            }
            file << "\n";
        }
    }

    file << "*eleset end\n\n";
    spdlog::debug("<-- Exiting ElementSetExporter.");
}

} // namespace SetExporter
