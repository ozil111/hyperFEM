// data_center/components/load_components.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <string>
#include <vector>
#include "entt/entt.hpp"

/**
 * @namespace Component
 * @brief ECS组件 - Load 和 Boundary（载荷与边界条件）部分
 * @details 这些组件分为两类：
 *   1. 定义组件：附加到 Load/Boundary 实体上，存储载荷/边界条件的抽象定义
 *   2. 应用组件：附加到 Node/Element 实体上，指向应用于它们的 Load/Boundary 实体
 */
namespace Component {

    // ===================================================================
    // ID组件 - 用于标识不同类型的实体，避免ID冲突
    // ===================================================================

    /**
     * @brief [新] 附加到 Load 实体，存储其用户定义的ID (lid)
     * @details 用于标识Load实体，避免与其他类型实体的ID冲突
     */
    struct LoadID {
        int value;
    };

    /**
     * @brief [新] 附加到 Boundary 实体，存储其用户定义的ID (bid)
     * @details 用于标识Boundary实体，避免与其他类型实体的ID冲突
     */
    struct BoundaryID {
        int value;
    };

    // ===================================================================
    // 定义组件 (Definition Components) - 附加到 Load/Boundary 实体
    // ===================================================================

    /**
     * @brief [新] 附加到 Load 实体，存储节点载荷的定义
     * @details 对应 JSON 中的 "load" 对象
     * 一个 Load 实体代表一个抽象的载荷定义，可以被应用到多个节点上
     */
    struct NodalLoad {
        int type_id;        // 载荷类型 ID
        std::string dof;    // 自由度："all", "x", "y", "z", "xy" 等
        double value;       // 载荷值
    };

    /**
     * @brief [新] 附加到 Load 实体，指向关联的 Curve 实体（可选）
     * @details 如果存在，载荷值会根据curve和时间进行缩放
     */
    struct CurveRef {
        entt::entity curve_entity;  // 指向Curve实体的引用
    };

    /**
     * @brief [新] 附加到 Boundary 实体，存储单点约束 (SPC) 的定义
     * @details 对应 JSON 中的 "boundary" 对象
     * 一个 Boundary 实体代表一个抽象的边界条件定义，可以被应用到多个节点上
     */
    struct BoundarySPC {
        int type_id;        // 边界条件类型 ID
        std::string dof;    // 约束自由度："all", "x", "y", "z", "xy" 等
        double value;       // 约束值（通常为 0.0 表示固定）
    };

    /**
     * @brief [新] 附加到 Curve 实体，存储曲线ID
     * @details 用于标识Curve实体
     */
    struct CurveID {
        int value;
    };

    /**
     * @brief [新] 附加到 Curve 实体，存储曲线的定义
     * @details 对应 JSON 中的 "curve" 对象
     * 支持不同类型的曲线（如linear），用于随时间缩放载荷值
     */
    struct Curve {
        std::string type;   // 曲线类型："linear" 等
        std::vector<double> x;  // x坐标数组（通常是时间）
        std::vector<double> y;  // y坐标数组（通常是缩放因子）
    };

    // ===================================================================
    // 应用组件 (Application Components) - 附加到 Node/Element 实体
    // ===================================================================

    /**
     * @brief [新] 附加到 Node 实体，包含应用于该节点的所有 Load 实体引用
     * @details 这是 Plan B 的核心：通过引用实现"多对一"关系。
     *          使用 vector 允许单个节点引用多个载荷定义（例如 ForceX 与 ForceY 同时存在）。
     * 
     * 使用示例：
     *   // 在求解器中遍历所有受载节点
     *   auto view = registry.view<Component::AppliedLoadRef, Component::Position>();
     *   for(auto [node_entity, load_ref, pos] : view.each()) {
     *       for(const auto load_entity : load_ref.load_entities) {
     *           // 获取载荷定义
     *           const auto& load = registry.get<Component::NodalLoad>(load_entity);
     *           // 应用载荷到节点
     *           apply_force(node_entity, load.dof, load.value);
     *       }
     *   }
     */
    struct AppliedLoadRef {
        std::vector<entt::entity> load_entities;
    };

    /**
     * @brief [新] 附加到 Node 实体，包含应用于该节点的所有 Boundary 实体引用
     * @details 与 AppliedLoadRef 类似，但用于边界条件
     */
    struct AppliedBoundaryRef {
        std::vector<entt::entity> boundary_entities;
    };

    // 未来扩展: 可以添加其他类型的载荷和边界条件
    // struct AppliedPressureRef { entt::entity pressure_entity; };
    // struct AppliedTemperatureRef { entt::entity temperature_entity; };

} // namespace Component

