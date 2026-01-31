// DofNumberingSystem.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "DofNumberingSystem.h"
#include "../../data_center/components/mesh_components.h"
#include "spdlog/spdlog.h"

// -------------------------------------------------------------------
// **DOF 编号系统：构建节点到全局自由度的映射**
// -------------------------------------------------------------------
void DofNumberingSystem::build_dof_map(entt::registry& registry) {
    spdlog::info("DofNumberingSystem: Building DOF map...");
    
    // 1. 获取或创建 Context 中的 DofMap
    DofMap* dof_map_ptr = nullptr;
    if (registry.ctx().contains<DofMap>()) {
        dof_map_ptr = &registry.ctx().get<DofMap>();
        // 如果已存在，清空并重建
        dof_map_ptr->node_to_dof_index.clear();
        dof_map_ptr->num_total_dofs = 0;
    } else {
        // 创建新的 DofMap 并存储到 Context
        dof_map_ptr = &registry.ctx().emplace<DofMap>();
    }
    auto& dof_map = *dof_map_ptr;
    
    // 2. 确定数组大小
    // EnTT 的 entity ID 可能不是从 0 开始连续，我们需要足够大的空间
    // 先遍历一次找出最大 entity ID
    size_t estimated_size = 1000;  // 初始大小
    
    // 如果映射已存在且大小足够，可以选择保留或重建
    // 这里选择重建以确保一致性
    dof_map.node_to_dof_index.assign(estimated_size, -1);
    dof_map.num_total_dofs = 0;
    dof_map.dofs_per_node = 3;  // 默认 3D 实体单元，每个节点 3 个自由度
    
    // 3. 遍历所有节点并分配 DOF 编号
    auto view = registry.view<Component::Position>();  // 所有几何节点
    int current_dof = 0;
    size_t node_count = 0;
    
    // 为了正确处理 entity ID，我们需要确保 vector 足够大
    // 遍历两次：第一次找出最大 entity ID，第二次分配 DOF
    uint32_t max_entity_id = 0;
    for (auto entity : view) {
        uint32_t entity_id = static_cast<uint32_t>(entity);
        if (entity_id > max_entity_id) {
            max_entity_id = entity_id;
        }
    }
    
    // 确保 vector 足够大（添加一些余量）
    if (max_entity_id + 1 > dof_map.node_to_dof_index.size()) {
        dof_map.node_to_dof_index.resize(max_entity_id + 1000, -1);
    }
    
    // 分配 DOF 编号
    for (auto entity : view) {
        uint32_t entity_id = static_cast<uint32_t>(entity);
        
        // 存储映射关系：该节点的起始全局自由度编号
        dof_map.node_to_dof_index[entity_id] = current_dof;
        
        // 每个节点分配 3 个自由度（x, y, z）
        // 未来可以根据节点类型判断（比如梁节点有 6 个自由度）
        current_dof += dof_map.dofs_per_node;
        node_count++;
    }
    
    dof_map.num_total_dofs = current_dof;
    
    spdlog::info("DofNumberingSystem: DOF map built successfully.");
    spdlog::info("  - Node count: {}", static_cast<int>(node_count));
    spdlog::info("  - Total DOFs: {}", dof_map.num_total_dofs);
    spdlog::info("  - DOFs per node: {}", dof_map.dofs_per_node);
    spdlog::info("  - Mapping table size: {}", static_cast<int>(dof_map.node_to_dof_index.size()));
}

