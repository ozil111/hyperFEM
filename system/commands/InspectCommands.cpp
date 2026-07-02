/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandInternal.h"
#include <spdlog/spdlog.h>
#include "tui/ComponentTUI.h"
#include <string>

namespace cmd::inspect {

using namespace cmd::detail;

void handle_list_nodes(std::stringstream& ss, AppSession& session) {
    auto& registry = session.data.registry;
    tui::render_nodes_list(registry);
}

void handle_list_elements(std::stringstream& ss, AppSession& session) {
    auto& registry = session.data.registry;
    const int selected_eid = tui::render_elements_list_select(registry);
    if (selected_eid >= 0) {
        session.inspector.inspect_element(registry, selected_eid);
    }
}

void handle_node(std::stringstream& ss, AppSession& session) {
    int nid;
    if (ss >> nid) {
        session.inspector.inspect_node(session.data.registry, nid);
    } else {
        spdlog::error("Usage: node <node_id>");
    }
}

void handle_elem(std::stringstream& ss, AppSession& session) {
    int eid;
    if (ss >> eid) {
        session.inspector.inspect_element(session.data.registry, eid);
    } else {
        spdlog::error("Usage: elem <element_id>");
    }
}

void handle_panel(std::stringstream& ss, AppSession& session) {
    std::string type, id_or_name;
    if (!(ss >> type >> id_or_name)) {
        spdlog::error("Usage: panel <type> <id_or_name>  (type: node|elem|element|part|set)");
        return;
    }
    tui::PanelEntityKind kind = tui::PanelEntityKind::Unknown;
    std::string display_id;
    entt::entity e = tui::resolve_panel_entity(
        session.data.registry, &session.inspector, type, id_or_name, &kind, &display_id);
    if (e == entt::null) {
        spdlog::error("Panel: '{}' '{}' not found. Ensure mesh is loaded and index built.", type, id_or_name);
        return;
    }
    tui::render_panel(session.data.registry, e, &session.inspector, kind, display_id);
}

} // namespace cmd::inspect
