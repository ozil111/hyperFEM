// system/mesh/node_exporter.h
#pragma once
#include "entt/entt.hpp"
#include <fstream>

// Encapsulate the node exporter in its own namespace
namespace NodeExporter {

/**
 * @brief Saves the node data from the registry to a file stream.
 * @param file [out] The output file stream.
 * @param registry [in] The EnTT registry containing node entities with Position and OriginalID components.
 */
void save(std::ofstream& file, const entt::registry& registry);

} // namespace NodeExporter
