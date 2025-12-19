// system/mesh/element_exporter.h
#pragma once
#include "entt/entt.hpp"
#include <fstream>

// Encapsulate the element exporter in its own namespace
namespace ElementExporter {

/**
 * @brief Saves the element data from the registry to a file stream.
 * @param file [out] The output file stream.
 * @param registry [in] The EnTT registry containing element entities with Connectivity, ElementType, and OriginalID components.
 */
void save(std::ofstream& file, const entt::registry& registry);

} // namespace ElementExporter
