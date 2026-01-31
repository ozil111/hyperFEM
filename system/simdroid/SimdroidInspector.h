// system/simdroid/SimdroidInspector.h
#pragma once
#include "entt/entt.hpp"
#include "../data_center/components/mesh_components.h"
#include "../data_center/components/simdroid_components.h"
#include <unordered_map>
#include <string>
#include <vector>
#include <algorithm>
#include <iostream>
#include <iomanip>

struct SimdroidInspector {
    // --- 查找表 (Lookup Tables) ---
    // 映射: Node ID (用户视角) -> Node Entity
    std::unordered_map<int, entt::entity> nid_to_entity;
    // 映射: Element ID (用户视角) -> Element Entity
    std::unordered_map<int, entt::entity> eid_to_entity;
    // 映射: Element ID -> 所属 Part 名称
    std::unordered_map<int, std::string> eid_to_part;
    // 映射: Node ID -> 包含该节点的所有 Element IDs
    std::unordered_map<int, std::vector<int>> nid_to_elems;

    bool is_built = false;

    // --- 构建索引 (Build Index) ---
    // 这是一个后处理步骤，解析完成后调用一次
    void build(entt::registry& registry) {
        std::cout << "Building inspection index..." << std::endl;
        clear();

        // 1. 构建 ID -> Entity 映射 & Node -> Elements 映射
        auto view_nodes = registry.view<const Component::NodeID>();
        for (auto entity : view_nodes) {
            nid_to_entity[view_nodes.get<const Component::NodeID>(entity).value] = entity;
        }

        auto view_elems = registry.view<const Component::ElementID, const Component::Connectivity>();
        for (auto entity : view_elems) {
            int eid = view_elems.get<const Component::ElementID>(entity).value;
            eid_to_entity[eid] = entity;

            // 建立 Node -> Elements 的反向关联
            const auto& conn = view_elems.get<const Component::Connectivity>(entity);
            for (auto node_entity : conn.nodes) {
                if(registry.all_of<Component::NodeID>(node_entity)) {
                    int nid = registry.get<Component::NodeID>(node_entity).value;
                    nid_to_elems[nid].push_back(eid);
                }
            }
        }

        // 2. 构建 Element -> Part 映射
        // 遍历所有 SimdroidPart 组件
        auto view_parts = registry.view<const Component::SimdroidPart>();
        for (auto part_entity : view_parts) {
            const auto& part = view_parts.get<const Component::SimdroidPart>(part_entity);
            entt::entity set_entity = part.element_set;
            
            if (registry.valid(set_entity) && registry.all_of<Component::ElementSetMembers>(set_entity)) {
                const auto& members = registry.get<Component::ElementSetMembers>(set_entity);
                for (auto elem_entity : members.members) {
                    if (registry.all_of<Component::ElementID>(elem_entity)) {
                        int eid = registry.get<Component::ElementID>(elem_entity).value;
                        eid_to_part[eid] = part.name;
                    }
                }
            }
        }
        
        is_built = true;
        std::cout << "Index built. Indexed " << nid_to_entity.size() << " nodes and " << eid_to_entity.size() << " elements." << std::endl;
    }

    void clear() {
        nid_to_entity.clear();
        eid_to_entity.clear();
        eid_to_part.clear();
        nid_to_elems.clear();
        is_built = false;
    }

    // --- 交互式查询功能 ---

    void inspect_node(entt::registry& registry, int nid) {
        if (!is_built) { std::cout << "Error: Index not built." << std::endl; return; }
        
        auto it = nid_to_entity.find(nid);
        if (it == nid_to_entity.end()) {
            std::cout << "Node " << nid << " not found." << std::endl;
            return;
        }

        auto entity = it->second;
        auto pos = registry.get<Component::Position>(entity);
        
        std::cout << "\n=== Node Inspector [" << nid << "] ===" << std::endl;
        std::cout << "Coords: (" << pos.x << ", " << pos.y << ", " << pos.z << ")" << std::endl;
        
        // 所属单元
        const auto& elems = nid_to_elems[nid];
        std::cout << "Used by " << elems.size() << " Elements: ";
        for (size_t i = 0; i < std::min(elems.size(), size_t(10)); ++i) std::cout << elems[i] << " ";
        if (elems.size() > 10) std::cout << "...";
        std::cout << std::endl;

        // 所属 Part (通过单元推断)
        std::vector<std::string> parts;
        for (int eid : elems) {
            if (eid_to_part.count(eid)) {
                const std::string& p = eid_to_part[eid];
                if (std::find(parts.begin(), parts.end(), p) == parts.end()) parts.push_back(p);
            }
        }
        std::cout << "Belongs to Parts: ";
        for (const auto& p : parts) std::cout << "[" << p << "] ";
        std::cout << std::endl;
    }

    void inspect_element(entt::registry& registry, int eid) {
        if (!is_built) { std::cout << "Error: Index not built." << std::endl; return; }

        auto it = eid_to_entity.find(eid);
        if (it == eid_to_entity.end()) {
            std::cout << "Element " << eid << " not found." << std::endl;
            return;
        }
        
        // 基础信息
        int type_id = registry.get<Component::ElementType>(it->second).type_id;
        std::cout << "\n=== Element Inspector [" << eid << "] ===" << std::endl;
        std::cout << "Type: " << type_id << std::endl;

        // 所属 Part
        if (eid_to_part.count(eid)) {
            std::cout << "Owner Part: \033[1;32m" << eid_to_part[eid] << "\033[0m" << std::endl;
        } else {
            std::cout << "Owner Part: <Unknown/Orphan>" << std::endl;
        }

        // 包含节点
        const auto& conn = registry.get<Component::Connectivity>(it->second);
        std::cout << "Nodes (" << conn.nodes.size() << "): [";
        for (auto node_e : conn.nodes) {
            if (registry.all_of<Component::NodeID>(node_e))
                std::cout << registry.get<Component::NodeID>(node_e).value << " ";
        }
        std::cout << "]" << std::endl;
    }
    
    void list_parts(entt::registry& registry) {
         if (!is_built) { std::cout << "Error: Index not built." << std::endl; return; }
         
         auto view = registry.view<const Component::SimdroidPart>();
         std::cout << "\n=== Detected Parts (" << view.size() << ") ===" << std::endl;
         std::cout << std::left << std::setw(30) << "Part Name" << std::setw(15) << "Material" << "Element Count" << std::endl;
         std::cout << std::string(60, '-') << std::endl;
         
         for(auto entity : view) {
             const auto& part = view.get<const Component::SimdroidPart>(entity);
             
             // 获取单元数量
             size_t count = 0;
             if (registry.valid(part.element_set) && registry.all_of<Component::ElementSetMembers>(part.element_set)) {
                 count = registry.get<Component::ElementSetMembers>(part.element_set).members.size();
             }
             
             // 获取材料名
             std::string mat_name = "-";
             if (registry.valid(part.material) && registry.all_of<Component::SetName>(part.material)) {
                 mat_name = registry.get<Component::SetName>(part.material).value;
             }
             
             std::cout << std::left << std::setw(30) << part.name 
                       << std::setw(15) << mat_name 
                       << count << std::endl;
         }
         std::cout << std::endl;
    }
};