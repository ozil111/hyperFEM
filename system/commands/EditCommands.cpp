/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include <spdlog/spdlog.h>
#include "components/mesh_components.h"
#include <algorithm>
#include <vector>
#include <unordered_set>
#include <string>

namespace cmd::edit {

using namespace cmd::detail;

// =======================================================
// Basic node / element operations
// =======================================================
void handle_node_add(std::stringstream& ss, AppSession& session) {
    double x, y, z;
    if (!(ss >> x >> y >> z)) {
        spdlog::error("Usage: node_add <x> <y> <z>");
        return;
    }
    auto& registry = session.data.registry;
    int nid = allocate_next_node_id(registry);
    auto e = registry.create();
    registry.emplace<Component::Position>(e, x, y, z);
    registry.emplace<Component::NodeID>(e, nid);
    registry.emplace<Component::OriginalID>(e, nid);
    spdlog::info("Node {} created at ({}, {}, {}).", nid, x, y, z);
    session.mesh_loaded = true;
    invalidate_topology_if_needed(session);
    rebuild_inspector_if_mesh_loaded(session);
}

void handle_node_move(std::stringstream& ss, AppSession& session) {
    int nid;
    double x, y, z;
    if (!(ss >> nid >> x >> y >> z)) {
        spdlog::error("Usage: node_move <nid> <x> <y> <z>");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity e = find_node_by_id(registry, nid);
    if (e == entt::null) {
        spdlog::error("Node {} not found.", nid);
        return;
    }
    auto& pos = registry.get<Component::Position>(e);
    pos.x = x;
    pos.y = y;
    pos.z = z;
    spdlog::info("Node {} moved to ({}, {}, {}).", nid, x, y, z);
    session.mesh_loaded = true;
    // Topology unchanged (coordinates only); no topology / inspector rebuild needed
}

void handle_nset_move(std::stringstream& ss, AppSession& session) {
    std::string set_name;
    double dx, dy, dz;
    if (!(ss >> set_name >> dx >> dy >> dz)) {
        spdlog::error("Usage: nset_move <set_name> <dx> <dy> <dz>");
        spdlog::info("  Moves all nodes in the named node set by a relative offset (dx, dy, dz).");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = find_set_by_name(registry, set_name);
    if (set_e == entt::null) {
        spdlog::error("Set '{}' not found.", set_name);
        return;
    }
    if (!registry.all_of<Component::NodeSetMembers>(set_e)) {
        spdlog::error("Set '{}' is not a node set.", set_name);
        return;
    }
    const auto& mem = registry.get<Component::NodeSetMembers>(set_e).members;
    std::size_t moved = 0;
    for (auto ne : mem) {
        if (!registry.valid(ne) || !registry.all_of<Component::Position>(ne)) continue;
        auto& pos = registry.get<Component::Position>(ne);
        pos.x += dx;
        pos.y += dy;
        pos.z += dz;
        ++moved;
    }
    spdlog::info("nset_move '{}': moved {} nodes by ({}, {}, {}).", set_name, moved, dx, dy, dz);
    // Topology unchanged (coordinates only); no topology / inspector rebuild needed
}

void handle_node_delete(std::stringstream& ss, AppSession& session) {
    int nid;
    if (!(ss >> nid)) {
        spdlog::error("Usage: node_delete <nid>");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity node_e = find_node_by_id(registry, nid);
    if (node_e == entt::null) {
        spdlog::error("Node {} not found.", nid);
        return;
    }
    // Safety: refuse delete if the node is still referenced by any element
    auto view_elems = registry.view<const Component::ElementID, const Component::Connectivity>();
    for (auto elem_e : view_elems) {
        const auto& conn = view_elems.get<const Component::Connectivity>(elem_e);
        if (std::find(conn.nodes.begin(), conn.nodes.end(), node_e) != conn.nodes.end()) {
            int eid = view_elems.get<const Component::ElementID>(elem_e).value;
            spdlog::error("Cannot delete node {}: used by element {}.", nid, eid);
            return;
        }
    }
    registry.destroy(node_e);
    spdlog::info("Node {} deleted.", nid);
    session.mesh_loaded = true;
    invalidate_topology_if_needed(session);
    rebuild_inspector_if_mesh_loaded(session);
}

void handle_elem_add(std::stringstream& ss, AppSession& session) {
    int type_id;
    if (!(ss >> type_id)) {
        spdlog::error("Usage: elem_add <typeid> <nid1> <nid2> ...");
        return;
    }
    if (type_id <= 0) {
        spdlog::error("elem_add requires a positive typeid.");
        return;
    }
    std::vector<int> node_ids;
    for (int nid; ss >> nid; ) {
        node_ids.push_back(nid);
    }
    if (node_ids.size() < 2) {
        spdlog::error("elem_add requires at least 2 node IDs.");
        return;
    }
    auto& registry = session.data.registry;
    int eid = allocate_next_element_id(registry);
    std::vector<entt::entity> node_entities;
    node_entities.reserve(node_ids.size());
    for (int nid : node_ids) {
        entt::entity ne = find_node_by_id(registry, nid);
        if (ne == entt::null) {
            spdlog::error("Node {} not found. Aborting elem_add.", nid);
            return;
        }
        node_entities.push_back(ne);
    }
    auto e = registry.create();
    registry.emplace<Component::ElementID>(e, eid);
    registry.emplace<Component::OriginalID>(e, eid);
    registry.emplace<Component::ElementType>(e, type_id);
    auto& conn = registry.emplace<Component::Connectivity>(e);
    conn.nodes = std::move(node_entities);
    spdlog::info("Element {} created with {} nodes (type_id={}).", eid, conn.nodes.size(), type_id);
    session.mesh_loaded = true;
    invalidate_topology_if_needed(session);
    rebuild_inspector_if_mesh_loaded(session);
}

void handle_elem_delete(std::stringstream& ss, AppSession& session) {
    int eid;
    if (!(ss >> eid)) {
        spdlog::error("Usage: elem_delete <eid>");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity elem_e = find_element_by_id(registry, eid);
    if (elem_e == entt::null) {
        spdlog::error("Element {} not found.", eid);
        return;
    }
    // Remove Surfaces tied to this element
    {
        std::vector<entt::entity> surfaces_to_delete;
        auto surf_view = registry.view<const Component::SurfaceParentElement>();
        for (auto se : surf_view) {
            const auto& pe = surf_view.get<const Component::SurfaceParentElement>(se).element;
            if (pe == elem_e) {
                surfaces_to_delete.push_back(se);
            }
        }
        if (!surfaces_to_delete.empty()) {
            // Remove from all SurfaceSetMembers
            std::unordered_set<entt::entity> surf_set(surfaces_to_delete.begin(), surfaces_to_delete.end());
            auto sset_view = registry.view<Component::SurfaceSetMembers>();
            for (auto set_e : sset_view) {
                auto& mem = registry.get<Component::SurfaceSetMembers>(set_e).members;
                mem.erase(
                    std::remove_if(mem.begin(), mem.end(), [&](entt::entity x) {
                        return surf_set.find(x) != surf_set.end();
                    }),
                    mem.end()
                );
            }
            for (auto se : surfaces_to_delete) {
                if (registry.valid(se)) registry.destroy(se);
            }
        }
    }
    // Remove this element from all ElementSetMembers
    {
        auto eset_view = registry.view<Component::ElementSetMembers>();
        for (auto set_e : eset_view) {
            auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
            mem.erase(
                std::remove(mem.begin(), mem.end(), elem_e),
                mem.end()
            );
        }
    }
    registry.destroy(elem_e);
    spdlog::info("Element {} deleted.", eid);
    session.mesh_loaded = true;
    invalidate_topology_if_needed(session);
    rebuild_inspector_if_mesh_loaded(session);
}

// =======================================================
// Basic set operations
// =======================================================
void handle_list_sets(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::warn("No mesh loaded.");
        return;
    }
    auto& registry = session.data.registry;
    auto view = registry.view<const Component::SetName>();
    spdlog::info("Sets (total {}):", view.size());
    for (auto e : view) {
        const auto& name = view.get<const Component::SetName>(e).value;
        bool has_node = registry.all_of<Component::NodeSetMembers>(e);
        bool has_elem = registry.all_of<Component::ElementSetMembers>(e);
        bool has_surf = registry.all_of<Component::SurfaceSetMembers>(e);
        std::string type = "generic";
        std::size_t count = 0;
        if (has_node && !has_elem && !has_surf) {
            type = "node";
            count = registry.get<Component::NodeSetMembers>(e).members.size();
        } else if (!has_node && has_elem && !has_surf) {
            type = "element";
            count = registry.get<Component::ElementSetMembers>(e).members.size();
        } else if (!has_node && !has_elem && has_surf) {
            type = "surface";
            count = registry.get<Component::SurfaceSetMembers>(e).members.size();
        } else if (has_node || has_elem || has_surf) {
            type = "mixed";
        }
        spdlog::info("  - {} (type={}, size={})", name, type, count);
    }
}

void handle_set_info(std::stringstream& ss, AppSession& session) {
    if (!session.mesh_loaded) {
        spdlog::warn("No mesh loaded.");
        return;
    }
    std::string set_name;
    ss >> set_name;
    if (set_name.empty()) {
        spdlog::error("Usage: set_info <set_name>");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = find_set_by_name(registry, set_name);
    if (set_e == entt::null) {
        spdlog::error("Set '{}' not found.", set_name);
        return;
    }
    spdlog::info("Set '{}':", set_name);
    if (registry.all_of<Component::NodeSetMembers>(set_e)) {
        const auto& mem = registry.get<Component::NodeSetMembers>(set_e).members;
        spdlog::info("  Node members ({}):", mem.size());
        std::string line;
        int printed = 0;
        for (auto n : mem) {
            if (!registry.valid(n) || !registry.all_of<Component::NodeID>(n)) continue;
            int nid = registry.get<Component::NodeID>(n).value;
            if (!line.empty()) line += ", ";
            line += std::to_string(nid);
            ++printed;
            if (printed >= 32) {
                spdlog::info("    {}", line);
                line.clear();
                printed = 0;
            }
        }
        if (!line.empty()) spdlog::info("    {}", line);
    }
    if (registry.all_of<Component::ElementSetMembers>(set_e)) {
        const auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
        spdlog::info("  Element members ({}):", mem.size());
        std::string line;
        int printed = 0;
        for (auto el : mem) {
            if (!registry.valid(el) || !registry.all_of<Component::ElementID>(el)) continue;
            int eid = registry.get<Component::ElementID>(el).value;
            if (!line.empty()) line += ", ";
            line += std::to_string(eid);
            ++printed;
            if (printed >= 32) {
                spdlog::info("    {}", line);
                line.clear();
                printed = 0;
            }
        }
        if (!line.empty()) spdlog::info("    {}", line);
    }
}

void handle_set_addnode(std::stringstream& ss, AppSession& session) {
    std::string set_name;
    ss >> set_name;
    if (set_name.empty()) {
        spdlog::error("Usage: set_addnode <set_name> <nid1> [nid2 ...]");
        return;
    }
    std::vector<int> node_ids;
    for (int nid; ss >> nid; ) {
        node_ids.push_back(nid);
    }
    if (node_ids.empty()) {
        spdlog::error("set_addnode requires at least one node id.");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = get_or_create_set_entity(registry, set_name);
    auto& mem = registry.get_or_emplace<Component::NodeSetMembers>(set_e);
    std::size_t added = 0;
    for (int nid : node_ids) {
        entt::entity ne = find_node_by_id(registry, nid);
        if (ne == entt::null) {
            spdlog::warn("Node {} not found. Skipped in set_addnode.", nid);
            continue;
        }
        if (std::find(mem.members.begin(), mem.members.end(), ne) == mem.members.end()) {
            mem.members.push_back(ne);
            ++added;
        }
    }
    spdlog::info("set_addnode '{}' : added {} nodes.", set_name, added);
}

void handle_set_addelem(std::stringstream& ss, AppSession& session) {
    std::string set_name;
    ss >> set_name;
    if (set_name.empty()) {
        spdlog::error("Usage: set_addelem <set_name> <eid1> [eid2 ...]");
        return;
    }
    std::vector<int> elem_ids;
    for (int eid; ss >> eid; ) {
        elem_ids.push_back(eid);
    }
    if (elem_ids.empty()) {
        spdlog::error("set_addelem requires at least one element id.");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = get_or_create_set_entity(registry, set_name);
    auto& mem = registry.get_or_emplace<Component::ElementSetMembers>(set_e);
    std::size_t added = 0;
    for (int eid : elem_ids) {
        entt::entity ee = find_element_by_id(registry, eid);
        if (ee == entt::null) {
            spdlog::warn("Element {} not found. Skipped in set_addelem.", eid);
            continue;
        }
        if (std::find(mem.members.begin(), mem.members.end(), ee) == mem.members.end()) {
            mem.members.push_back(ee);
            ++added;
        }
    }
    spdlog::info("set_addelem '{}' : added {} elements.", set_name, added);
}

void handle_set_removenode(std::stringstream& ss, AppSession& session) {
    std::string set_name;
    ss >> set_name;
    if (set_name.empty()) {
        spdlog::error("Usage: set_removenode <set_name> <nid1> [nid2 ...]");
        return;
    }
    std::vector<int> node_ids;
    for (int nid; ss >> nid; ) {
        node_ids.push_back(nid);
    }
    if (node_ids.empty()) {
        spdlog::error("set_removenode requires at least one node id.");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = find_set_by_name(registry, set_name);
    if (set_e == entt::null || !registry.all_of<Component::NodeSetMembers>(set_e)) {
        spdlog::error("Node set '{}' not found.", set_name);
        return;
    }
    auto& mem = registry.get<Component::NodeSetMembers>(set_e).members;
    std::size_t removed = 0;
    for (int nid : node_ids) {
        entt::entity ne = find_node_by_id(registry, nid);
        if (ne == entt::null) continue;
        auto it = std::remove(mem.begin(), mem.end(), ne);
        if (it != mem.end()) {
            removed += static_cast<std::size_t>(mem.end() - it);
            mem.erase(it, mem.end());
        }
    }
    spdlog::info("set_removenode '{}' : removed {} entries.", set_name, removed);
}

void handle_set_removeelem(std::stringstream& ss, AppSession& session) {
    std::string set_name;
    ss >> set_name;
    if (set_name.empty()) {
        spdlog::error("Usage: set_removeelem <set_name> <eid1> [eid2 ...]");
        return;
    }
    std::vector<int> elem_ids;
    for (int eid; ss >> eid; ) {
        elem_ids.push_back(eid);
    }
    if (elem_ids.empty()) {
        spdlog::error("set_removeelem requires at least one element id.");
        return;
    }
    auto& registry = session.data.registry;
    entt::entity set_e = find_set_by_name(registry, set_name);
    if (set_e == entt::null || !registry.all_of<Component::ElementSetMembers>(set_e)) {
        spdlog::error("Element set '{}' not found.", set_name);
        return;
    }
    auto& mem = registry.get<Component::ElementSetMembers>(set_e).members;
    std::size_t removed = 0;
    for (int eid : elem_ids) {
        entt::entity ee = find_element_by_id(registry, eid);
        if (ee == entt::null) continue;
        auto it = std::remove(mem.begin(), mem.end(), ee);
        if (it != mem.end()) {
            removed += static_cast<std::size_t>(mem.end() - it);
            mem.erase(it, mem.end());
        }
    }
    spdlog::info("set_removeelem '{}' : removed {} entries.", set_name, removed);
}

void handle_create_set(std::stringstream& ss, AppSession& session) {
    std::string set_name, type_str;
    if (!(ss >> set_name >> type_str)) {
        spdlog::error("Usage: create_set <set_name> <node|element> [id1 id2 ...]");
        return;
    }
    auto& registry = session.data.registry;
    // 同名 set 已存在则报错
    if (find_set_by_name(registry, set_name) != entt::null) {
        spdlog::error("Set '{}' already exists.", set_name);
        return;
    }
    auto e = registry.create();
    registry.emplace<Component::SetName>(e, set_name);

    if (type_str == "node") {
        int nsid = allocate_next_nodeset_id(registry);
        registry.emplace<Component::NodeSetID>(e, nsid);
        auto& mem = registry.emplace<Component::NodeSetMembers>(e);
        std::size_t added = 0;
        for (int nid; ss >> nid; ) {
            entt::entity ne = find_node_by_id(registry, nid);
            if (ne == entt::null) {
                spdlog::warn("Node {} not found. Skipped in create_set.", nid);
                continue;
            }
            if (std::find(mem.members.begin(), mem.members.end(), ne) == mem.members.end()) {
                mem.members.push_back(ne);
                ++added;
            }
        }
        spdlog::info("Created node set '{}' (nsid={}) with {} members.", set_name, nsid, added);
    } else if (type_str == "element") {
        int esid = allocate_next_eleset_id(registry);
        registry.emplace<Component::EleSetID>(e, esid);
        auto& mem = registry.emplace<Component::ElementSetMembers>(e);
        std::size_t added = 0;
        for (int eid; ss >> eid; ) {
            entt::entity ee = find_element_by_id(registry, eid);
            if (ee == entt::null) {
                spdlog::warn("Element {} not found. Skipped in create_set.", eid);
                continue;
            }
            if (std::find(mem.members.begin(), mem.members.end(), ee) == mem.members.end()) {
                mem.members.push_back(ee);
                ++added;
            }
        }
        spdlog::info("Created element set '{}' (esid={}) with {} members.", set_name, esid, added);
    } else {
        registry.destroy(e);
        spdlog::error("Unknown set type '{}'. Use 'node' or 'element'.", type_str);
        return;
    }
}

} // namespace cmd::edit
