// CurveSystem.cpp
#include "CurveSystem.h"
#include "../../data_center/components/load_components.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cmath>

double CurveSystem::evaluate_curve(entt::registry& registry, entt::entity curve_entity, double t) {
    // Check if curve entity exists and has Curve component
    if (!registry.all_of<Component::Curve>(curve_entity)) {
        spdlog::warn("Curve entity missing Curve component. Returning 1.0.");
        return 1.0;
    }

    const auto& curve = registry.get<Component::Curve>(curve_entity);

    // Check if curve data is valid
    if (curve.x.empty() || curve.y.empty() || curve.x.size() != curve.y.size()) {
        spdlog::warn("Invalid curve data. Returning 1.0.");
        return 1.0;
    }

    // Handle different curve types
    if (curve.type == "linear") {
        // Linear interpolation
        const auto& x = curve.x;
        const auto& y = curve.y;

        // Check if time is before first point
        if (t <= x[0]) {
            return y[0];
        }

        // Check if time is after last point
        if (t >= x.back()) {
            return y.back();
        }

        // Find the interval containing t
        for (size_t i = 0; i < x.size() - 1; ++i) {
            if (t >= x[i] && t <= x[i + 1]) {
                // Linear interpolation: y = y0 + (y1 - y0) * (t - x0) / (x1 - x0)
                double x0 = x[i];
                double x1 = x[i + 1];
                double y0 = y[i];
                double y1 = y[i + 1];
                
                if (std::abs(x1 - x0) < 1e-12) {
                    return y0;  // Avoid division by zero
                }
                
                return y0 + (y1 - y0) * (t - x0) / (x1 - x0);
            }
        }

        // Should not reach here, but return last value as fallback
        return y.back();
    } else {
        spdlog::warn("Unknown curve type: '{}'. Returning 1.0.", curve.type);
        return 1.0;
    }
}
