// system/output/CsvExporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 */
#include "output/CsvExporter.h"
#include "DataContext.h"
#include "components/mesh_components.h"
#include "components/simdroid_components.h"
#include "components/material_components.h"
#include "element/c3d8r/C3D8RStiffnessMatrix.h"
#include "TopologyData.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <cmath>
#include <vector>
#include <algorithm>

namespace {

/** Map internal Voigt index [XX,YY,ZZ,XY,YZ,XZ] -> Abaqus order [S11,S22,S33,S12,S13,S23] */
static constexpr int g_abaqus_order[6] = {0, 1, 2, 3, 5, 4};

/** Abaqus component names for stress and strain */
static const char* g_stress_comps[6] = {"S11","S22","S33","S12","S13","S23"};
static const char* g_strain_comps[6] = {"E11","E22","E33","E12","E13","E23"};

static constexpr const char* INSTANCE_NAME = "PART-1-1";

} // namespace

bool CsvExporter::save(const std::string& prefix, const DataContext& data_context) {
    const auto& registry = data_context.registry;

    // ---- Disp CSV ----
    {
        std::string path = prefix + "_disp.csv";
        std::ofstream out(path);
        if (!out.is_open()) {
            spdlog::error("CsvExporter: cannot open {}", path);
            return false;
        }
        out << "instance,node_label,u1,u2,u3,mag\n";

        // Collect nodes with NodeID, sorted by NodeID
        auto node_view = registry.view<Component::NodeID>();
        std::vector<std::pair<int, entt::entity>> sorted_nodes;
        sorted_nodes.reserve(node_view.size());
        for (auto e : node_view) {
            sorted_nodes.emplace_back(registry.get<Component::NodeID>(e).value, e);
        }
        std::sort(sorted_nodes.begin(), sorted_nodes.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (const auto& [nid, entity] : sorted_nodes) {
            double dx = 0.0, dy = 0.0, dz = 0.0;
            if (registry.all_of<Component::Displacement>(entity)) {
                const auto& d = registry.get<Component::Displacement>(entity);
                dx = d.dx;
                dy = d.dy;
                dz = d.dz;
            }
            double mag = std::sqrt(dx * dx + dy * dy + dz * dz);
            out << INSTANCE_NAME << ',' << nid << ','
                << dx << ',' << dy << ',' << dz << ',' << mag << '\n';
        }
        spdlog::info("CsvExporter: wrote {} ({} nodes)", path, sorted_nodes.size());
    }

    // ---- Elements CSV ----
    {
        std::string path = prefix + "_elements.csv";
        std::ofstream out(path);
        if (!out.is_open()) {
            spdlog::error("CsvExporter: cannot open {}", path);
            return false;
        }
        out << "variable,component,location,instance,element_label,node_label,integration_point,value\n";

        // Need TopologyData for D matrix lookup
        if (!registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
            spdlog::warn("CsvExporter: TopologyData not found, skipping elements CSV");
            // Still write empty elements CSV so comparison can proceed
            return true;
        }
        auto& topology = *registry.ctx().get<std::unique_ptr<TopologyData>>();

        // Collect elements with ElementID, sorted
        auto elem_view = registry.view<Component::ElementID, Component::ElementType>();
        std::vector<std::tuple<int, entt::entity, int>> sorted_elems;
        for (auto e : elem_view) {
            int eid = registry.get<Component::ElementID>(e).value;
            int type_id = registry.get<Component::ElementType>(e).type_id;
            sorted_elems.emplace_back(eid, e, type_id);
        }
        std::sort(sorted_elems.begin(), sorted_elems.end(),
                  [](const auto& a, const auto& b) { return std::get<0>(a) < std::get<0>(b); });

        // First pass: compute stress/strain for all C3D8R elements
        // Output matches Abaqus ODB extraction order:
        //   All S components (element-major), then all E components (element-major)
        struct ElemData { int eid; Eigen::Vector<double, 6> stress; Eigen::Vector<double, 6> strain; bool valid; };
        std::vector<ElemData> elem_data;

        for (const auto& [eid, entity, type_id] : sorted_elems) {
            if (type_id != 308) continue;

            ElemData ed;
            ed.eid = eid;
            ed.valid = false;

            if (eid < 0 || static_cast<size_t>(eid) >= topology.element_uid_to_part_map.size())
                { elem_data.push_back(ed); continue; }
            entt::entity part_entity = topology.element_uid_to_part_map[static_cast<size_t>(eid)];
            if (part_entity == entt::null || !registry.all_of<Component::SimdroidPart>(part_entity))
                { elem_data.push_back(ed); continue; }
            entt::entity mat_entity = registry.get<Component::SimdroidPart>(part_entity).material;
            if (!registry.all_of<Component::LinearElasticMatrix>(mat_entity))
                { elem_data.push_back(ed); continue; }
            const auto& mat = registry.get<Component::LinearElasticMatrix>(mat_entity);
            if (!mat.is_initialized) { elem_data.push_back(ed); continue; }

            Eigen::Map<const Eigen::Matrix<double, 6, 6, Eigen::RowMajor>> D(mat.D);

            ed.valid = compute_c3d8r_stress_strain(registry, entity, D, ed.stress, ed.strain);
            elem_data.push_back(ed);
        }

        // Second pass: output in Abaqus order (all S element-major, then all E element-major)
        // Stress: all 6 components for elem1, then elem2, ...
        for (const auto& ed : elem_data) {
            if (!ed.valid) continue;
            for (int c = 0; c < 6; ++c) {
                int idx = g_abaqus_order[c];
                out << "S," << g_stress_comps[c] << ",element," << INSTANCE_NAME
                    << ',' << ed.eid << ",,1," << ed.stress[idx] << '\n';
            }
        }
        // Strain: all 6 components for elem1, then elem2, ...
        for (const auto& ed : elem_data) {
            if (!ed.valid) continue;
            for (int c = 0; c < 6; ++c) {
                int idx = g_abaqus_order[c];
                out << "E," << g_strain_comps[c] << ",element," << INSTANCE_NAME
                    << ',' << ed.eid << ",,1," << ed.strain[idx] << '\n';
            }
        }
        spdlog::info("CsvExporter: wrote {}", path);
    }

    return true;
}
