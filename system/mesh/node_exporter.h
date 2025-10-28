// system/mesh/node_exporter.h
#pragma once
#include "mesh.h"
#include <fstream>

// Encapsulate the node exporter in its own namespace
namespace NodeExporter {

/**
 * @brief Saves the node data from a Mesh object to a file stream.
 * @param file [out] The output file stream.
 * @param mesh [in] The Mesh data object containing the node data.
 */
void save(std::ofstream& file, const Mesh& mesh);

} // namespace NodeExporter
