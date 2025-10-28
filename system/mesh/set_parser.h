// system/mesh/set_parser.h
#pragma once
#include "../data_center/mesh.h"
#include <fstream>

// Encapsulate the set parsers in their own namespace
namespace SetParser {

/**
 * @brief Parses the node set data block (*nodeset begin ... *nodeset end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param mesh [out] The Mesh data object to populate with node set data.
 */
void parse_node_sets(std::ifstream& file, Mesh& mesh);

/**
 * @brief Parses the element set data block (*eleset begin ... *eleset end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param mesh [out] The Mesh data object to populate with element set data.
 */
void parse_element_sets(std::ifstream& file, Mesh& mesh);

} // namespace SetParser
