// main0_explicit.cpp
// Extracted explicit solver logic from main.cpp

#include "spdlog/spdlog.h"
#include "DataContext.h"
#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include "dof/DofNumberingSystem.h"
#include "mass/MassSystem.h"
#include "force/InternalForceSystem.h"
#include "load/LoadSystem.h"
#include "explicit/ExplicitSolver.h"
#include "material/mat1/LinearElasticMatrixSystem.h"

/**
 * @brief Run explicit dynamics solver
 * @param data_context The data context containing the mesh and analysis configuration
 */
void run_explicit_solver(DataContext& data_context) {
    spdlog::info("Starting explicit dynamics solver...");
    
    // 1. Initialize material D matrices
    spdlog::info("Computing material D matrices...");
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(data_context.registry);
    
    // 2. Build DOF map (needed for boundary conditions)
    spdlog::info("Building DOF map...");
    DofNumberingSystem::build_dof_map(data_context.registry);
    
    // 3. Compute lumped mass matrix
    spdlog::info("Computing lumped mass matrix...");
    MassSystem::compute_lumped_mass(data_context.registry);
    
    // 4. Initialize initial positions (for displacement calculation)
    spdlog::info("Initializing initial positions...");
    auto node_view = data_context.registry.view<Component::Position>();
    for (auto node_entity : node_view) {
        const auto& pos = data_context.registry.get<Component::Position>(node_entity);
        if (!data_context.registry.all_of<Component::InitialPosition>(node_entity)) {
            data_context.registry.emplace<Component::InitialPosition>(
                node_entity, pos.x, pos.y, pos.z
            );
        }
    }
    
    // 5. Initialize velocity and acceleration to zero
    spdlog::info("Initializing velocity and acceleration...");
    for (auto node_entity : node_view) {
        if (!data_context.registry.all_of<Component::Velocity>(node_entity)) {
            data_context.registry.emplace<Component::Velocity>(node_entity, 0.0, 0.0, 0.0);
        }
        if (!data_context.registry.all_of<Component::Acceleration>(node_entity)) {
            data_context.registry.emplace<Component::Acceleration>(node_entity, 0.0, 0.0, 0.0);
        }
    }
    
    // 6. Time step loop (dt, total_time from analysis entity when present)
    double t = 0.0;
    double dt = 1e-6;
    double total_time = 1e-3;
    if (data_context.analysis_entity != entt::null && data_context.registry.valid(data_context.analysis_entity)) {
        if (data_context.registry.all_of<Component::FixedTimeStep>(data_context.analysis_entity)) {
            dt = data_context.registry.get<Component::FixedTimeStep>(data_context.analysis_entity).value;
        }
        if (data_context.registry.all_of<Component::EndTime>(data_context.analysis_entity)) {
            total_time = data_context.registry.get<Component::EndTime>(data_context.analysis_entity).value;
        }
    }
    spdlog::info("Starting time integration. dt = {:.2e}, total_time = {:.2e}", dt, total_time);
    
    int step_count = 0;
    while (t < total_time) {
        // Reset and compute internal forces (based on current coordinates)
        InternalForceSystem::reset_internal_forces(data_context.registry);
        InternalForceSystem::compute_internal_forces(data_context.registry);
        
        // Reset and apply external loads
        LoadSystem::reset_external_forces(data_context.registry);
        LoadSystem::apply_nodal_loads(data_context.registry, t);
        
        // Time integration
        ExplicitSolver::integrate(data_context.registry, dt);
        
        t += dt;
        step_count++;
        
        // Output progress every 100 steps
        if (step_count % 100 == 0) {
            spdlog::info("Time: {:.6e} s, Step: {}", t, step_count);
        }
    }
    
    spdlog::info("Explicit solver completed. Final time: {:.6e} s, Total steps: {}", t, step_count);
}
