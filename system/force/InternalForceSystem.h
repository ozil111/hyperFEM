// InternalForceSystem.h
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
 * @class InternalForceSystem
 * @brief System for computing internal forces from element stresses
 * @details Computes nodal internal forces by:
 *   1. Computing strain from current displacement (B * u)
 *   2. Computing stress (D * strain)
 *   3. Computing element internal forces (B^T * stress * V)
 *   4. Scattering to nodes
 */
class InternalForceSystem {
public:
    /**
     * @brief Reset all internal forces to zero
     * @param registry EnTT registry
     */
    static void reset_internal_forces(entt::registry& registry);

    /**
     * @brief Compute internal forces for all elements
     * @param registry EnTT registry
     * @details Computes internal forces based on current node positions
     */
    static void compute_internal_forces(entt::registry& registry);
};
