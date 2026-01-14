// LoadSystem.h
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
