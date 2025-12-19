// mesh_components.h
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

} // namespace Component

