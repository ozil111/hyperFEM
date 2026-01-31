// AssemblySystem.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "AssemblySystem.h"
#include "../element/c3d8r/C3D8RStiffnessMatrix.h"
#include "../../data_center/DofMap.h"
#include "../../data_center/components/mesh_components.h"
#include "../../data_center/components/property_components.h"
#include "../../data_center/components/material_components.h"
#include "spdlog/spdlog.h"

// -------------------------------------------------------------------
// **Dispatcher：根据单元类型分发到相应的刚度矩阵计算函数（高性能版本）**
// -------------------------------------------------------------------
bool AssemblySystem::compute_element_stiffness_dispatcher(
    entt::registry& registry,
    entt::entity element_entity,
    Eigen::MatrixXd& Ke_buffer
) {
    // A. 获取单元类型
    if (!registry.all_of<Component::ElementType>(element_entity)) {
        spdlog::error("Element entity missing ElementType component");
        return false;
    }
    
    int type_id = registry.get<Component::ElementType>(element_entity).type_id;

    // B. 准备数据：获取 D 矩阵（避免在 Kernel 中重复查找）
    // -----------------------------------------------------
    if (!registry.all_of<Component::PropertyRef>(element_entity)) {
        spdlog::error("Element entity missing PropertyRef component");
        return false;
    }
    
    auto prop_entity = registry.get<Component::PropertyRef>(element_entity).property_entity;
    
    if (!registry.all_of<Component::MaterialRef>(prop_entity)) {
        spdlog::error("Property entity missing MaterialRef component");
        return false;
    }
    
    auto mat_entity = registry.get<Component::MaterialRef>(prop_entity).material_entity;
    
    if (!registry.all_of<Component::LinearElasticMatrix>(mat_entity)) {
        spdlog::error("Material entity missing LinearElasticMatrix component. "
                     "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
        return false;
    }
    
    const auto& material_matrix = registry.get<Component::LinearElasticMatrix>(mat_entity);
    if (!material_matrix.is_initialized) {
        spdlog::error("Material D matrix not initialized. "
                     "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
        return false;
    }
    
    const Eigen::Matrix<double, 6, 6>& D = material_matrix.D;

    // C. switch-case 分发（传入 D 矩阵，避免重复查找）
    // -----------------------------------------------------
    switch (type_id) {
        case 308: {  // Hexa8 (C3D8R)
            try {
                // 调用高性能版本，直接传入 D 矩阵，输出到缓冲区
                compute_c3d8r_stiffness_matrix(registry, element_entity, D, Ke_buffer);
                return true;
            } catch (const std::exception& e) {
                spdlog::error("Error computing C3D8R stiffness matrix: {}", e.what());
                return false;
            }
        }
        
        // 未来可以添加其他单元类型：
        // case 304: {  // Tetra4
        //     compute_tet4_stiffness_matrix(registry, element_entity, D, Ke_buffer);
        //     return true;
        // }
        
        default:
            spdlog::warn("Unknown element type {} for stiffness calculation", type_id);
            return false;
    }
}

// -------------------------------------------------------------------
// **Assembly Loop：统一的组装循环**
// -------------------------------------------------------------------
void AssemblySystem::assemble_stiffness(
    entt::registry& registry,
    SparseMatrix& K_global
) {
    spdlog::info("AssemblySystem: Starting stiffness matrix assembly...");
    
    // 1. 从 Context 获取现成的 DofMap（必须先运行 DofNumberingSystem）
    if (!registry.ctx().contains<DofMap>()) {
        spdlog::error("DofMap not found in Context! Please run DofNumberingSystem::build_dof_map() first.");
        K_global.resize(0, 0);
        return;
    }
    
    const auto& dof_map = registry.ctx().get<DofMap>();
    
    if (dof_map.num_total_dofs == 0) {
        spdlog::warn("AssemblySystem: DofMap has zero total DOFs");
        K_global.resize(0, 0);
        return;
    }
    
    spdlog::info("AssemblySystem: Using DofMap with {} total DOFs", dof_map.num_total_dofs);
    
    // 2. 准备 Triplet 列表（用于高效构建稀疏矩阵）
    // 优化：对于 C3D8R 单元，每个节点连接约 8 个单元（平均），
    // 每个节点 3 个自由度，刚度矩阵的一行大概有 3 * 27 ≈ 81 个非零元
    // 使用 * 60 比较安全（略小于理论上限，避免过度预留）
    std::vector<Triplet> triplets;
    triplets.reserve(dof_map.num_total_dofs * 60);
    
    // 3. 预分配单元刚度矩阵缓冲区（避免循环内堆分配）
    // 最大可能是 60x60（20 节点六面体），预留足够空间
    Eigen::MatrixXd Ke_buffer;
    
    // 4. 获取 DOF 映射数组的指针（直接数组访问，极快）
    const int* dof_array_ptr = dof_map.get_dof_array_ptr();
    
    // 5. 遍历所有单元（不管类型，统一遍历）
    auto view = registry.view<Component::Connectivity, Component::ElementType>();
    
    size_t element_count = 0;
    size_t skipped_count = 0;
    
    for (auto entity : view) {
        element_count++;
        
        // --- Step 1: 调用 Dispatcher 计算单元刚度矩阵到缓冲区 ---
        if (!compute_element_stiffness_dispatcher(registry, entity, Ke_buffer)) {
            skipped_count++;
            continue;  // 跳过不支持的单元
        }
        
        // --- Step 2: 统一组装逻辑 ---
        const auto& conn = view.get<Component::Connectivity>(entity);
        int num_nodes = static_cast<int>(conn.nodes.size());
        int num_dofs_per_node = 3;  // 假设全是 3D 实体单元
        int element_dofs = num_nodes * num_dofs_per_node;
        
        // 验证单元刚度矩阵大小
        if (Ke_buffer.rows() != element_dofs || Ke_buffer.cols() != element_dofs) {
            spdlog::warn("Element stiffness matrix size mismatch: expected {}x{}, got {}x{}",
                        element_dofs, element_dofs, Ke_buffer.rows(), Ke_buffer.cols());
            skipped_count++;
            continue;
        }
        
        // 双层循环填坑：遍历单元刚度矩阵的每个元素
        // 【性能优化】使用直接数组访问，跳过边界检查
        for (int i = 0; i < element_dofs; ++i) {
            // 计算局部节点和自由度索引
            int local_node_i = i / num_dofs_per_node;
            int local_dof_i = i % num_dofs_per_node;
            
            // 【关键优化】直接数组访问，O(1) 复杂度，无函数调用和边界检查开销
            uint32_t entity_id_i = static_cast<uint32_t>(conn.nodes[local_node_i]);
            int global_row = dof_array_ptr[entity_id_i] + local_dof_i;
            
            for (int j = 0; j < element_dofs; ++j) {
                int local_node_j = j / num_dofs_per_node;
                int local_dof_j = j % num_dofs_per_node;
                
                uint32_t entity_id_j = static_cast<uint32_t>(conn.nodes[local_node_j]);
                int global_col = dof_array_ptr[entity_id_j] + local_dof_j;
                
                // 只添加非零元素（过滤极小的数值误差）
                double value = Ke_buffer(i, j);
                if (std::abs(value) > 1.0e-15) {
                    triplets.emplace_back(global_row, global_col, value);
                }
            }
        }
    }
    
    spdlog::info("AssemblySystem: Processed {} elements, skipped {}", element_count, skipped_count);
    spdlog::info("AssemblySystem: Collected {} triplets", triplets.size());
    
    // 5. 构建全局稀疏矩阵
    K_global.resize(dof_map.num_total_dofs, dof_map.num_total_dofs);
    K_global.setFromTriplets(triplets.begin(), triplets.end());
    K_global.makeCompressed();  // 压缩存储格式，提高后续运算效率
    
    spdlog::info("AssemblySystem: Global stiffness matrix assembled: {}x{} with {} non-zeros",
                K_global.rows(), K_global.cols(), K_global.nonZeros());
}

