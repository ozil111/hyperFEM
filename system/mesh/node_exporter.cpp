// system/mesh/node_exporter.cpp
#include "mesh/node_exporter.h"
#include "spdlog/spdlog.h"

namespace NodeExporter {

void save(std::ofstream& file, const Mesh& mesh) {
    spdlog::debug("--> Entering NodeExporter...");
    file << "*node begin\n";

    size_t node_count = mesh.getNodeCount();
    for (size_t i = 0; i < node_count; ++i) {
        NodeID id = mesh.node_index_to_id[i];
        // Coordinates are stored as [x1, y1, z1, x2, y2, z2, ...]
        double x = mesh.node_coordinates[i * 3 + 0];
        double y = mesh.node_coordinates[i * 3 + 1];
        double z = mesh.node_coordinates[i * 3 + 2];

        file << id << ", " << x << ", " << y << ", " << z << "\n";
    }

    file << "*node end\n\n"; // Add extra newline for readability
    spdlog::debug("<-- Exiting NodeExporter.");
}

} // namespace NodeExporter
