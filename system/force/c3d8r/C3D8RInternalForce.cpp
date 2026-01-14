// C3D8RInternalForce.cpp
#include "C3D8RInternalForce.h"
#include "../../../data_center/components/mesh_components.h"
#include "../../../data_center/components/property_components.h"
#include "../../../data_center/components/material_components.h"
#include <Eigen/Dense>
#include "spdlog/spdlog.h"
#include <cmath>

namespace {
    // Helper function: Calculate B-bar component (same as in C3D8RStiffnessMatrix.cpp)
    void calc_b_bar_component(const double* y, const double* z, double* BiI) {
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

    double calc_vol_bbar(const double* BiI, const double* X) {
        double V = 0.0;
        for (int i = 0; i < 8; ++i) {
            V += X[i] * BiI[i];
        }
        return V;
    }

    Eigen::Matrix<double, 6, 24> form_b_matrix(const Eigen::Matrix<double, 8, 3>& BiI) {
        Eigen::Matrix<double, 6, 24> B = Eigen::Matrix<double, 6, 24>::Zero();

        for (int K = 0; K < 8; ++K) {
            B(0, 3*K + 0) = BiI(K, 0);  // XX
            B(1, 3*K + 1) = BiI(K, 1);  // YY
            B(2, 3*K + 2) = BiI(K, 2);  // ZZ

            // XY
            B(3, 3*K + 0) = BiI(K, 1);
            B(3, 3*K + 1) = BiI(K, 0);

            // YZ
            B(4, 3*K + 1) = BiI(K, 2);
            B(4, 3*K + 2) = BiI(K, 1);

            // XZ
            B(5, 3*K + 0) = BiI(K, 2);
            B(5, 3*K + 2) = BiI(K, 0);
        }

        return B;
    }
}

bool compute_c3d8r_internal_forces(entt::registry& registry, entt::entity element_entity) {
    if (!registry.all_of<Component::Connectivity, Component::ElementType>(element_entity)) {
        return false;
    }

    const auto& connectivity = registry.get<Component::Connectivity>(element_entity);
    if (connectivity.nodes.size() != 8) {
        return false;
    }

    // Get material D matrix
    if (!registry.all_of<Component::PropertyRef>(element_entity)) {
        return false;
    }

    const auto& property_ref = registry.get<Component::PropertyRef>(element_entity);
    entt::entity property_entity = property_ref.property_entity;

    if (!registry.all_of<Component::MaterialRef>(property_entity)) {
        return false;
    }

    const auto& material_ref = registry.get<Component::MaterialRef>(property_entity);
    entt::entity material_entity = material_ref.material_entity;

    if (!registry.all_of<Component::LinearElasticMatrix>(material_entity)) {
        return false;
    }

    const auto& material_matrix = registry.get<Component::LinearElasticMatrix>(material_entity);
    if (!material_matrix.is_initialized) {
        return false;
    }

    const Eigen::Matrix<double, 6, 6>& D = material_matrix.D;

    // Get current and initial node coordinates
    Eigen::Matrix<double, 8, 3> coords_current;
    Eigen::Matrix<double, 8, 3> coords_initial;

    for (size_t i = 0; i < 8; ++i) {
        entt::entity node_entity = connectivity.nodes[i];

        if (!registry.all_of<Component::Position>(node_entity)) {
            return false;
        }

        const auto& pos = registry.get<Component::Position>(node_entity);
        coords_current(i, 0) = pos.x;
        coords_current(i, 1) = pos.y;
        coords_current(i, 2) = pos.z;

        if (registry.all_of<Component::InitialPosition>(node_entity)) {
            const auto& pos0 = registry.get<Component::InitialPosition>(node_entity);
            coords_initial(i, 0) = pos0.x0;
            coords_initial(i, 1) = pos0.y0;
            coords_initial(i, 2) = pos0.z0;
        } else {
            coords_initial(i, 0) = pos.x;
            coords_initial(i, 1) = pos.y;
            coords_initial(i, 2) = pos.z;
        }
    }

    // Displacement vector (current - initial)
    Eigen::Matrix<double, 24, 1> u_e;
    for (int i = 0; i < 8; ++i) {
        u_e(3*i + 0) = coords_current(i, 0) - coords_initial(i, 0);
        u_e(3*i + 1) = coords_current(i, 1) - coords_initial(i, 1);
        u_e(3*i + 2) = coords_current(i, 2) - coords_initial(i, 2);
    }

    // B-bar matrix using current coordinates
    Eigen::Matrix<double, 8, 3> BiI;
    double x[8], y[8], z[8];
    for (int i = 0; i < 8; ++i) {
        x[i] = coords_current(i, 0);
        y[i] = coords_current(i, 1);
        z[i] = coords_current(i, 2);
    }

    calc_b_bar_component(y, z, BiI.data() + 0*8);  // x component
    calc_b_bar_component(z, x, BiI.data() + 1*8);  // y component
    calc_b_bar_component(x, y, BiI.data() + 2*8);  // z component

    // Element volume
    double VOL = calc_vol_bbar(BiI.data() + 0*8, x);
    if (std::abs(VOL) < 1.0e-20) {
        return false;
    }

    // Normalize B-bar matrix
    BiI /= VOL;

    // B matrix (6x24)
    Eigen::Matrix<double, 6, 24> B = form_b_matrix(BiI);

    // strain and stress
    Eigen::Matrix<double, 6, 1> strain = B * u_e;
    Eigen::Matrix<double, 6, 1> stress = D * strain;

    // element internal force: f_int = B^T * sigma * V
    Eigen::Matrix<double, 24, 1> f_element = B.transpose() * stress * VOL;

    // Scatter to nodes
    for (size_t i = 0; i < 8; ++i) {
        entt::entity node_entity = connectivity.nodes[i];
        if (!registry.all_of<Component::InternalForce>(node_entity)) {
            registry.emplace<Component::InternalForce>(node_entity, 0.0, 0.0, 0.0);
        }

        auto& internal_force = registry.get<Component::InternalForce>(node_entity);
        internal_force.fx += f_element(3*i + 0);
        internal_force.fy += f_element(3*i + 1);
        internal_force.fz += f_element(3*i + 2);
    }

    return true;
}

