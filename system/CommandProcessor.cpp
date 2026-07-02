/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "CommandProcessor.h"
#include "commands/CommandInternal.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

void process_command(const std::string& command_line, AppSession& session) {
    std::stringstream ss(command_line);
    std::string command;
    ss >> command;

    if (command == "quit" || command == "exit") {
        session.is_running = false;
        spdlog::info("Exiting NovaFEA. Goodbye!");
    }
    else if (command == "help") {
        spdlog::info("Available commands: import, import_simdroid, export_simdroid, export_abaqus, json_apply, "
                     "info, build_topology, list_bodies, show_body, "
                     "list_parts, delete_part, graph, remesh_plan, remesh_generate, validate_constraints, list_constraint_warnings, "
                     "panel node <nid>, panel elem <eid>, panel part <name>, panel set <name>, "
                     "node, list_nodes, node_add, node_move, nset_move, node_delete, "
                     "elem, elem_add, elem_delete, "
                     "list_elements, "
                     "list_sets, set_info, set_addnode, set_addelem, set_removenode, set_removeelem, "
                     "set_material <mid> <component> <param> <value>, "
                     "set_section <sid> <type> <param> <value>, "
                     "save, help, quit");
    }
    else if (command == "import") {
        cmd::io::handle_import(ss, session);
    }
    else if (command == "json_apply") {
        cmd::io::handle_json_apply(ss, session);
    }
    else if (command == "import_simdroid") {
        cmd::io::handle_import_simdroid(ss, session);
    }
    else if (command == "export_simdroid") {
        cmd::io::handle_export_simdroid(ss, session);
    }
    else if (command == "export_abaqus") {
        cmd::io::handle_export_abaqus(ss, session);
    }
    else if (command == "build_topology") {
        cmd::topology::handle_build_topology(ss, session);
    }
    else if (command == "list_bodies") {
        cmd::topology::handle_list_bodies(ss, session);
    }
    else if (command == "show_body") {
        cmd::topology::handle_show_body(ss, session);
    }
    else if (command == "save") {
        cmd::io::handle_save(ss, session);
    }
    else if (command == "info") {
        cmd::io::handle_info(ss, session);
    }
    else if (command == "list_parts") {
        cmd::topology::handle_list_parts(ss, session);
    }
    else if (command == "delete_part") {
        cmd::topology::handle_delete_part(ss, session);
    }
    else if (command == "graph") {
        cmd::topology::handle_graph(ss, session);
    }
    else if (command == "remesh_plan") {
        cmd::topology::handle_remesh_plan(ss, session);
    }
    else if (command == "remesh_generate") {
        cmd::topology::handle_remesh_generate(ss, session);
    }
    else if (command == "validate_constraints") {
        cmd::topology::handle_validate_constraints(ss, session);
    }
    else if (command == "list_constraint_warnings") {
        cmd::topology::handle_list_constraint_warnings(ss, session);
    }
    else if (command == "node_add") {
        cmd::edit::handle_node_add(ss, session);
    }
    else if (command == "node_move") {
        cmd::edit::handle_node_move(ss, session);
    }
    else if (command == "nset_move") {
        cmd::edit::handle_nset_move(ss, session);
    }
    else if (command == "node_delete") {
        cmd::edit::handle_node_delete(ss, session);
    }
    else if (command == "elem_add") {
        cmd::edit::handle_elem_add(ss, session);
    }
    else if (command == "elem_delete") {
        cmd::edit::handle_elem_delete(ss, session);
    }
    else if (command == "list_sets") {
        cmd::edit::handle_list_sets(ss, session);
    }
    else if (command == "set_info") {
        cmd::edit::handle_set_info(ss, session);
    }
    else if (command == "set_addnode") {
        cmd::edit::handle_set_addnode(ss, session);
    }
    else if (command == "set_addelem") {
        cmd::edit::handle_set_addelem(ss, session);
    }
    else if (command == "set_removenode") {
        cmd::edit::handle_set_removenode(ss, session);
    }
    else if (command == "set_removeelem") {
        cmd::edit::handle_set_removeelem(ss, session);
    }
    else if (command == "set_material") {
        cmd::property::handle_set_material(ss, session);
    }
    else if (command == "set_section") {
        cmd::property::handle_set_section(ss, session);
    }
    else if (command == "list_nodes") {
        cmd::inspect::handle_list_nodes(ss, session);
    }
    else if (command == "list_elements") {
        cmd::inspect::handle_list_elements(ss, session);
    }
    else if (command == "node") {
        cmd::inspect::handle_node(ss, session);
    }
    else if (command == "elem" || command == "element") {
        cmd::inspect::handle_elem(ss, session);
    }
    else if (command == "panel") {
        cmd::inspect::handle_panel(ss, session);
    }
    else {
        spdlog::warn("Unknown command: '{}'. Type 'help' for a list of commands.", command);
    }
}
