// LoadSystem.cpp
#include "LoadSystem.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/load_components.h"
#include "../curve/CurveSystem.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cctype>

void LoadSystem::reset_external_forces(entt::registry& registry) {
    auto node_view = registry.view<Component::ExternalForce>();
    for (auto node_entity : node_view) {
        auto& external_force = registry.get<Component::ExternalForce>(node_entity);
        external_force.fx = 0.0;
        external_force.fy = 0.0;
        external_force.fz = 0.0;
    }
}

void LoadSystem::apply_nodal_loads(entt::registry& registry, double t) {
    // Reset external forces first
    reset_external_forces(registry);

    // Traverse nodes with AppliedLoadRef
    auto node_view = registry.view<Component::AppliedLoadRef>();
    size_t load_count = 0;

    for (auto node_entity : node_view) {
        const auto& load_ref = registry.get<Component::AppliedLoadRef>(node_entity);
        entt::entity load_entity = load_ref.load_entity;

        if (!registry.all_of<Component::NodalLoad>(load_entity)) {
            spdlog::warn("Load entity missing NodalLoad component. Skipping.");
            continue;
        }

        const auto& nodal_load = registry.get<Component::NodalLoad>(load_entity);

        // Calculate scaling factor from curve if available
        double scale_factor = 1.0;
        if (registry.all_of<Component::CurveRef>(load_entity)) {
            const auto& curve_ref = registry.get<Component::CurveRef>(load_entity);
            scale_factor = CurveSystem::evaluate_curve(registry, curve_ref.curve_entity, t);
        }

        // Calculate scaled load value
        double scaled_value = nodal_load.value * scale_factor;

        // Ensure ExternalForce component exists
        if (!registry.all_of<Component::ExternalForce>(node_entity)) {
            registry.emplace<Component::ExternalForce>(node_entity, 0.0, 0.0, 0.0);
        }

        auto& external_force = registry.get<Component::ExternalForce>(node_entity);

        // Convert dof string to lowercase for comparison
        std::string dof = nodal_load.dof;
        std::transform(dof.begin(), dof.end(), dof.begin(), ::tolower);

        // Apply load based on DOF specification
        if (dof == "all" || dof == "xyz") {
            // Apply to all directions
            external_force.fx += scaled_value;
            external_force.fy += scaled_value;
            external_force.fz += scaled_value;
        } else if (dof == "x") {
            external_force.fx += scaled_value;
        } else if (dof == "y") {
            external_force.fy += scaled_value;
        } else if (dof == "z") {
            external_force.fz += scaled_value;
        } else if (dof == "xy" || dof == "yx") {
            external_force.fx += scaled_value;
            external_force.fy += scaled_value;
        } else if (dof == "xz" || dof == "zx") {
            external_force.fx += scaled_value;
            external_force.fz += scaled_value;
        } else if (dof == "yz" || dof == "zy") {
            external_force.fy += scaled_value;
            external_force.fz += scaled_value;
        } else {
            spdlog::warn("Unknown DOF specification: '{}'. Skipping load application.", nodal_load.dof);
            continue;
        }

        load_count++;
    }

    if (load_count > 0) {
        spdlog::debug("Applied {} nodal loads at time {:.6e}.", load_count, t);
    }
}
