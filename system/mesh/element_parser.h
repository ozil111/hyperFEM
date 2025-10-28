// element_parser.h
#pragma once
#include "mesh.h"
#include <fstream>

// Encapsulate the element parser in its own namespace for clarity
namespace ElementParser {

/**
 * @brief Parses the element data block (*element begin ... *element end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param mesh [out] The Mesh data object to populate with element data.
 */
void parse(std::ifstream& file, Mesh& mesh);

} // namespace ElementParser
