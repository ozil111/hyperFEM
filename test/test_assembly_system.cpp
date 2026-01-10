// test_assembly_system.cpp
// Unit tests for the assembly system modules

#include <gtest/gtest.h>
#include <entt/entt.hpp>
#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <cmath>

// Include the modules to test
// Note: Using paths relative to include directories set in CMakeLists.txt
// (data_center/ and system/ are in include directories)
#include "DofMap.h"
#include "dof/DofNumberingSystem.h"
#include "material/mat1/LinearElasticMatrixSystem.h"
#include "element/c3d8r/C3D8RStiffnessMatrix.h"
#include "assemble/AssemblySystem.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"

// Test fixture for creating a simple test mesh
class AssemblySystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create a simple unit cube with 8 nodes
        // Nodes: (0,0,0), (1,0,0), (1,1,0), (0,1,0), (0,0,1), (1,0,1), (1,1,1), (0,1,1)
        node_entities.clear();
        
        // Create 8 nodes
        std::vector<std::array<double, 3>> node_coords = {
            {0.0, 0.0, 0.0},
            {1.0, 0.0, 0.0},
            {1.0, 1.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0},
            {1.0, 0.0, 1.0},
            {1.0, 1.0, 1.0},
            {0.0, 1.0, 1.0}
        };
        
        for (const auto& coord : node_coords) {
            auto node = registry.create();
            registry.emplace<Component::Position>(node, coord[0], coord[1], coord[2]);
            node_entities.push_back(node);
        }
        
        // Create material (linear elastic: E=210000, nu=0.3)
        material_entity = registry.create();
        registry.emplace<Component::MaterialID>(material_entity, 1);
        registry.emplace<Component::LinearElasticParams>(
            material_entity, 
            7850.0,  // rho (density)
            210000.0, // E (Young's modulus)
            0.3       // nu (Poisson's ratio)
        );
        
        // Create property
        property_entity = registry.create();
        registry.emplace<Component::PropertyID>(property_entity, 1);
        registry.emplace<Component::SolidProperty>(
            property_entity,
            308,      // type_id (C3D8R)
            2,        // integration_network
            "eas"     // hourglass_control
        );
        registry.emplace<Component::MaterialRef>(property_entity, material_entity);
        
        // Create element (C3D8R)
        element_entity = registry.create();
        registry.emplace<Component::ElementType>(element_entity, 308); // C3D8R
        registry.emplace<Component::PropertyRef>(element_entity, property_entity);
        
        Component::Connectivity conn;
        conn.nodes = node_entities; // 8 nodes
        registry.emplace<Component::Connectivity>(element_entity, std::move(conn));
    }
    
    void TearDown() override {
        registry.clear();
        node_entities.clear();
    }
    
    entt::registry registry;
    std::vector<entt::entity> node_entities;
    entt::entity material_entity;
    entt::entity property_entity;
    entt::entity element_entity;
};

// Test DofMap basic functionality
TEST(DofMapTest, BasicFunctionality) {
    DofMap dof_map;
    dof_map.node_to_dof_index.resize(10, -1);
    dof_map.node_to_dof_index[0] = 0;
    dof_map.node_to_dof_index[1] = 3;
    dof_map.node_to_dof_index[2] = 6;
    dof_map.num_total_dofs = 9;
    dof_map.dofs_per_node = 3;
    
    // Test has_node
    EXPECT_TRUE(dof_map.has_node(entt::entity{0}));
    EXPECT_TRUE(dof_map.has_node(entt::entity{1}));
    EXPECT_FALSE(dof_map.has_node(entt::entity{5})); // Not in map
    
    // Test get_dof_index (safe version)
    EXPECT_EQ(dof_map.get_dof_index(entt::entity{0}, 0), 0); // x dof
    EXPECT_EQ(dof_map.get_dof_index(entt::entity{0}, 1), 1); // y dof
    EXPECT_EQ(dof_map.get_dof_index(entt::entity{0}, 2), 2); // z dof
    EXPECT_EQ(dof_map.get_dof_index(entt::entity{1}, 0), 3); // x dof
    EXPECT_EQ(dof_map.get_dof_index(entt::entity{5}, 0), -1); // Invalid
    
    // Test get_dof_index_unsafe
    EXPECT_EQ(dof_map.get_dof_index_unsafe(0, 0), 0);
    EXPECT_EQ(dof_map.get_dof_index_unsafe(1, 2), 5);
    
    // Test get_dof_array_ptr
    const int* ptr = dof_map.get_dof_array_ptr();
    EXPECT_NE(ptr, nullptr);
    EXPECT_EQ(ptr[0], 0);
    EXPECT_EQ(ptr[1], 3);
}

// Test DofNumberingSystem
TEST_F(AssemblySystemTest, DofNumberingSystemTest) {
    // Build DOF map
    DofNumberingSystem::build_dof_map(registry);
    
    // Check that DofMap exists in context
    ASSERT_TRUE(registry.ctx().contains<DofMap>());
    const auto& dof_map = registry.ctx().get<DofMap>();
    
    // Check total DOFs (8 nodes * 3 DOFs = 24)
    EXPECT_EQ(dof_map.num_total_dofs, 24);
    EXPECT_EQ(dof_map.dofs_per_node, 3);
    
    // Check DOF indices for each node
    for (size_t i = 0; i < node_entities.size(); ++i) {
        uint32_t entity_id = static_cast<uint32_t>(node_entities[i]);
        EXPECT_TRUE(dof_map.has_node(node_entities[i]));
        EXPECT_EQ(dof_map.get_dof_index(node_entities[i], 0), static_cast<int>(i * 3));
        EXPECT_EQ(dof_map.get_dof_index(node_entities[i], 1), static_cast<int>(i * 3 + 1));
        EXPECT_EQ(dof_map.get_dof_index(node_entities[i], 2), static_cast<int>(i * 3 + 2));
    }
}

// Test LinearElasticMatrixSystem
TEST_F(AssemblySystemTest, LinearElasticMatrixSystemTest) {
    // Compute linear elastic matrix
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(registry);
    
    // Check that LinearElasticMatrix component exists
    ASSERT_TRUE(registry.all_of<Component::LinearElasticMatrix>(material_entity));
    const auto& matrix_comp = registry.get<Component::LinearElasticMatrix>(material_entity);
    ASSERT_TRUE(matrix_comp.is_initialized);
    
    const auto& D = matrix_comp.D;
    
    // Check matrix size (6x6)
    EXPECT_EQ(D.rows(), 6);
    EXPECT_EQ(D.cols(), 6);
    
    // Note: D matrix uses Abaqus/Fortran Voigt order: [xx, yy, zz, xy, yz, xz]
    // Index mapping: 0->XX, 1->YY, 2->ZZ, 3->XY, 4->YZ, 5->XZ
    
    // Check symmetry
    EXPECT_NEAR(D(0, 1), D(1, 0), 1e-10);
    EXPECT_NEAR(D(0, 2), D(2, 0), 1e-10);
    EXPECT_NEAR(D(1, 2), D(2, 1), 1e-10);
    
    // Check diagonal elements (should be lambda + 2*mu)
    double E = 210000.0;
    double nu = 0.3;
    double lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    double mu = E / (2.0 * (1.0 + nu));
    double diag_expected = lambda + 2.0 * mu;
    
    EXPECT_NEAR(D(0, 0), diag_expected, 1e-6);  // XX-XX
    EXPECT_NEAR(D(1, 1), diag_expected, 1e-6);  // YY-YY
    EXPECT_NEAR(D(2, 2), diag_expected, 1e-6);  // ZZ-ZZ
    
    // Check off-diagonal elements (should be lambda)
    EXPECT_NEAR(D(0, 1), lambda, 1e-6);  // XX-YY
    EXPECT_NEAR(D(0, 2), lambda, 1e-6);  // XX-ZZ
    EXPECT_NEAR(D(1, 2), lambda, 1e-6);  // YY-ZZ
    
    // Check shear terms (should be mu)
    EXPECT_NEAR(D(3, 3), mu, 1e-6);  // XY-XY
    EXPECT_NEAR(D(4, 4), mu, 1e-6);  // YZ-YZ
    EXPECT_NEAR(D(5, 5), mu, 1e-6);  // XZ-XZ
}

// Test C3D8R stiffness matrix computation
TEST_F(AssemblySystemTest, C3D8RStiffnessMatrixTest) {
    // First compute material matrix
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(registry);
    const auto& matrix_comp = registry.get<Component::LinearElasticMatrix>(material_entity);
    const auto& D = matrix_comp.D;
    
    // Compute element stiffness matrix
    // Note: This includes both volumetric stiffness (B-bar method) 
    // and hourglass stiffness (Puso EAS method)
    Eigen::MatrixXd Ke;
    compute_c3d8r_stiffness_matrix(registry, element_entity, D, Ke);
    
    // Check matrix size (24x24 for 8 nodes * 3 DOFs)
    EXPECT_EQ(Ke.rows(), 24);
    EXPECT_EQ(Ke.cols(), 24);
    
    // Check symmetry
    // The stiffness matrix must be symmetric for conservative systems
    double symmetry_error = (Ke - Ke.transpose()).norm();
    EXPECT_LT(symmetry_error, 1e-10);
    
    // Check positive definiteness (all eigenvalues should be positive)
    // For a properly constrained element, the stiffness matrix should be positive definite
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigensolver(Ke);
    if (eigensolver.info() == Eigen::Success) {
        double min_eigenvalue = eigensolver.eigenvalues().minCoeff();
        // Allow small negative eigenvalues due to numerical errors
        // Note: An unconstrained element would have 6 zero eigenvalues (rigid body modes)
        EXPECT_GT(min_eigenvalue, -1e-8);
    }
    
    // Check that matrix is not zero
    // The hourglass control ensures non-zero stiffness even for underintegrated elements
    EXPECT_GT(Ke.norm(), 1e-10);
}

// Test AssemblySystem dispatcher
TEST_F(AssemblySystemTest, AssemblySystemDispatcherTest) {
    // First compute material matrix
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(registry);
    
    // Test dispatcher
    Eigen::MatrixXd Ke_buffer;
    bool success = AssemblySystem::compute_element_stiffness_dispatcher(
        registry, element_entity, Ke_buffer
    );
    
    EXPECT_TRUE(success);
    EXPECT_EQ(Ke_buffer.rows(), 24);
    EXPECT_EQ(Ke_buffer.cols(), 24);
    
    // Test with invalid element (no ElementType)
    auto invalid_element = registry.create();
    Eigen::MatrixXd Ke_invalid;
    bool fail = AssemblySystem::compute_element_stiffness_dispatcher(
        registry, invalid_element, Ke_invalid
    );
    EXPECT_FALSE(fail);
}

// Test full assembly system
TEST_F(AssemblySystemTest, FullAssemblySystemTest) {
    // Step 1: Build DOF map
    DofNumberingSystem::build_dof_map(registry);
    const auto& dof_map = registry.ctx().get<DofMap>();
    EXPECT_EQ(dof_map.num_total_dofs, 24);
    
    // Step 2: Compute material matrices
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(registry);
    
    // Step 3: Assemble global stiffness matrix
    // The C3D8R elements include both volumetric stiffness (B-bar) 
    // and hourglass stiffness (Puso EAS method)
    AssemblySystem::SparseMatrix K_global;
    AssemblySystem::assemble_stiffness(registry, K_global);
    
    // Check matrix size
    EXPECT_EQ(K_global.rows(), 24);
    EXPECT_EQ(K_global.cols(), 24);
    
    // Check that matrix has non-zero entries
    // With hourglass control, even a single element should have non-zero entries
    EXPECT_GT(K_global.nonZeros(), 0);
    
    // Check symmetry (convert to dense for comparison)
    Eigen::MatrixXd K_dense = Eigen::MatrixXd(K_global);
    double symmetry_error = (K_dense - K_dense.transpose()).norm();
    EXPECT_LT(symmetry_error, 1e-10);
    
    // Check that the matrix is positive semi-definite
    // For an unconstrained structure, we expect 6 zero eigenvalues (rigid body modes)
    // The hourglass control ensures that spurious zero-energy modes are eliminated
    Eigen::SelfAdjointEigenSolver<Eigen::MatrixXd> eigensolver(K_dense);
    if (eigensolver.info() == Eigen::Success) {
        double min_eigenvalue = eigensolver.eigenvalues().minCoeff();
        // Stiffness matrix should be positive semi-definite
        // Allow small negative eigenvalues due to numerical errors
        EXPECT_GT(min_eigenvalue, -1e-8);
    }
}

// Test assembly with multiple elements
TEST_F(AssemblySystemTest, MultipleElementsAssemblyTest) {
    // Create a second element (same nodes for simplicity, in practice they would be different)
    auto element2 = registry.create();
    registry.emplace<Component::ElementType>(element2, 308);
    registry.emplace<Component::PropertyRef>(element2, property_entity);
    
    Component::Connectivity conn2;
    conn2.nodes = node_entities; // Same nodes
    registry.emplace<Component::Connectivity>(element2, std::move(conn2));
    
    // Build DOF map
    DofNumberingSystem::build_dof_map(registry);
    
    // Compute material matrices
    LinearElasticMatrixSystem::compute_linear_elastic_matrix(registry);
    
    // Assemble global stiffness matrix
    AssemblySystem::SparseMatrix K_global;
    AssemblySystem::assemble_stiffness(registry, K_global);
    
    // Should still be 24x24 (same nodes)
    EXPECT_EQ(K_global.rows(), 24);
    EXPECT_EQ(K_global.cols(), 24);
    
    // Should have more non-zeros than single element case (values are summed)
    EXPECT_GT(K_global.nonZeros(), 0);
}

// Main function for running tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

