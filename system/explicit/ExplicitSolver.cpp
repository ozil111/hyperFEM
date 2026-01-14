// ExplicitSolver.cpp
#include "ExplicitSolver.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/load_components.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cctype>
#include <cmath>

void ExplicitSolver::integrate(entt::registry& registry, double dt) {
    // Step 1: Compute acceleration: a = M^-1 * (f_ext - f_int)
    auto node_view = registry.view<Component::Position>();
    
    for (auto node_entity : node_view) {
        // Ensure all required components exist
        if (!registry.all_of<Component::Mass>(node_entity)) {
            continue;  // Skip nodes without mass
        }

        const auto& mass = registry.get<Component::Mass>(node_entity);
        if (std::abs(mass.value) < 1.0e-20) {
            continue;  // Skip nodes with zero mass
        }

        double inv_mass = 1.0 / mass.value;

        // Get forces (default to zero if not present)
        double fx_net = 0.0;
        double fy_net = 0.0;
        double fz_net = 0.0;

        if (registry.all_of<Component::ExternalForce>(node_entity)) {
            const auto& ext_force = registry.get<Component::ExternalForce>(node_entity);
            fx_net += ext_force.fx;
            fy_net += ext_force.fy;
            fz_net += ext_force.fz;
        }

        if (registry.all_of<Component::InternalForce>(node_entity)) {
            const auto& int_force = registry.get<Component::InternalForce>(node_entity);
            fx_net -= int_force.fx;  // Internal force opposes motion
            fy_net -= int_force.fy;
            fz_net -= int_force.fz;
        }

        // Compute acceleration
        double ax = fx_net * inv_mass;
        double ay = fy_net * inv_mass;
        double az = fz_net * inv_mass;

        // Ensure Acceleration component exists
        if (!registry.all_of<Component::Acceleration>(node_entity)) {
            registry.emplace<Component::Acceleration>(node_entity, 0.0, 0.0, 0.0);
        }

        auto& acceleration = registry.get<Component::Acceleration>(node_entity);
        acceleration.ax = ax;
        acceleration.ay = ay;
        acceleration.az = az;
    }

    // Step 2: Apply boundary conditions (SPC) - set constrained accelerations to 0
    auto boundary_view = registry.view<Component::AppliedBoundaryRef>();
    
    for (auto node_entity : boundary_view) {
        const auto& boundary_ref = registry.get<Component::AppliedBoundaryRef>(node_entity);
        entt::entity boundary_entity = boundary_ref.boundary_entity;

        if (!registry.all_of<Component::BoundarySPC>(boundary_entity)) {
            continue;
        }

        const auto& boundary_spc = registry.get<Component::BoundarySPC>(boundary_entity);

        // Ensure Acceleration component exists
        if (!registry.all_of<Component::Acceleration>(node_entity)) {
            registry.emplace<Component::Acceleration>(node_entity, 0.0, 0.0, 0.0);
        }

        auto& acceleration = registry.get<Component::Acceleration>(node_entity);

        // Convert dof string to lowercase
        std::string dof = boundary_spc.dof;
        std::transform(dof.begin(), dof.end(), dof.begin(), ::tolower);

        // Apply constraints (set acceleration to 0 for constrained DOFs)
        if (dof == "all" || dof == "xyz") {
            acceleration.ax = 0.0;
            acceleration.ay = 0.0;
            acceleration.az = 0.0;
        } else if (dof == "x") {
            acceleration.ax = 0.0;
        } else if (dof == "y") {
            acceleration.ay = 0.0;
        } else if (dof == "z") {
            acceleration.az = 0.0;
        } else if (dof == "xy" || dof == "yx") {
            acceleration.ax = 0.0;
            acceleration.ay = 0.0;
        } else if (dof == "xz" || dof == "zx") {
            acceleration.ax = 0.0;
            acceleration.az = 0.0;
        } else if (dof == "yz" || dof == "zy") {
            acceleration.ay = 0.0;
            acceleration.az = 0.0;
        }
    }

    // Step 3: Update velocity (half-step): v_{t+1/2} = v_{t-1/2} + a_t * dt
    // Step 4: Update position: x_{t+1} = x_t + v_{t+1/2} * dt
    for (auto node_entity : node_view) {
        if (!registry.all_of<Component::Acceleration>(node_entity)) {
            continue;
        }

        const auto& acceleration = registry.get<Component::Acceleration>(node_entity);

        // Ensure Velocity component exists
        if (!registry.all_of<Component::Velocity>(node_entity)) {
            registry.emplace<Component::Velocity>(node_entity, 0.0, 0.0, 0.0);
        }

        auto& velocity = registry.get<Component::Velocity>(node_entity);

        // Update velocity (half-step)
        velocity.vx += acceleration.ax * dt;
        velocity.vy += acceleration.ay * dt;
        velocity.vz += acceleration.az * dt;

        // Update position
        auto& position = registry.get<Component::Position>(node_entity);
        position.x += velocity.vx * dt;
        position.y += velocity.vy * dt;
        position.z += velocity.vz * dt;
    }
}

double ExplicitSolver::compute_stable_timestep(entt::registry& registry) {
    // Placeholder implementation for CFL-based time step calculation
    // This would typically compute: dt = CFL * min_element_size / wave_speed
    // For now, return a conservative default
    return 1.0e-6;
}
