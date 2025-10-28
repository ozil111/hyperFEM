// system/mesh/element_exporter.cpp
#include "mesh/element_exporter.h"
#include "spdlog/spdlog.h"

namespace ElementExporter {

void save(std::ofstream& file, const Mesh& mesh) {
    spdlog::debug("--> Entering ElementExporter...");
    file << "*element begin\n";

    size_t element_count = mesh.getElementCount();
    for (size_t i = 0; i < element_count; ++i) {
        ElementID id = mesh.element_index_to_id[i];
        int type_id = mesh.element_types[i];

        file << id << ", " << type_id;

        // Determine the range of connectivity for this element
        size_t start_offset = mesh.element_offsets[i];
        // The end is either the start of the next element or the end of the whole connectivity vector
        size_t end_offset = (i + 1 < element_count) ? mesh.element_offsets[i + 1] : mesh.element_connectivity.size();

        for (size_t j = start_offset; j < end_offset; ++j) {
            file << ", " << mesh.element_connectivity[j];
        }
        file << "\n";
    }

    file << "*element end\n\n";
    spdlog::debug("<-- Exiting ElementExporter.");
}

} // namespace ElementExporter
