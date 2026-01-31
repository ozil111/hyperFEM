// ExplicitSolver.h
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
 * @class ExplicitSolver
 * @brief System for explicit time integration using central difference method
 * @details Implements central difference method for explicit dynamics:
 *   1. Compute acceleration: a = M^-1 * (f_ext - f_int)
 *   2. Apply boundary conditions (SPC): set constrained accelerations to 0
 *   3. Update velocity (half-step): v_{t+1/2} = v_{t-1/2} + a_t * dt
 *   4. Update position: x_{t+1} = x_t + v_{t+1/2} * dt
 */
class ExplicitSolver {
public:
    /**
     * @brief Perform one time step integration
     * @param registry EnTT registry
     * @param dt Time step size
     */
    static void integrate(entt::registry& registry, double dt);

    /**
     * @brief Compute stable time step (optional, for future use)
     * @param registry EnTT registry
     * @return Estimated stable time step based on CFL condition
     */
    static double compute_stable_timestep(entt::registry& registry);
};
