// mesh_components.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <vector>
#include <string>
#include "entt/entt.hpp"

// Type aliases for clarity and consistency
using NodeID = int;
using ElementID = int;

/**
 * @namespace Component
 * @brief Contains all ECS components for mesh representation
 * @details Components are organized by domain:
 *   - Geometric components: Position
 *   - Topological components: Connectivity, ElementType
 *   - Identification components: OriginalID
 *   - Set components: SetName, NodeSetMembers, ElementSetMembers
 */
namespace Component {

    // ===================================================================
    // Core Geometric & Topological Components
    // ===================================================================

    /**
     * @brief 3D position component for node entities
     * @details Attached to entities representing mesh nodes
     */
    struct Position {
        double x, y, z;
    };

    /**
     * @brief [已废弃] Stores the original ID from the input file
     * @details 为了保持一致性并避免ID冲突，现在使用专门的ID组件：
     *   - Node 使用 NodeID
     *   - Element 使用 ElementID
     *   此组件保留用于向后兼容，但新代码应使用专门的ID组件
     * @deprecated 使用 NodeID 或 ElementID 替代
     */
    struct OriginalID {
        int value;
    };

    /**
     * @brief [新] 附加到 Node 实体，存储其用户定义的ID (nid)
     * @details 用于标识Node实体，避免与其他类型实体的ID冲突
     */
    struct NodeID {
        int value;
    };

    /**
     * @brief [新] 附加到 Element 实体，存储其用户定义的ID (eid)
     * @details 用于标识Element实体，避免与其他类型实体的ID冲突
     */
    struct ElementID {
        int value;
    };

    /**
     * @brief Element type identifier (e.g., 308 for Hexa8, 304 for Tetra4)
     * @details Attached to element entities. Used to:
     *   - Determine element topology
     *   - Look up properties in ElementRegistry
     *   - Extract faces for topology analysis
     */
    struct ElementType {
        int type_id;
    };

    /**
     * @brief Element-to-node connectivity
     * @details Attached to element entities. Stores direct entity handles
     * to node entities, enabling fast traversal without ID lookups.
     */
    struct Connectivity {
        std::vector<entt::entity> nodes;  // Direct handles to node entities
    };

    // ===================================================================
    // Set-Related Components
    // ===================================================================
    // Strategy: Each set (node set or element set) is represented as
    // a separate entity with SetName and member components attached.

    /**
     * @brief Name identifier for a set entity
     * @details Attached to set entities. Used for:
     *   - User-friendly identification
     *   - File export
     *   - Command-line queries
     */
    struct SetName {
        std::string value;
    };

    /**
     * @brief Members of a node set
     * @details Attached to node set entities. Contains entity handles
     * to all node entities that are members of this set.
     */
    struct NodeSetMembers {
        std::vector<entt::entity> members;
    };

    /**
     * @brief Members of an element set
     * @details Attached to element set entities. Contains entity handles
     * to all element entities that are members of this set.
     */
    struct ElementSetMembers {
        std::vector<entt::entity> members;
    };

    // ===================================================================
    // Surface (Face) Components (Simdroid)
    // ===================================================================
    // Note:
    // - "Surface" in Simdroid mesh.dat is a list of boundary faces/edges with stable IDs.
    // - We represent each surface entry as its own ECS entity, using dedicated components
    //   to avoid mixing them with volume/shell elements (which use Component::Connectivity).

    /**
     * @brief Surface ID component for surface entities (sid)
     * @details Attached to surface entities parsed from / exported to Simdroid "Surface {" block.
     */
    struct SurfaceID {
        int value;
    };

    /**
     * @brief Surface connectivity (nodes on the face/edge)
     * @details Uses direct node entity handles; does NOT reuse Component::Connectivity
     * to avoid being treated as a volume/shell element by other systems.
     */
    struct SurfaceConnectivity {
        std::vector<entt::entity> nodes;
    };

    /**
     * @brief Parent element reference for a surface entity
     * @details Simdroid surface lines append the parent element ID at the end.
     */
    struct SurfaceParentElement {
        entt::entity element;
    };

    /**
     * @brief Members of a surface set (SurfaceSet)
     * @details Attached to set entities representing Simdroid "Set { Surface { ... } }".
     */
    struct SurfaceSetMembers {
        std::vector<entt::entity> members;
    };

    /**
     * @brief [新] 附加到 NodeSet 实体，存储其用户定义的ID (nsid)
     * @details 用于标识NodeSet实体，避免与其他类型实体的ID冲突
     */
    struct NodeSetID {
        int value;
    };

    /**
     * @brief [新] 附加到 EleSet 实体，存储其用户定义的ID (esid)
     * @details 用于标识EleSet实体，避免与其他类型实体的ID冲突
     */
    struct EleSetID {
        int value;
    };

    // ===================================================================
    // Reference Components (Plan B: Entity-to-Entity References)
    // ===================================================================
    // These components establish relationships between entities by storing
    // entt::entity handles, enabling flexible and memory-efficient references.

    /**
     * @brief [新] 附加到 Element 实体，指向其关联的 Property 实体
     * @details 用于 Plan B 架构：Element 通过此组件引用 Property，
     * Property 再通过 MaterialRef 引用 Material。
     * 
     * 使用示例：
     *   auto property_entity = registry.get<Component::PropertyRef>(element_entity).property_entity;
     *   const auto& property = registry.get<Component::SolidProperty>(property_entity);
     *   auto material_entity = registry.get<Component::MaterialRef>(property_entity).material_entity;
     *   const auto& material = registry.get<Component::LinearElasticParams>(material_entity);
     */
    struct PropertyRef {
        entt::entity property_entity;
    };

    // ===================================================================
    // Explicit Dynamics Components
    // ===================================================================
    // Components for explicit time integration solver

    /**
     * @brief 节点速度组件（用于显式动力学）
     * @details 附加到 Node 实体，存储节点在三个方向的速度分量
     */
    struct Velocity {
        double vx, vy, vz;
    };

    /**
     * @brief 节点加速度组件（用于显式动力学）
     * @details 附加到 Node 实体，存储节点在三个方向的加速度分量
     */
    struct Acceleration {
        double ax, ay, az;
    };

    /**
     * @brief 节点位移组件（用于显式动力学）
     * @details 附加到 Node 实体，存储节点在三个方向的位移分量
     */
    struct Displacement {
        double dx, dy, dz;
    };

    /**
     * @brief 节点集中质量组件（用于显式动力学）
     * @details 附加到 Node 实体，存储节点的集中质量（Lumped Mass）
     * 通过 MassSystem 从单元质量分配得到
     */
    struct Mass {
        double value;
    };

    /**
     * @brief 节点外力组件（用于显式动力学）
     * @details 附加到 Node 实体，存储外部载荷在三个方向的分量
     * 由 LoadSystem 从 AppliedLoadRef 计算得到
     */
    struct ExternalForce {
        double fx, fy, fz;
    };

    /**
     * @brief 节点内力组件（用于显式动力学）
     * @details 附加到 Node 实体，存储单元应力产生的内力在三个方向的分量
     * 由 InternalForceSystem 从单元应力计算得到
     */
    struct InternalForce {
        double fx, fy, fz;
    };

    /**
     * @brief 初始位置组件（用于显式动力学）
     * @details 附加到 Node 实体，存储节点的初始位置，用于计算位移增量
     * 在求解器初始化时从 Position 复制得到
     */
    struct InitialPosition {
        double x0, y0, z0;
    };

} // namespace Component

