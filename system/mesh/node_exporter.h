// system/mesh/node_exporter.h
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

// Encapsulate the node exporter in its own namespace
namespace NodeExporter {

/**
 * @brief Saves the node data from the registry to a file stream.
 * @param file [out] The output file stream.
 * @param registry [in] The EnTT registry containing node entities with Position and OriginalID components.
 */
void save(std::ofstream& file, const entt::registry& registry);

} // namespace NodeExporter
