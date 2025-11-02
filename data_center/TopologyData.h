// TopologyData.h
#pragma once

#include "mesh.h" // 引用您原始的网格数据结构
#include <vector>
#include <unordered_map>

// --- 类型别名，增强代码清晰度 ---
using NodeID = int;       // 来自输入文件的外部ID
using ElementID = int;    // 来自输入文件的外部ID

using FaceID = size_t;       // 面实体的内部索引 (0 to N-1)
using BodyID = int;          // 连续网格体的ID
using ElementIndex = size_t; // 单元的内部索引 (0 to N-1)，明确其用途

// 定义一个“面”：由其节点的外部ID排序后构成，以保证唯一性和稳定性
using FaceKey = std::vector<NodeID>;

// --- 哈希函数，让 FaceKey 可以用于 unordered_map ---
// 对于 std::vector<int> 作为键，需要自定义哈希函数
struct VectorHasher {
    std::size_t operator()(const std::vector<int>& v) const {
        std::size_t seed = v.size();
        for(int i : v) {
            seed ^= i + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

// -------------------------------------------------------------------
// **核心数据结构 (Component)**
// 这个结构体只包含拓扑数据，不包含任何逻辑。
// -------------------------------------------------------------------
struct TopologyData {
    // --- 核心拓扑实体 ---

    // 1. 面实体 (Face)
    // `faces` 的索引就是 FaceID
    std::vector<FaceKey> faces;
    // 从 FaceKey -> FaceID 的快速查找表
    std::unordered_map<FaceKey, FaceID, VectorHasher> face_key_to_id;

    // --- 关系映射表 (存储内部索引以保证性能) ---

    // 2. 单元 <-> 面 的双向查找
    // `element_to_faces[elem_idx]` -> 获取该单元拥有的所有面的 FaceID
    std::vector<std::vector<FaceID>> element_to_faces;
    // `face_to_elements[face_id]` -> 获取共享该面的所有单元的 ElementIndex
    // **优化点**: 存储 ElementIndex (size_t) 而不是 ElementID (int)
    std::vector<std::vector<ElementIndex>> face_to_elements;

    // 3. 单元 -> 连续体 的关系
    // `element_to_body[elem_idx]` -> 获取该单元所属的连续体 BodyID
    std::vector<BodyID> element_to_body;
    // `body_to_elements[body_id]` -> 获取该连续体包含的所有单元的 ElementIndex
    // **优化点**: 存储 ElementIndex
    std::unordered_map<BodyID, std::vector<ElementIndex>> body_to_elements;

    // --- 辅助数据 ---
    // 持有对原始网格数据的常量引用，避免数据拷贝
    const Mesh& source_mesh;

    // --- 构造与清理 ---
    explicit TopologyData(const Mesh& mesh) : source_mesh(mesh) {}

    void clear() {
        faces.clear();
        face_key_to_id.clear();
        element_to_faces.clear();
        face_to_elements.clear();
        element_to_body.clear();
        body_to_elements.clear();
    }
};