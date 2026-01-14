// MassSystem.cpp
#include "MassSystem.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/property_components.h"
#include "../../data_center/components/material_components.h"
#include <Eigen/Dense>
#include "spdlog/spdlog.h"
#include <cmath>

// Forward declaration of helper functions from C3D8RStiffnessMatrix
// We'll duplicate the volume calculation logic here to avoid circular dependencies
namespace {
    // Helper function: Calculate B-bar component (same as in C3D8RStiffnessMatrix.cpp)
    void calc_b_bar_component(
        const double* y,  // 8 node y coordinates
        const double* z,  // 8 node z coordinates
        double* BiI       // Output: 8 B-bar values
    ) {
        BiI[0] = -(y[1]*(z[2]+z[3]-z[4]-z[5])+y[2]*(-z[1]+z[3])
                 +y[3]*(-z[1]-z[2]+z[4]+z[7])+y[4]*(z[1]-z[3]+z[5]-z[7])
                 +y[5]*(z[1]-z[4])+y[7]*(-z[3]+z[4]))/12.0;
        
        BiI[1] = (y[0]*(z[2]+z[3]-z[4]-z[5])+y[2]*(-z[0]-z[3]+z[5]+z[6])
                 +y[3]*(-z[0]+z[2])+y[4]*(z[0]-z[5])
                 +y[5]*(z[0]-z[2]+z[4]-z[6])+y[6]*(-z[2]+z[5]))/12.0;
        
        BiI[2] = -(y[0]*(z[1]-z[3])+y[1]*(-z[0]-z[3]+z[5]+z[6])
                 +y[3]*(z[0]+z[1]-z[6]-z[7])+y[5]*(-z[1]+z[6])
                 +y[6]*(-z[1]+z[3]-z[5]+z[7])+y[7]*(z[3]-z[6]))/12.0;
        
        BiI[3] = -(y[0]*(z[1]+z[2]-z[4]-z[7])+y[1]*(-z[0]+z[2])
                 +y[2]*(-z[0]-z[1]+z[6]+z[7])+y[4]*(z[0]-z[7])
                 +y[6]*(-z[2]+z[7])+y[7]*(z[0]-z[2]+z[4]-z[6]))/12.0;
        
        BiI[4] = (y[0]*(z[1]-z[3]+z[5]-z[7])+y[1]*(-z[0]+z[5])
                 +y[3]*(z[0]-z[7])+y[5]*(-z[0]-z[1]+z[6]+z[7])
                 +y[6]*(-z[5]+z[7])+y[7]*(z[0]+z[3]-z[5]-z[6]))/12.0;
        
        BiI[5] = (y[0]*(z[1]-z[4])+y[1]*(-z[0]+z[2]-z[4]+z[6])
                 +y[2]*(-z[1]+z[6])+y[4]*(z[0]+z[1]-z[6]-z[7])
                 +y[6]*(-z[1]-z[2]+z[4]+z[7])+y[7]*(z[4]-z[6]))/12.0;
        
        BiI[6] = (y[1]*(z[2]-z[5])+y[2]*(-z[1]+z[3]-z[5]+z[7])
                 +y[3]*(-z[2]+z[7])+y[4]*(z[5]-z[7])
                 +y[5]*(z[1]+z[2]-z[4]-z[7])+y[7]*(-z[2]-z[3]+z[4]+z[5]))/12.0;
        
        BiI[7] = -(y[0]*(z[3]-z[4])+y[2]*(-z[3]+z[6])
                 +y[3]*(-z[0]+z[2]-z[4]+z[6])+y[4]*(z[0]+z[3]-z[5]-z[6])
                 +y[5]*(z[4]-z[6])+y[6]*(-z[2]-z[3]+z[4]+z[5]))/12.0;
    }

    // Helper function: Calculate element volume using B-bar method
    double calc_vol_bbar(const double* BiI, const double* X) {
        double V = 0.0;
        for (int i = 0; i < 8; ++i) {
            V += X[i] * BiI[i];
        }
        return V;
    }
}

void MassSystem::compute_lumped_mass(entt::registry& registry) {
    spdlog::info("Computing lumped mass matrix...");

    // Initialize mass to zero for all nodes
    auto node_view = registry.view<Component::Position>();
    for (auto node_entity : node_view) {
        if (!registry.all_of<Component::Mass>(node_entity)) {
            registry.emplace<Component::Mass>(node_entity, 0.0);
        } else {
            registry.get<Component::Mass>(node_entity).value = 0.0;
        }
    }

    // Traverse all elements
    auto element_view = registry.view<Component::Connectivity, Component::ElementType>();
    size_t element_count = 0;
    
    for (auto element_entity : element_view) {
        const auto& connectivity = registry.get<Component::Connectivity>(element_entity);
        const auto& element_type = registry.get<Component::ElementType>(element_entity);
        
        // Only process C3D8R elements (type_id = 308)
        if (element_type.type_id != 308) {
            continue;
        }
        
        if (connectivity.nodes.size() != 8) {
            spdlog::warn("Element has {} nodes, expected 8 for C3D8R. Skipping.", connectivity.nodes.size());
            continue;
        }

        // Get material density
        if (!registry.all_of<Component::PropertyRef>(element_entity)) {
            spdlog::warn("Element missing PropertyRef. Skipping mass calculation.");
            continue;
        }

        const auto& property_ref = registry.get<Component::PropertyRef>(element_entity);
        entt::entity property_entity = property_ref.property_entity;

        if (!registry.all_of<Component::MaterialRef>(property_entity)) {
            spdlog::warn("Property missing MaterialRef. Skipping mass calculation.");
            continue;
        }

        const auto& material_ref = registry.get<Component::MaterialRef>(property_entity);
        entt::entity material_entity = material_ref.material_entity;

        if (!registry.all_of<Component::LinearElasticParams>(material_entity)) {
            spdlog::warn("Material missing LinearElasticParams. Skipping mass calculation.");
            continue;
        }

        const auto& material_params = registry.get<Component::LinearElasticParams>(material_entity);
        double rho = material_params.rho;

        // Get node coordinates
        Eigen::Matrix<double, 8, 3> coords;
        for (size_t i = 0; i < 8; ++i) {
            if (!registry.all_of<Component::Position>(connectivity.nodes[i])) {
                spdlog::warn("Node missing Position component. Skipping element.");
                continue;
            }
            const auto& pos = registry.get<Component::Position>(connectivity.nodes[i]);
            coords(i, 0) = pos.x;
            coords(i, 1) = pos.y;
            coords(i, 2) = pos.z;
        }

        // Calculate B-bar matrix components
        Eigen::Matrix<double, 8, 3> BiI;
        
        // Extract coordinate components
        double x[8], y[8], z[8];
        for (int i = 0; i < 8; ++i) {
            x[i] = coords(i, 0);
            y[i] = coords(i, 1);
            z[i] = coords(i, 2);
        }

        // Calculate three B-bar components
        calc_b_bar_component(y, z, BiI.data() + 0*8);  // x component
        calc_b_bar_component(z, x, BiI.data() + 1*8);  // y component
        calc_b_bar_component(x, y, BiI.data() + 2*8);  // z component

        // Calculate element volume
        double VOL = calc_vol_bbar(BiI.data() + 0*8, x);
        
        if (std::abs(VOL) < 1.0e-20) {
            spdlog::warn("Element volume is zero or too small. Skipping.");
            continue;
        }

        // Calculate element mass
        double element_mass = rho * VOL;
        
        // Distribute mass uniformly to 8 nodes
        double nodal_mass = element_mass / 8.0;

        // Accumulate mass to nodes
        for (size_t i = 0; i < 8; ++i) {
            entt::entity node_entity = connectivity.nodes[i];
            if (!registry.all_of<Component::Mass>(node_entity)) {
                registry.emplace<Component::Mass>(node_entity, nodal_mass);
            } else {
                registry.get<Component::Mass>(node_entity).value += nodal_mass;
            }
        }

        element_count++;
    }

    spdlog::info("Lumped mass computed for {} elements.", element_count);
}
