// data_center/components/property_components.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <string>
#include "entt/entt.hpp"

/**
 * @namespace Component
 * @brief ECS组件 - Property（属性）部分
 * @details Property是连接Element和Material的中间层，存储单元的物理属性和计算参数
 */
namespace Component {

    /**
     * @brief [新] 附加到 Property 实体，存储其用户定义的ID (pid)
     * @details 用于标识Property实体，避免与其他类型实体的ID冲突
     */
    struct PropertyID {
        int value;
    };

    /**
     * @brief [新] 附加到 Property 实体，存储固体单元的属性
     * @details 对应 JSON 中的 "property" 对象
     */
    struct SolidProperty {
        int type_id;                    // 来自 JSON 的 "typeid"
        int integration_network;        // 积分网络参数，如 "integration_network": 2
        std::string hourglass_control;  // 沙漏控制方法，如 "hourglass_control": "eas"
    };

    /**
     * @brief [新] 附加到 Property 实体，指向其关联的 Material 实体
     * @details 这是 Plan B 的核心：通过 entt::entity 句柄建立实体间的引用关系
     * 使用方式：
     *   auto material_entity = registry.get<Component::MaterialRef>(property_entity).material_entity;
     *   const auto& material_params = registry.get<Component::LinearElasticParams>(material_entity);
     */
    struct MaterialRef {
        entt::entity material_entity;
    };

    // 未来扩展: 可以添加其他类型的 Property
    // struct ShellProperty { ... };
    // struct BeamProperty { ... };

} // namespace Component

