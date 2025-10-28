// mesh.h
#pragma once
#include <vector>
#include <unordered_map>

// 使用类型别名增强代码可读性和可维护性
using NodeID = int;
using ElementID = int;

// 这是一个纯数据聚合体，描述一个具体的网格实例
struct Mesh {
    // --- 节点数据 (SoA 格式) ---
    std::vector<double> node_coordinates;         // [x1, y1, z1, x2, y2, z2, ...]
    std::unordered_map<NodeID, size_t> node_id_to_index; // 外部ID -> 内部索引
    std::vector<NodeID> node_index_to_id;         // 内部索引 -> 外部ID

    // --- 单元数据 (SoA 格式) ---
    std::vector<int> element_types;               // [type_e1, type_e2, ...] 存储每个单元的类型ID
    std::vector<int> element_connectivity;        // 所有单元的节点ID连续存储
    std::vector<size_t> element_offsets;          // 每个单元的节点列表在 connectivity 中的起始位置
    std::unordered_map<ElementID, size_t> element_id_to_index; // 外部ID -> 内部索引
    std::vector<ElementID> element_index_to_id;   // 内部索引 -> 外部ID 

    // --- 辅助函数 ---
    size_t getNodeCount() const {
        return node_index_to_id.size();
    }
    
    size_t getElementCount() const {
        return element_index_to_id.size();
    }

    // --- 管理函数 ---
    void clear() {
        // 清理节点数据
        node_coordinates.clear();
        node_id_to_index.clear();
        node_index_to_id.clear();

        // 清理单元数据
        element_types.clear();
        element_connectivity.clear();
        element_offsets.clear();
        element_id_to_index.clear();
        element_index_to_id.clear();
    }
};