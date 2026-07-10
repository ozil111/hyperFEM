/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "TopologyData.h"
#include <algorithm>
#include <cstddef>

namespace cmd::detail {

// Basic lookup helpers: nodes / elements / sets
entt::entity find_node_by_id(entt::registry& registry, int nid) {
    auto view = registry.view<const Component::NodeID>();
    for (auto e : view) {
        if (view.get<const Component::NodeID>(e).value == nid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_element_by_id(entt::registry& registry, int eid) {
    auto view = registry.view<const Component::ElementID>();
    for (auto e : view) {
        if (view.get<const Component::ElementID>(e).value == eid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_set_by_name(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto e : view) {
        if (view.get<const Component::SetName>(e).value == name) {
            return e;
        }
    }
    return entt::null;
}

int allocate_next_node_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::NodeID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::NodeID>(e).value);
    }
    return max_id + 1;
}

int allocate_next_element_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::ElementID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::ElementID>(e).value);
    }
    return max_id + 1;
}

int allocate_next_nodeset_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::NodeSetID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::NodeSetID>(e).value);
    }
    return max_id + 1;
}

int allocate_next_eleset_id(entt::registry& registry) {
    int max_id = 0;
    auto view = registry.view<const Component::EleSetID>();
    for (auto e : view) {
        max_id = std::max(max_id, view.get<const Component::EleSetID>(e).value);
    }
    return max_id + 1;
}

entt::entity find_material_by_id(entt::registry& registry, int mid) {
    auto view = registry.view<const Component::MaterialID>();
    for (auto e : view) {
        if (view.get<const Component::MaterialID>(e).value == mid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity find_property_by_id(entt::registry& registry, int pid) {
    auto view = registry.view<const Component::PropertyID>();
    for (auto e : view) {
        if (view.get<const Component::PropertyID>(e).value == pid) {
            return e;
        }
    }
    return entt::null;
}

entt::entity get_or_create_set_entity(entt::registry& registry, const std::string& name) {
    entt::entity e = find_set_by_name(registry, name);
    if (e != entt::null) return e;
    e = registry.create();
    registry.emplace<Component::SetName>(e, name);
    return e;
}

[[maybe_unused]] int infer_element_type_from_node_count(std::size_t count) {
    if (count == 2) return 102;   // Line2
    if (count == 3) return 203;   // Tri3
    if (count == 4) return 304;   // Tet4 / Quad4, default to volume element
    if (count == 8) return 308;   // Hex8
    if (count == 10) return 310;  // Tet10
    if (count == 20) return 320;  // Hex20
    return 0;
}

void invalidate_topology_if_needed(AppSession& session) {
    auto& registry = session.data.registry;
    if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        registry.ctx().erase<std::unique_ptr<TopologyData>>();
    }
    session.topology_built = false;
}

void rebuild_inspector_if_mesh_loaded(AppSession& session) {
    if (!session.mesh_loaded) return;
    session.inspector.build(session.data.registry);
}

/**
 * Parse the next argument from the stringstream.
 * Supports quoted strings: "path with spaces" or 'path with spaces'.
 * Unquoted tokens are read until whitespace.
 */
std::string parse_next_arg(std::stringstream& ss) {
    ss >> std::ws;           // skip leading whitespace
    if (ss.eof()) return {};

    int ch = ss.peek();
    if (ch == '"' || ch == '\'') {
        // Quoted string — read until matching closing quote
        char quote = static_cast<char>(ch);
        ss.get();            // consume opening quote
        std::string result;
        char c;
        while (ss.get(c)) {
            if (c == quote) break;
            result.push_back(c);
        }
        return result;
    } else {
        // Unquoted — read until whitespace
        std::string result;
        ss >> result;
        return result;
    }
}

/**
 * Read the remainder of the line, trim whitespace, and strip matching
 * outer quotes (single or double). Suitable for commands with a single
 * file-path argument (e.g. import, import_simdroid, save).
 */
std::string read_rest_of_line(std::stringstream& ss) {
    std::string rest;
    std::getline(ss, rest);

    // Trim leading/trailing whitespace
    const std::string whitespace = " \t\r\n";
    size_t start = rest.find_first_not_of(whitespace);
    if (start == std::string::npos) return {};
    size_t end = rest.find_last_not_of(whitespace);
    rest = rest.substr(start, end - start + 1);

    // Strip matching outer single or double quotes
    if (rest.size() >= 2) {
        char first = rest.front();
        char last  = rest.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            rest = rest.substr(1, rest.size() - 2);
        }
    }
    return rest;
}

} // namespace cmd::detail
