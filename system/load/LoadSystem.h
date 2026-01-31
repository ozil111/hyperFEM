// LoadSystem.h
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
 * @class LoadSystem
 * @brief System for applying nodal loads to nodes
 * @details Applies loads from AppliedLoadRef to ExternalForce component
 */
class LoadSystem {
public:
    /**
     * @brief Reset all external forces to zero
     * @param registry EnTT registry
     */
    static void reset_external_forces(entt::registry& registry);

    /**
     * @brief Apply nodal loads to nodes
     * @param registry EnTT registry
     * @param t Current time (for curve evaluation)
     * @details Traverses nodes with AppliedLoadRef and applies loads based on NodalLoad definition.
     *          If load has a curve reference, the load value is scaled by the curve value at time t.
     */
    static void apply_nodal_loads(entt::registry& registry, double t);
};
