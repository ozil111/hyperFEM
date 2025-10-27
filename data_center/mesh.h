// mesh.h
#pragma once
#include <vector>
#include <unordered_map>

// 假设NodeID的类型是int
using NodeID = int;

// 这是一个纯数据聚合体
struct Mesh {
    // 节点组件
    std::vector<double> node_coordinates;         // [x1, y1, z1, x2, y2, z2, ...]
    std::unordered_map<NodeID, size_t> node_id_to_index; // 外部ID -> 内部逻辑索引
    std::vector<NodeID> node_index_to_id;       // 内部逻辑索引 -> 外部ID

    // (未来还会有单元、材料等数据...)

    // 获取节点数量的辅助函数
    size_t get_node_count() const {
        return node_index_to_id.size();
    }

    void clear() {
        node_coordinates.clear();
        node_id_to_index.clear();
        node_index_to_id.clear();
    }
};