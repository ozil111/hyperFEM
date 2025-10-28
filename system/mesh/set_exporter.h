// system/mesh/set_exporter.h
#pragma once
#include "mesh.h"
#include <fstream>

// Encapsulate the set exporters in their own namespace
namespace SetExporter {

/**
 * @brief Saves the node set data from a Mesh object to a file stream.
 * @param file [out] The output file stream.
 * @param mesh [in] The Mesh data object containing node set data.
 */
void save_node_sets(std::ofstream& file, const Mesh& mesh);

/**
 * @brief Saves the element set data from a Mesh object to a file stream.
 * @param file [out] The output file stream.
 * @param mesh [in] The Mesh data object containing element set data.
 */
void save_element_sets(std::ofstream& file, const Mesh& mesh);

} // namespace SetExporter
