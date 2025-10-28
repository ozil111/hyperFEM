// system/mesh/element_exporter.h
#pragma once
#include "mesh.h"
#include <fstream>

// Encapsulate the element exporter in its own namespace
namespace ElementExporter {

/**
 * @brief Saves the element data from a Mesh object to a file stream.
 * @param file [out] The output file stream.
 * @param mesh [in] The Mesh data object containing the element data.
 */
void save(std::ofstream& file, const Mesh& mesh);

} // namespace ElementExporter
