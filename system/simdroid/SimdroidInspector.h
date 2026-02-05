// system/simdroid/SimdroidInspector.h
#pragma once
#include "entt/entt.hpp"
#include "../data_center/components/mesh_components.h"
#include "../data_center/components/simdroid_components.h"
#include "spdlog/spdlog.h"
#include <unordered_map>
#include <unordered_set>
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
                if(registry.valid(node_entity) && registry.all_of<Component::NodeID>(node_entity)) {
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
                    if (registry.valid(elem_entity) && registry.all_of<Component::ElementID>(elem_entity)) {
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

    /**
     * @brief 删除指定 Part 及其关联的单元、独有节点和相关定义
     */
    bool delete_part(entt::registry& registry, const std::string& target_part_name) {
        if (!is_built) {
            spdlog::error("Index not built. Cannot delete part safely.");
            return false;
        }

        // 1. 找到 Part 实体
        entt::entity part_entity = entt::null;
        auto part_view = registry.view<const Component::SimdroidPart>();
        for (auto entity : part_view) {
            if (part_view.get<const Component::SimdroidPart>(entity).name == target_part_name) {
                part_entity = entity;
                break;
            }
        }

        if (part_entity == entt::null) {
            return false; // Part 不存在
        }

        spdlog::info("Deleting Part: {}", target_part_name);

        // 2. 收集所有待删除的单元 (Elements)
        std::vector<entt::entity> elements_to_delete;
        std::vector<int> element_ids_to_delete;
        elements_to_delete.reserve(eid_to_part.size());
        element_ids_to_delete.reserve(eid_to_part.size());

        for (const auto& [eid, part_name] : eid_to_part) {
            if (part_name != target_part_name) continue;
            auto it = eid_to_entity.find(eid);
            if (it == eid_to_entity.end()) continue;
            if (!registry.valid(it->second)) continue;
            elements_to_delete.push_back(it->second);
            element_ids_to_delete.push_back(eid);
        }
        spdlog::info(" -> Found {} elements to delete.", elements_to_delete.size());

        // 2.5 收集并删除与这些 Elements 关联的 Surface（以及从 SurfaceSet 中移除）
        // 原因：Surface 的 parent element 被删后会变成悬空引用，导出时会产生 parent_eid=0 等脏数据。
        std::vector<entt::entity> surfaces_to_delete;
        surfaces_to_delete.reserve(elements_to_delete.size() * 2);
        {
            std::unordered_set<entt::entity> elem_entities_to_delete;
            elem_entities_to_delete.reserve(elements_to_delete.size() * 2);
            for (auto ee : elements_to_delete) elem_entities_to_delete.insert(ee);

            auto surf_view = registry.view<const Component::SurfaceParentElement>();
            for (auto se : surf_view) {
                const auto& pe = surf_view.get<const Component::SurfaceParentElement>(se).element;
                if (elem_entities_to_delete.find(pe) != elem_entities_to_delete.end()) {
                    surfaces_to_delete.push_back(se);
                }
            }

            if (!surfaces_to_delete.empty()) {
                std::sort(surfaces_to_delete.begin(), surfaces_to_delete.end());
                surfaces_to_delete.erase(std::unique(surfaces_to_delete.begin(), surfaces_to_delete.end()), surfaces_to_delete.end());

                // 从所有 SurfaceSetMembers 中移除这些 surface
                std::unordered_set<entt::entity> surfaces_to_delete_set;
                surfaces_to_delete_set.reserve(surfaces_to_delete.size() * 2);
                for (auto se : surfaces_to_delete) surfaces_to_delete_set.insert(se);

                auto sset_view = registry.view<Component::SurfaceSetMembers>();
                for (auto set_e : sset_view) {
                    auto& mem = registry.get<Component::SurfaceSetMembers>(set_e).members;
                    mem.erase(
                        std::remove_if(mem.begin(), mem.end(), [&](entt::entity x) {
                            return !registry.valid(x) || surfaces_to_delete_set.find(x) != surfaces_to_delete_set.end();
                        }),
                        mem.end()
                    );
                }

                // 删除 surface 实体本身（先删 surface，再删 node/element，避免 surface 悬空）
                for (auto se : surfaces_to_delete) {
                    if (registry.valid(se)) registry.destroy(se);
                }

                spdlog::info(" -> Removed {} surfaces associated with deleted elements.", surfaces_to_delete.size());
            }
        }

        // 3. 标记待删除的节点 (孤儿节点检测)
        std::vector<entt::entity> nodes_to_delete;
        nodes_to_delete.reserve(elements_to_delete.size() * 4);

        std::unordered_set<int> deleted_eids_set(element_ids_to_delete.begin(), element_ids_to_delete.end());

        for (auto elem_entity : elements_to_delete) {
            if (!registry.valid(elem_entity) || !registry.all_of<Component::Connectivity>(elem_entity)) continue;
            const auto& conn = registry.get<Component::Connectivity>(elem_entity);
            for (auto node_entity : conn.nodes) {
                if (!registry.valid(node_entity) || !registry.all_of<Component::NodeID>(node_entity)) continue;
                const int nid = registry.get<Component::NodeID>(node_entity).value;

                auto it = nid_to_elems.find(nid);
                const std::vector<int>* using_elements = (it == nid_to_elems.end()) ? nullptr : &it->second;
                bool is_shared = false;

                if (using_elements != nullptr) {
                    for (int user_eid : *using_elements) {
                        if (deleted_eids_set.find(user_eid) == deleted_eids_set.end()) {
                            is_shared = true;
                            break;
                        }
                    }
                }

                if (!is_shared) {
                    nodes_to_delete.push_back(node_entity);
                }
            }
        }

        std::sort(nodes_to_delete.begin(), nodes_to_delete.end());
        nodes_to_delete.erase(std::unique(nodes_to_delete.begin(), nodes_to_delete.end()), nodes_to_delete.end());
        spdlog::info(" -> Found {} orphan nodes to delete (shared nodes preserved).", nodes_to_delete.size());

        // 4. 删除 Part 关联的主 ElementSet（减少悬空句柄）
        const auto& part_comp = registry.get<Component::SimdroidPart>(part_entity);
        if (registry.valid(part_comp.element_set)) {
            registry.destroy(part_comp.element_set);
        }

        // 5. 执行物理删除
        for (auto entity : nodes_to_delete) {
            if (registry.valid(entity)) registry.destroy(entity);
        }
        for (auto entity : elements_to_delete) {
            if (registry.valid(entity)) registry.destroy(entity);
        }
        if (registry.valid(part_entity)) {
            registry.destroy(part_entity);
        }

        // 6. 清理失效的交互定义 (Contact, Constraints)
        {
            auto set_has_any_valid_member = [&](entt::entity set_entity) -> bool {
                if (!registry.valid(set_entity)) return false;
                if (registry.all_of<Component::NodeSetMembers>(set_entity)) {
                    const auto& mem = registry.get<Component::NodeSetMembers>(set_entity).members;
                    for (auto e : mem) if (registry.valid(e)) return true;
                    return false;
                }
                if (registry.all_of<Component::ElementSetMembers>(set_entity)) {
                    const auto& mem = registry.get<Component::ElementSetMembers>(set_entity).members;
                    for (auto e : mem) if (registry.valid(e)) return true;
                    return false;
                }
                if (registry.all_of<Component::SurfaceSetMembers>(set_entity)) {
                    const auto& mem = registry.get<Component::SurfaceSetMembers>(set_entity).members;
                    for (auto e : mem) if (registry.valid(e)) return true;
                    return false;
                }
                // 不是 Set 类型（例如具体的 Surface/Node/Element 实体），只要实体还在就认为有效
                return registry.valid(set_entity);
            };

            // Contacts
            auto view_contacts = registry.view<const Component::ContactDefinition>();
            std::vector<entt::entity> contacts_to_remove;
            for (auto entity : view_contacts) {
                const auto& contact = view_contacts.get<const Component::ContactDefinition>(entity);
                if (!registry.valid(contact.master_entity) || !registry.valid(contact.slave_entity)
                    || !set_has_any_valid_member(contact.master_entity) || !set_has_any_valid_member(contact.slave_entity)) {
                    contacts_to_remove.push_back(entity);
                }
            }
            for (auto e : contacts_to_remove) if (registry.valid(e)) registry.destroy(e);
            if (!contacts_to_remove.empty()) {
                spdlog::info(" -> Removed {} invalidated contact definitions.", contacts_to_remove.size());
            }

            // Rigid body / MPC constraints
            auto view_rb = registry.view<const Component::RigidBodyConstraint>();
            std::vector<entt::entity> rb_to_remove;
            for (auto entity : view_rb) {
                const auto& rb = view_rb.get<const Component::RigidBodyConstraint>(entity);
                if (!registry.valid(rb.master_node_set) || !registry.valid(rb.slave_node_set)
                    || !set_has_any_valid_member(rb.master_node_set) || !set_has_any_valid_member(rb.slave_node_set)) {
                    rb_to_remove.push_back(entity);
                }
            }
            for (auto e : rb_to_remove) if (registry.valid(e)) registry.destroy(e);
            if (!rb_to_remove.empty()) {
                spdlog::info(" -> Removed {} invalidated rigid body constraints.", rb_to_remove.size());
            }
        }

        // 7. 删除后索引失效：清空，等待外部重建
        clear();
        return true;
    }

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