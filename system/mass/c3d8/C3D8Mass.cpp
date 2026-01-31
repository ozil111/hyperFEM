// C3D8Mass.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "C3D8Mass.h"
#include "../../../data_center/components/mesh_components.h"
#include "../../../data_center/components/property_components.h"
#include "../../../data_center/components/material_components.h"
#include "../gauss/GaussIntegration.h"
#include <Eigen/Dense>
#include "spdlog/spdlog.h"
#include <cmath>

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

    // Helper function: Shape function for 8-node hexahedron at natural coordinates (xi, eta, zeta)
    void shape_function_8node(double xi, double eta, double zeta, double N[8]) {
        N[0] = 0.125 * (1.0 - xi) * (1.0 - eta) * (1.0 - zeta);
        N[1] = 0.125 * (1.0 + xi) * (1.0 - eta) * (1.0 - zeta);
        N[2] = 0.125 * (1.0 + xi) * (1.0 + eta) * (1.0 - zeta);
        N[3] = 0.125 * (1.0 - xi) * (1.0 + eta) * (1.0 - zeta);
        N[4] = 0.125 * (1.0 - xi) * (1.0 - eta) * (1.0 + zeta);
        N[5] = 0.125 * (1.0 + xi) * (1.0 - eta) * (1.0 + zeta);
        N[6] = 0.125 * (1.0 + xi) * (1.0 + eta) * (1.0 + zeta);
        N[7] = 0.125 * (1.0 - xi) * (1.0 + eta) * (1.0 + zeta);
    }

    // Helper function: Shape function derivatives for 8-node hexahedron
    void shape_function_derivatives_8node(double xi, double eta, double zeta, 
                                          Eigen::Matrix<double, 8, 3>& dN) {
        // dN/dxi
        dN(0, 0) = -0.125 * (1.0 - eta) * (1.0 - zeta);
        dN(1, 0) =  0.125 * (1.0 - eta) * (1.0 - zeta);
        dN(2, 0) =  0.125 * (1.0 + eta) * (1.0 - zeta);
        dN(3, 0) = -0.125 * (1.0 + eta) * (1.0 - zeta);
        dN(4, 0) = -0.125 * (1.0 - eta) * (1.0 + zeta);
        dN(5, 0) =  0.125 * (1.0 - eta) * (1.0 + zeta);
        dN(6, 0) =  0.125 * (1.0 + eta) * (1.0 + zeta);
        dN(7, 0) = -0.125 * (1.0 + eta) * (1.0 + zeta);
        
        // dN/deta
        dN(0, 1) = -0.125 * (1.0 - xi) * (1.0 - zeta);
        dN(1, 1) = -0.125 * (1.0 + xi) * (1.0 - zeta);
        dN(2, 1) =  0.125 * (1.0 + xi) * (1.0 - zeta);
        dN(3, 1) =  0.125 * (1.0 - xi) * (1.0 - zeta);
        dN(4, 1) = -0.125 * (1.0 - xi) * (1.0 + zeta);
        dN(5, 1) = -0.125 * (1.0 + xi) * (1.0 + zeta);
        dN(6, 1) =  0.125 * (1.0 + xi) * (1.0 + zeta);
        dN(7, 1) =  0.125 * (1.0 - xi) * (1.0 + zeta);
        
        // dN/dzeta
        dN(0, 2) = -0.125 * (1.0 - xi) * (1.0 - eta);
        dN(1, 2) = -0.125 * (1.0 + xi) * (1.0 - eta);
        dN(2, 2) = -0.125 * (1.0 + xi) * (1.0 + eta);
        dN(3, 2) = -0.125 * (1.0 - xi) * (1.0 + eta);
        dN(4, 2) =  0.125 * (1.0 - xi) * (1.0 - eta);
        dN(5, 2) =  0.125 * (1.0 + xi) * (1.0 - eta);
        dN(6, 2) =  0.125 * (1.0 + xi) * (1.0 + eta);
        dN(7, 2) =  0.125 * (1.0 - xi) * (1.0 + eta);
    }

    // Helper function: Compute Jacobian matrix at a point
    double compute_jacobian_det(const Eigen::Matrix<double, 8, 3>& coords,
                                double xi, double eta, double zeta) {
        Eigen::Matrix<double, 8, 3> dN;
        shape_function_derivatives_8node(xi, eta, zeta, dN);
        
        // J = dN^T * coords (3x8 * 8x3 = 3x3)
        Eigen::Matrix3d J = dN.transpose() * coords;
        
        return J.determinant();
    }
}

bool compute_c3d8_mass(entt::registry& registry, entt::entity element_entity, int n_integration_points) {
    const auto& connectivity = registry.get<Component::Connectivity>(element_entity);
    
    if (connectivity.nodes.size() != 8) {
        spdlog::warn("Element has {} nodes, expected 8 for C3D8. Skipping.", connectivity.nodes.size());
        return false;
    }

    // Get material density
    if (!registry.all_of<Component::PropertyRef>(element_entity)) {
        spdlog::warn("Element missing PropertyRef. Skipping mass calculation.");
        return false;
    }

    const auto& property_ref = registry.get<Component::PropertyRef>(element_entity);
    entt::entity property_entity = property_ref.property_entity;

    if (!registry.all_of<Component::MaterialRef>(property_entity)) {
        spdlog::warn("Property missing MaterialRef. Skipping mass calculation.");
        return false;
    }

    const auto& material_ref = registry.get<Component::MaterialRef>(property_entity);
    entt::entity material_entity = material_ref.material_entity;

    if (!registry.all_of<Component::LinearElasticParams>(material_entity)) {
        spdlog::warn("Material missing LinearElasticParams. Skipping mass calculation.");
        return false;
    }

    const auto& material_params = registry.get<Component::LinearElasticParams>(material_entity);
    double rho = material_params.rho;

    // Get node coordinates
    Eigen::Matrix<double, 8, 3> coords;
    for (size_t i = 0; i < 8; ++i) {
        if (!registry.all_of<Component::Position>(connectivity.nodes[i])) {
            spdlog::warn("Node missing Position component. Skipping element.");
            return false;
        }
        const auto& pos = registry.get<Component::Position>(connectivity.nodes[i]);
        coords(i, 0) = pos.x;
        coords(i, 1) = pos.y;
        coords(i, 2) = pos.z;
    }

    double VOL = 0.0;

    if (n_integration_points == 1) {
        // Use B-bar method (reduced integration)
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
        VOL = calc_vol_bbar(BiI.data() + 0*8, x);
    } else {
        // TODO: Implement Gaussian integration for n_integration_points > 1
        // This requires:
        // 1. Get Gauss points and weights using GaussIntegration::get_3d_hex_gauss_points()
        // 2. For each Gauss point:
        //    - Compute shape functions N[8]
        //    - Compute Jacobian determinant det(J)
        //    - Accumulate: VOL += weight * det(J)
        // 3. Distribute mass to nodes based on shape functions
        spdlog::warn("Gaussian integration with {} points per dimension is not yet implemented. Using B-bar method instead.", n_integration_points);
        
        // Fallback to B-bar method
        Eigen::Matrix<double, 8, 3> BiI;
        double x[8], y[8], z[8];
        for (int i = 0; i < 8; ++i) {
            x[i] = coords(i, 0);
            y[i] = coords(i, 1);
            z[i] = coords(i, 2);
        }
        calc_b_bar_component(y, z, BiI.data() + 0*8);
        calc_b_bar_component(z, x, BiI.data() + 1*8);
        calc_b_bar_component(x, y, BiI.data() + 2*8);
        VOL = calc_vol_bbar(BiI.data() + 0*8, x);
    }
    
    if (std::abs(VOL) < 1.0e-20) {
        spdlog::warn("Element volume is zero or too small. Skipping.");
        return false;
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

    return true;
}
