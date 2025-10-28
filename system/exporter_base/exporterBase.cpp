// system/exporter_base/exporterBase.cpp
#include "exporter_base/exporterBase.h"
#include "mesh/node_exporter.h"     // Include the node exporter header
#include "mesh/element_exporter.h"  // Include the element exporter header
#include "mesh/set_exporter.h"      // Include the set exporter header
#include "spdlog/spdlog.h"
#include <fstream>
#include <iomanip> // For std::setprecision

bool FemExporter::save(const std::string& filepath, const Mesh& mesh) {
    // std::ios::trunc ensures that if the file exists, it's overwritten.
    std::ofstream file(filepath, std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("FemExporter could not open file for writing: {}", filepath);
        return false;
    }

    spdlog::info("FemExporter started for file: {}", filepath);

    // Set precision for floating-point output
    file << std::fixed << std::setprecision(8);

    try {
        // --- Dispatch tasks to specialized exporters ---

        // 1. Export Nodes
        if (mesh.getNodeCount() > 0) {
            NodeExporter::save(file, mesh);
        }

        // 2. Export Elements
        if (mesh.getElementCount() > 0) {
            ElementExporter::save(file, mesh);
        }

        // 3. Export Node Sets
        if (!mesh.node_sets.empty()) {
            SetExporter::save_node_sets(file, mesh);
        }

        // 4. Export Element Sets
        if (!mesh.element_sets.empty()) {
            SetExporter::save_element_sets(file, mesh);
        }

    } catch (const std::exception& e) {
        spdlog::error("A critical error occurred during export: {}", e.what());
        file.close(); // Ensure file is closed on error
        return false;
    }

    file.close();
    spdlog::info("FemExporter finished successfully. Mesh data saved to {}", filepath);
    return true;
}
