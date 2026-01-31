// MassSystem.h
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
 * @class MassSystem
 * @brief System for computing lumped mass matrix for explicit dynamics
 * @details Computes nodal lumped mass by distributing element mass uniformly to nodes
 */
class MassSystem {
public:
    /**
     * @brief Compute lumped mass matrix for all nodes
     * @param registry EnTT registry containing elements and nodes
     * @details 
     *   - Traverses all elements
     *   - Gets material density from LinearElasticParams
     *   - Computes element volume
     *   - Distributes element mass (rho * V) uniformly to 8 nodes
     *   - Accumulates mass in Component::Mass
     */
    static void compute_lumped_mass(entt::registry& registry);
};
