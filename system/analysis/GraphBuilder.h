/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once
#include "PartGraph.h"
#include "entt/entt.hpp"
#include "../simdroid/SimdroidInspector.h"
#include "../../data_center/components/simdroid_components.h"
#include <set>
#include <map>
#include <algorithm>

class GraphBuilder {
public:
    static PartGraph build(entt::registry& registry, SimdroidInspector& inspector) {
        PartGraph graph;

        // 确保索引已构建
        if (!inspector.is_built) {
            inspector.build(registry);
        }

        // 1. 初始化节点 (Parts)
        auto view_parts = registry.view<const Component::SimdroidPart>();
        for (auto entity : view_parts) {
            const auto& part = view_parts.get<const Component::SimdroidPart>(entity);
            graph.add_node(part.name);
        }

        // -------------------------------------------------------
        // 2. 处理 Contact (显式连接)
        // -------------------------------------------------------
        auto view_contacts = registry.view<const Component::ContactDefinition>();
        for (auto entity : view_contacts) {
            const auto& contact = view_contacts.get<const Component::ContactDefinition>(entity);
            
            auto master_parts = get_parts_from_set(registry, inspector, contact.master_entity);
            auto slave_parts = get_parts_from_set(registry, inspector, contact.slave_entity);

            for (const auto& m : master_parts) {
                for (const auto& s : slave_parts) {
                    if (m != s) {
                        // Contact 连接通常被视为“强连接”，权重设低一点 (1.0)
                        graph.add_edge(m, s, ConnectionType::Contact, 1.0);
                        graph.add_edge(s, m, ConnectionType::Contact, 1.0);
                    }
                }
            }
        }

        // -------------------------------------------------------
        // 3. 处理 Shared Nodes (隐式拓扑连接) - 核心算法
        // -------------------------------------------------------
        // 逻辑：遍历每个节点 -> 找出该节点属于哪些 Part -> 如果 >1 个 Part，则这些 Part 互联
        
        // 临时存储：pair<PartA, PartB> -> SharedNodeCount
        std::map<std::pair<std::string, std::string>, int> shared_topology_map;

        for (const auto& [nid, elem_ids] : inspector.nid_to_elems) {
            // 如果一个节点只被一个单元引用，或者只被同一个 Part 的单元引用，则无连接
            if (elem_ids.empty()) continue;

            // 收集该节点涉及的所有 Part
            std::vector<std::string> parts_sharing_this_node;
            parts_sharing_this_node.reserve(4); // 预留少量空间，通常不会超过4个

            for (int eid : elem_ids) {
                // 利用 Inspector 的 O(1) 查找
                if (inspector.eid_to_part.count(eid)) {
                    const std::string& p_name = inspector.eid_to_part.at(eid);
                    // 避免重复添加 (std::unique 需要排序，这里手动检查更快)
                    bool already_added = false;
                    for (const auto& existing : parts_sharing_this_node) {
                        if (existing == p_name) { already_added = true; break; }
                    }
                    if (!already_added) {
                        parts_sharing_this_node.push_back(p_name);
                    }
                }
            }

            // 如果该节点被多个 Part 共享，建立两两连接
            if (parts_sharing_this_node.size() > 1) {
                // 排序以确保 pair 的 key 一致性 (A, B) vs (B, A)
                std::sort(parts_sharing_this_node.begin(), parts_sharing_this_node.end());
                
                for (size_t i = 0; i < parts_sharing_this_node.size(); ++i) {
                    for (size_t j = i + 1; j < parts_sharing_this_node.size(); ++j) {
                        shared_topology_map[{parts_sharing_this_node[i], parts_sharing_this_node[j]}]++;
                    }
                }
            }
        }

        // 将统计结果写入 Graph
        for (const auto& [pair, count] : shared_topology_map) {
            // 阈值过滤：比如共享节点少于 3 个的可能是噪点？这里暂时全部保留
            // Shared Node 权重逻辑：共享节点越多，连接越紧密 (权重越低)
            double weight = (count > 100) ? 0.1 : (count > 10 ? 0.5 : 2.0);
            
            graph.add_edge(pair.first, pair.second, ConnectionType::SharedNode, weight, count);
            graph.add_edge(pair.second, pair.first, ConnectionType::SharedNode, weight, count);
        }

        return graph;
    }

private:
    // 辅助函数：从 Set Entity 解析出包含的 Parts
    static std::vector<std::string> get_parts_from_set(entt::registry& reg, SimdroidInspector& insp, entt::entity set_entity) {
        std::vector<std::string> parts;
        if (!reg.valid(set_entity)) return parts;
        
        // 如果是 Element Set
        if (reg.all_of<Component::ElementSetMembers>(set_entity)) {
            const auto& members = reg.get<Component::ElementSetMembers>(set_entity).members;
            // 优化：不需要遍历所有 member，只需要采样几个或者全部遍历去重
            // 为了准确性，我们遍历全部，但用 Set 去重
            std::set<std::string> unique_parts;
            for (auto ent : members) {
                if (reg.all_of<Component::OriginalID>(ent)) {
                    int eid = reg.get<Component::OriginalID>(ent).value;
                    if (insp.eid_to_part.count(eid)) {
                        unique_parts.insert(insp.eid_to_part.at(eid));
                    }
                }
            }
            parts.assign(unique_parts.begin(), unique_parts.end());
        }
        // TODO: 支持 Node Set (通过 Node -> Elem -> Part)
        return parts;
    }
};