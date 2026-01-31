// system/mesh/set_exporter.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
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
