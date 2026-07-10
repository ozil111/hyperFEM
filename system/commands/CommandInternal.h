/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once

#include "AppSession.h"
#include <sstream>
#include <string>

// Internal declarations for the command processor module.
// CommandProcessor.cpp (public entry) and the per-group handler .cpp files
// share these helpers and handler prototypes.
namespace cmd {

namespace detail {

// Basic lookup helpers: nodes / elements / sets / materials / properties
entt::entity find_node_by_id(entt::registry& registry, int nid);
entt::entity find_element_by_id(entt::registry& registry, int eid);
entt::entity find_set_by_name(entt::registry& registry, const std::string& name);
entt::entity find_material_by_id(entt::registry& registry, int mid);
entt::entity find_property_by_id(entt::registry& registry, int pid);

int allocate_next_node_id(entt::registry& registry);
int allocate_next_element_id(entt::registry& registry);
int allocate_next_nodeset_id(entt::registry& registry);
int allocate_next_eleset_id(entt::registry& registry);

entt::entity get_or_create_set_entity(entt::registry& registry, const std::string& name);

[[maybe_unused]] int infer_element_type_from_node_count(std::size_t count);

void invalidate_topology_if_needed(AppSession& session);
void rebuild_inspector_if_mesh_loaded(AppSession& session);

// Argument parsing helpers
std::string parse_next_arg(std::stringstream& ss);
std::string read_rest_of_line(std::stringstream& ss);

} // namespace detail

// Command handlers. Each handler corresponds one-to-one to a command branch in
// the original process_command() if-else chain; the body is preserved verbatim.
// Signature: read args from `ss`, operate on `session`.
namespace io {
void handle_import(std::stringstream& ss, AppSession& session);
void handle_json_apply(std::stringstream& ss, AppSession& session);
void handle_import_simdroid(std::stringstream& ss, AppSession& session);
void handle_export_simdroid(std::stringstream& ss, AppSession& session);
void handle_export_abaqus(std::stringstream& ss, AppSession& session);
void handle_save(std::stringstream& ss, AppSession& session);
void handle_info(std::stringstream& ss, AppSession& session);
} // namespace io

namespace topology {
void handle_build_topology(std::stringstream& ss, AppSession& session);
void handle_list_bodies(std::stringstream& ss, AppSession& session);
void handle_show_body(std::stringstream& ss, AppSession& session);
void handle_list_parts(std::stringstream& ss, AppSession& session);
void handle_delete_part(std::stringstream& ss, AppSession& session);
void handle_graph(std::stringstream& ss, AppSession& session);
void handle_remesh_plan(std::stringstream& ss, AppSession& session);
void handle_remesh_generate(std::stringstream& ss, AppSession& session);
void handle_validate_constraints(std::stringstream& ss, AppSession& session);
void handle_list_constraint_warnings(std::stringstream& ss, AppSession& session);
} // namespace topology

namespace edit {
void handle_node_add(std::stringstream& ss, AppSession& session);
void handle_node_move(std::stringstream& ss, AppSession& session);
void handle_nset_move(std::stringstream& ss, AppSession& session);
void handle_node_delete(std::stringstream& ss, AppSession& session);
void handle_elem_add(std::stringstream& ss, AppSession& session);
void handle_elem_delete(std::stringstream& ss, AppSession& session);
void handle_list_sets(std::stringstream& ss, AppSession& session);
void handle_set_info(std::stringstream& ss, AppSession& session);
void handle_set_addnode(std::stringstream& ss, AppSession& session);
void handle_set_addelem(std::stringstream& ss, AppSession& session);
void handle_set_removenode(std::stringstream& ss, AppSession& session);
void handle_set_removeelem(std::stringstream& ss, AppSession& session);
void handle_create_set(std::stringstream& ss, AppSession& session);
} // namespace edit

namespace property {
void handle_set_material(std::stringstream& ss, AppSession& session);
void handle_set_section(std::stringstream& ss, AppSession& session);
} // namespace property

namespace inspect {
void handle_list_nodes(std::stringstream& ss, AppSession& session);
void handle_list_elements(std::stringstream& ss, AppSession& session);
void handle_node(std::stringstream& ss, AppSession& session);
void handle_elem(std::stringstream& ss, AppSession& session);
void handle_panel(std::stringstream& ss, AppSession& session);
} // namespace inspect

} // namespace cmd
