// TopologySystems.h
#pragma once

#include "TopologyData.h"

// -------------------------------------------------------------------
// **逻辑系统 (Systems)**
// 这个类是无状态的，所有函数都是静态的。
// 它接收 TopologyData 作为输入/输出参数并对其进行操作。
// -------------------------------------------------------------------
class TopologySystems {
public:
    /**
     * @brief [System 1] 从原始Mesh数据中提取拓扑关系。
     * @details 遍历所有单元，识别出唯一的面，并构建单元与面之间的双向查找表。
     * @param source_mesh 输入的原始网格数据。
     * @param topology 输出参数，填充后的拓扑数据。
     */
    static void extract_topology(const Mesh& source_mesh, TopologyData& topology);

    /**
     * @brief [System 2] 基于拓扑关系查找所有连续的网格体。
     * @details 使用洪水填充算法，通过共享内部面来遍历和分组单元。
     * @param topology 输入/输出参数，此函数将填充其 `element_to_body` 和 `body_to_elements` 字段。
     */
    static void find_continuous_bodies(TopologyData& topology);

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