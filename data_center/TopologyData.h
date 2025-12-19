// TopologyData.h
#pragma once

#include <vector>
#include <unordered_map>
#include "entt/entt.hpp"

// --- 类型别名，增强代码清晰度 ---
using NodeID = int;       // 来自输入文件的外部ID
using ElementID = int;    // 来自输入文件的外部ID

using FaceID = size_t;       // 面实体的内部索引 (0 to N-1)
using BodyID = int;          // 连续网格体的ID

// 定义一个"面"：由其节点的外部ID排序后构成，以保证唯一性和稳定性
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
// **核心数据结构 - 派生/加速数据**
// 这个结构体只包含拓扑数据，不包含任何逻辑。
// 它作为从EnTT registry中的基础组件计算出的派生数据，
// 存储在registry的上下文(context)中供后续系统使用。
// -------------------------------------------------------------------
struct TopologyData {
    // --- 核心拓扑实体 ---

    // 1. 面实体 (Face)
    // `faces` 的索引就是 FaceID
    std::vector<FaceKey> faces;
    // 从 FaceKey -> FaceID 的快速查找表
    std::unordered_map<FaceKey, FaceID, VectorHasher> face_key_to_id;

    // --- 关系映射表 ---
    // 注意：这里使用entt::entity而不是索引，因为entity是稳定的句柄

    // 2. 单元 <-> 面 的双向查找
    // `element_to_faces[entity]` -> 获取该单元实体拥有的所有面的 FaceID
    std::unordered_map<entt::entity, std::vector<FaceID>> element_to_faces;
    // `face_to_elements[face_id]` -> 获取共享该面的所有单元entity
    std::vector<std::vector<entt::entity>> face_to_elements;

    // 3. 单元 -> 连续体 的关系
    // `element_to_body[entity]` -> 获取该单元entity所属的连续体 BodyID
    std::unordered_map<entt::entity, BodyID> element_to_body;
    // `body_to_elements[body_id]` -> 获取该连续体包含的所有单元entity
    std::unordered_map<BodyID, std::vector<entt::entity>> body_to_elements;

    // --- 构造与清理 ---
    TopologyData() = default;

    void clear() {
        faces.clear();
        face_key_to_id.clear();
        element_to_faces.clear();
        face_to_elements.clear();
        element_to_body.clear();
        body_to_elements.clear();
    }
};