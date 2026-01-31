// C3D8Mass.h
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
 * @brief Compute lumped mass for C3D8 (8-node hexahedron) elements
 * @param registry EnTT registry containing elements and nodes
 * @param element_entity Element entity to process
 * @param n_integration_points Number of integration points per dimension (1 for reduced integration, 2+ for full integration)
 * @return true if mass was successfully computed and distributed, false otherwise
 * @details
 *   - For n_integration_points = 1: Uses B-bar method (reduced integration)
 *   - For n_integration_points > 1: Uses Gaussian integration (full integration) - TODO: Not yet implemented
 *   - Gets material density from LinearElasticParams
 *   - Computes element volume
 *   - Distributes element mass (rho * V) uniformly to 8 nodes
 *   - Accumulates mass in Component::Mass for each node
 */
bool compute_c3d8_mass(entt::registry& registry, entt::entity element_entity, int n_integration_points);
