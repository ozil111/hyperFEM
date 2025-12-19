// TopologySystems.h
#pragma once

#include "TopologyData.h"
#include "entt/entt.hpp"
#include "components/mesh_components.h"

// -------------------------------------------------------------------
// **逻辑系统 (Systems)**
// 这个类是无状态的，所有函数都是静态的。
// 它操作EnTT registry，从中读取基础组件数据，
// 生成派生的TopologyData，并存储在registry的上下文中。
// -------------------------------------------------------------------
class TopologySystems {
public:
    /**
     * @brief [System 1] 从registry中的基础组件提取拓扑关系。
     * @details 遍历所有单元实体，识别出唯一的面，并构建单元与面之间的双向查找表。
     * 生成的TopologyData将存储在registry.ctx()中。
     * @param registry EnTT registry，包含所有节点和单元实体
     */
    static void extract_topology(entt::registry& registry);

    /**
     * @brief [System 2] 基于拓扑关系查找所有连续的网格体。
     * @details 使用洪水填充算法，通过共享内部面来遍历和分组单元。
     * 从registry.ctx()中获取TopologyData，并更新其连通性信息。
     * @param registry EnTT registry，其上下文中必须包含TopologyData
     */
    static void find_continuous_bodies(entt::registry& registry);

private:
    /**
     * @brief 辅助函数：根据单元的节点和类型，提取其所有的面（或边）。
     * @param element_nodes 组成单元的节点列表 (使用外部ID)。
     * @param element_type 单元的类型ID，用于决定如何提取面。
     * @return 一个包含多个面的列表，每个面由其节点ID列表定义。
     */
    static std::vector<FaceKey> get_faces_from_element(
        const std::vector<NodeID>& element_nodes,
        int element_type
    );
};