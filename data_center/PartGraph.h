// PartGraph.h
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
#include <unordered_map>

enum class ConnectionType {
    Contact,        // 显式定义的接触 (Tie, Surface-to-Surface)
    SharedNode,     // 隐式拓扑连接 (Mesh Topology)
    MPC             // 刚体连接
};

struct EdgeInfo {
    std::string target_part;
    ConnectionType type;
    double weight;
    int count; // 共享节点数量 或 接触定义数量
};

struct PartNode {
    std::string name;
    bool is_load_part = false;
    bool is_constraint_part = false;
    std::vector<EdgeInfo> edges;
};

class PartGraph {
public:
    std::unordered_map<std::string, PartNode> nodes;

    void add_node(const std::string& name) {
        if (nodes.find(name) == nodes.end()) {
            nodes[name] = {name};
        }
    }

    void add_edge(const std::string& src, const std::string& tgt, ConnectionType type, double weight, int count = 1) {
        if (nodes.find(src) == nodes.end()) add_node(src);
        if (nodes.find(tgt) == nodes.end()) add_node(tgt);

        // 检查是否已存在相同类型的边，如果是则累加计数
        auto& edges = nodes[src].edges;
        for (auto& edge : edges) {
            if (edge.target_part == tgt && edge.type == type) {
                edge.count += count;
                // 取最小权重 (阻抗越小连接越紧密)
                if (weight < edge.weight) edge.weight = weight;
                return;
            }
        }
        edges.push_back({tgt, type, weight, count});
    }
};