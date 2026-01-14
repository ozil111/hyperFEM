// CurveSystem.h
#pragma once

#include "entt/entt.hpp"

/**
 * @class CurveSystem
 * @brief System for evaluating curve functions at given time points
 * @details Provides mathematical tools for curve evaluation, including linear interpolation
 */
class CurveSystem {
public:
    /**
     * @brief Evaluate curve value at given time
     * @param registry EnTT registry
     * @param curve_entity Curve entity
     * @param t Time point
     * @return Curve value at time t (scaling factor)
     * @details Returns 1.0 if curve is invalid or time is out of range
     */
    static double evaluate_curve(entt::registry& registry, entt::entity curve_entity, double t);
};
