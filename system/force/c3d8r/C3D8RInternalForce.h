// C3D8RInternalForce.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include "entt/entt.hpp"

/**
 * @brief Compute and scatter internal forces for a single C3D8R element
 * @param registry EnTT registry containing element and node data
 * @param element_entity Element entity to process
 * @return true if computed successfully, false otherwise
 * @details
 *   - Reads current Position and optional InitialPosition to build element displacement
 *   - Reads material D matrix from LinearElasticMatrix (via PropertyRef -> MaterialRef)
 *   - Computes B-bar at current configuration (reduced integration)
 *   - Computes element internal force vector and accumulates into node InternalForce
 */
bool compute_c3d8r_internal_forces(entt::registry& registry, entt::entity element_entity);

