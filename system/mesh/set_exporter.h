// system/mesh/set_exporter.h
#pragma once
#include "entt/entt.hpp"
#include <fstream>

// Encapsulate the set exporters in their own namespace
namespace SetExporter {

/**
 * @brief Saves the node set data from the registry to a file stream.
 * @param file [out] The output file stream.
 * @param registry [in] The EnTT registry containing set entities with SetName and NodeSetMembers components.
 */
void save_node_sets(std::ofstream& file, const entt::registry& registry);

/**
 * @brief Saves the element set data from the registry to a file stream.
 * @param file [out] The output file stream.
 * @param registry [in] The EnTT registry containing set entities with SetName and ElementSetMembers components.
 */
void save_element_sets(std::ofstream& file, const entt::registry& registry);

} // namespace SetExporter
