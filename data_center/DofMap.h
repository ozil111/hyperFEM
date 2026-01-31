// DofMap.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <vector>
#include "entt/entt.hpp"

/**
 * @brief DOF 映射资源 (DOF Mapping Resource)
 * @details 
 *   - 存储在 registry.ctx() (Context) 中的全局唯一数据
 *   - 提供节点实体到全局自由度索引的快速映射
 *   - 在分析开始前构建一次，之后多个系统共享使用
 * 
 * 架构优势：
 *   - 解耦：StiffnessSystem、MassSystem、ForceSystem 等都可使用
 *   - 缓存：DOF 编号在非线性迭代中保持不变
 *   - 性能：使用 vector 实现 O(1) 访问（entity ID 直接作为索引）
 */
struct DofMap {
    /**
     * @brief 核心映射：Entity ID -> Global DOF Start Index
     * @details 
     *   - 索引：entity ID (通过 static_cast<uint32_t>(entity) 转换)
     *   - 值：该节点的起始全局自由度编号
     *   - 每个节点通常有 3 个自由度（x, y, z），所以节点的 DOF 范围是 [index, index+2]
     *   - 如果值为 -1，表示该 entity ID 不是节点或未分配 DOF
     */
    std::vector<int> node_to_dof_index;

    /**
     * @brief 总自由度数量（即系统方程的大小）
     */
    int num_total_dofs = 0;

    /**
     * @brief 每个节点的自由度数（通常为 3，用于 3D 实体单元）
     * @details 未来可扩展支持不同类型的节点（如梁节点有 6 个自由度）
     */
    int dofs_per_node = 3;

    /**
     * @brief 检查 entity 是否在映射中
     */
    bool has_node(entt::entity node_entity) const {
        uint32_t entity_id = static_cast<uint32_t>(node_entity);
        if (entity_id >= node_to_dof_index.size()) {
            return false;
        }
        return node_to_dof_index[entity_id] != -1;
    }

    /**
     * @brief 获取节点的全局自由度索引（安全版本，带边界检查）
     * @param node_entity 节点实体
     * @param dof 自由度方向（0=x, 1=y, 2=z）
     * @return 全局自由度索引，如果节点不存在返回 -1
     */
    int get_dof_index(entt::entity node_entity, int dof) const {
        uint32_t entity_id = static_cast<uint32_t>(node_entity);
        if (entity_id >= node_to_dof_index.size()) {
            return -1;
        }
        int base_index = node_to_dof_index[entity_id];
        if (base_index == -1) {
            return -1;
        }
        return base_index + dof;
    }

    /**
     * @brief 快速获取节点的全局自由度索引（不安全版本，不检查边界）
     * @param entity_id 节点实体 ID（已转换为 uint32_t）
     * @param dof 自由度方向（0=x, 1=y, 2=z）
     * @return 全局自由度索引
     * @details 
     *   - 性能优化：跳过边界检查，直接数组访问
     *   - 使用前提：确保 entity_id 有效且 DofMap 已正确构建
     *   - 仅在热点循环中使用，Assembly 阶段通常满足前提条件
     */
    inline int get_dof_index_unsafe(uint32_t entity_id, int dof) const {
        return node_to_dof_index[entity_id] + dof;
    }

    /**
     * @brief 获取底层数组的指针（用于极致性能优化）
     * @return 指向 node_to_dof_index 数组的指针
     * @details 允许直接数组访问，避免函数调用开销
     */
    const int* get_dof_array_ptr() const {
        return node_to_dof_index.data();
    }
};

