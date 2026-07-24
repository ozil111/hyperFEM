// system/output/VtkExporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 */
#include "output/VtkExporter.h"
#include "DataContext.h"
#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>

namespace {

int toVtkCellType(int type_id) {
    switch (type_id) {
        case 304: return 10;   // Tetra4  -> VTK_TETRA
        case 306: return 13;   // Penta6  -> VTK_WEDGE
        case 308: return 12;   // Hexa8   -> VTK_HEXAHEDRON
        case 310: return 24;   // Tetra10 -> VTK_QUADRATIC_TETRA
        case 320: return 25;   // Hexa20  -> VTK_QUADRATIC_HEXAHEDRON
        default:  return 12;
    }
}

int nodesPerCell(int type_id) {
    switch (type_id) {
        case 304: return 4;
        case 306: return 6;
        case 308: return 8;
        case 310: return 10;
        case 320: return 20;
        default:  return 8;
    }
}

} // namespace

bool VtkExporter::save(const std::string& filepath, const DataContext& data_context, entt::entity /*output_entity*/) {
    const auto& registry = data_context.registry;
    auto pos_view = registry.view<Component::Position>();
    auto cell_view = registry.view<Component::Connectivity, Component::ElementType>();

    const size_t num_points = pos_view.size();
    const size_t num_cells  = cell_view.size_hint();
    if (num_points == 0 || num_cells == 0) {
        spdlog::warn("VtkExporter: no points or no cells, skip.");
        return false;
    }

    // Build stable node ordering
    std::vector<entt::entity> node_entities;
    node_entities.reserve(num_points);
    for (auto e : pos_view)
        node_entities.push_back(e);

    std::unordered_map<entt::entity, size_t> entity_to_index;
    entity_to_index.reserve(node_entities.size());
    for (size_t i = 0; i < node_entities.size(); ++i)
        entity_to_index[node_entities[i]] = i;

    std::ofstream out(filepath);
    if (!out.is_open()) {
        spdlog::error("VtkExporter could not open file: {}", filepath);
        return false;
    }

    // Header
    out << "# vtk DataFile Version 3.0\n";
    out << "NovaFEA legacy VTK output\n";
    out << "ASCII\n";
    out << "DATASET UNSTRUCTURED_GRID\n";

    // Points
    out << "POINTS " << num_points << " float\n";
    for (entt::entity e : node_entities) {
        const auto& p = registry.get<Component::Position>(e);
        out << p.x << ' ' << p.y << ' ' << p.z << '\n';
    }

    // Cells: connectivity + offsets + types
    // First pass: compute total connectivity size
    int conn_size = 0;
    std::vector<int> offsets;
    std::vector<int> types;
    std::stringstream conn_ss;
    for (auto cell_entity : cell_view) {
        const auto& conn = cell_view.get<Component::Connectivity>(cell_entity);
        const auto& etype = cell_view.get<Component::ElementType>(cell_entity);
        int npc = nodesPerCell(etype.type_id);
        conn_ss << npc;
        for (entt::entity node_entity : conn.nodes) {
            auto it = entity_to_index.find(node_entity);
            if (it != entity_to_index.end())
                conn_ss << ' ' << it->second;
        }
        conn_ss << '\n';
        conn_size += npc + 1;
        offsets.push_back(conn_size);
        types.push_back(toVtkCellType(etype.type_id));
    }

    out << "\nCELLS " << num_cells << ' ' << conn_size << '\n';
    out << conn_ss.str();
    out << "\nCELL_TYPES " << num_cells << '\n';
    for (size_t i = 0; i < types.size(); ++i) {
        out << types[i] << '\n';
    }

    // PointData: Displacement vector
    out << "\nPOINT_DATA " << num_points << '\n';
    bool has_disp = false;
    for (entt::entity e : node_entities) {
        if (registry.all_of<Component::Displacement>(e)) {
            has_disp = true;
            break;
        }
    }
    if (has_disp) {
        out << "VECTORS Displacement float\n";
        for (entt::entity e : node_entities) {
            if (registry.all_of<Component::Displacement>(e)) {
                const auto& d = registry.get<Component::Displacement>(e);
                out << d.dx << ' ' << d.dy << ' ' << d.dz << '\n';
            } else {
                out << "0 0 0\n";
            }
        }
    }

    spdlog::info("VtkExporter wrote: {}", filepath);
    return true;
}
