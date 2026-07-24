/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "exporter_json/JsonExporter.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/simdroid_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"
#include "nlohmann/json.hpp"
#include <spdlog/spdlog.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>

using json = nlohmann::json;

bool JsonExporter::save(const std::string& filepath, const DataContext& data_context) {
    const auto& registry = data_context.registry;
    json j;

    spdlog::info("JsonExporter started for file: {}", filepath);

    // --- 1. Export Materials ---
    j["material"] = json::array();
    auto mat_view = registry.view<Component::MaterialID>();
    for (auto e : mat_view) {
        int mid = registry.get<Component::MaterialID>(e).value;
        json mat_j;
        mat_j["mid"] = mid;
        
        if (registry.all_of<Component::LinearElasticParams>(e)) {
            const auto& params = registry.get<Component::LinearElasticParams>(e);
            mat_j["typeid"] = 1;
            mat_j["rho"] = params.rho;
            mat_j["E"] = params.E;
            mat_j["nu"] = params.nu;
        } else {
            // Default or unknown typeid
            mat_j["typeid"] = 0;
        }
        j["material"].push_back(mat_j);
    }

    // --- 2. Export Properties ---
    j["property"] = json::array();
    auto prop_view = registry.view<Component::PropertyID>();
    
    // We need to find mid for each property. 
    // In JsonParser, mid is linked via SimdroidPart.
    // Let's build a map from property entity to material mid.
    std::unordered_map<entt::entity, int> prop_to_mid;
    auto part_view = registry.view<Component::SimdroidPart>();
    for (auto e : part_view) {
        const auto& part = registry.get<Component::SimdroidPart>(e);
        if (registry.valid(part.section) && registry.valid(part.material) && registry.all_of<Component::MaterialID>(part.material)) {
            prop_to_mid[part.section] = registry.get<Component::MaterialID>(part.material).value;
        }
    }

    for (auto e : prop_view) {
        int pid = registry.get<Component::PropertyID>(e).value;
        json prop_j;
        prop_j["pid"] = pid;
        prop_j["mid"] = prop_to_mid.count(e) ? prop_to_mid[e] : 0; // Default to 0 if not found

        if (registry.all_of<Component::SolidProperty>(e)) {
            const auto& solid = registry.get<Component::SolidProperty>(e);
            prop_j["typeid"] = solid.type_id;
            if (registry.all_of<Component::IntegrationPoints>(e))
                prop_j["integration_network"] = registry.get<Component::IntegrationPoints>(e).value;
            if (registry.all_of<Component::HourglassControl>(e))
                prop_j["hourglass_control"] = registry.get<Component::HourglassControl>(e).value;
        } else {
            prop_j["typeid"] = 0;
        }
        j["property"].push_back(prop_j);
    }

    // --- 3. Export Mesh (Nodes and Elements) ---
    j["mesh"] = json::object();
    j["mesh"]["nodes"] = json::array();
    auto node_view = registry.view<Component::NodeID, Component::Position>();
    for (auto e : node_view) {
        const auto& id = registry.get<Component::NodeID>(e);
        const auto& pos = registry.get<Component::Position>(e);
        json node_j;
        node_j["nid"] = id.value;
        node_j["x"] = pos.x;
        node_j["y"] = pos.y;
        node_j["z"] = pos.z;
        j["mesh"]["nodes"].push_back(node_j);
    }

    j["mesh"]["elements"] = json::array();
    auto elem_view = registry.view<Component::ElementID, Component::ElementType, Component::Connectivity, Component::PropertyRef>();
    for (auto e : elem_view) {
        const auto& id = registry.get<Component::ElementID>(e);
        const auto& type = registry.get<Component::ElementType>(e);
        const auto& conn = registry.get<Component::Connectivity>(e);
        const auto& prop_ref = registry.get<Component::PropertyRef>(e);

        json elem_j;
        elem_j["eid"] = id.value;
        elem_j["etype"] = type.type_id;
        
        if (registry.valid(prop_ref.property_entity) && registry.all_of<Component::PropertyID>(prop_ref.property_entity)) {
            elem_j["pid"] = registry.get<Component::PropertyID>(prop_ref.property_entity).value;
        } else {
            elem_j["pid"] = 0;
        }

        elem_j["nids"] = json::array();
        for (auto node_e : conn.nodes) {
            if (registry.valid(node_e) && registry.all_of<Component::NodeID>(node_e)) {
                elem_j["nids"].push_back(registry.get<Component::NodeID>(node_e).value);
            }
        }
        j["mesh"]["elements"].push_back(elem_j);
    }

    // --- 4. Export NodeSets ---
    j["nodeset"] = json::array();
    auto ns_view = registry.view<Component::NodeSetID, Component::NodeSetMembers>();
    for (auto e : ns_view) {
        int nsid = registry.get<Component::NodeSetID>(e).value;
        const auto& members = registry.get<Component::NodeSetMembers>(e);
        
        json ns_j;
        ns_j["nsid"] = nsid;
        if (registry.all_of<Component::SetName>(e)) {
            ns_j["name"] = registry.get<Component::SetName>(e).value;
        }
        ns_j["nids"] = json::array();
        for (auto node_e : members.members) {
            if (registry.valid(node_e) && registry.all_of<Component::NodeID>(node_e)) {
                ns_j["nids"].push_back(registry.get<Component::NodeID>(node_e).value);
            }
        }
        j["nodeset"].push_back(ns_j);
    }

    // --- 5. Export EleSets ---
    j["eleset"] = json::array();
    auto es_view = registry.view<Component::EleSetID, Component::ElementSetMembers>();
    for (auto e : es_view) {
        int esid = registry.get<Component::EleSetID>(e).value;
        const auto& members = registry.get<Component::ElementSetMembers>(e);
        
        json es_j;
        es_j["esid"] = esid;
        if (registry.all_of<Component::SetName>(e)) {
            es_j["name"] = registry.get<Component::SetName>(e).value;
        }
        es_j["eids"] = json::array();
        for (auto elem_e : members.members) {
            if (registry.valid(elem_e) && registry.all_of<Component::ElementID>(elem_e)) {
                es_j["eids"].push_back(registry.get<Component::ElementID>(elem_e).value);
            }
        }
        j["eleset"].push_back(es_j);
    }

    // --- 6. Export Curves ---
    j["curve"] = json::array();
    auto curve_view = registry.view<Component::CurveID, Component::Curve>();
    for (auto e : curve_view) {
        int cid = registry.get<Component::CurveID>(e).value;
        const auto& curve = registry.get<Component::Curve>(e);
        
        json curve_j;
        curve_j["cid"] = cid;
        curve_j["type"] = curve.type;
        curve_j["x"] = curve.x;
        curve_j["y"] = curve.y;
        j["curve"].push_back(curve_j);
    }

    // --- 7. Export Loads (Inverse application logic) ---
    j["load"] = json::array();
    // Use a unique counter for exported LIDs to avoid duplicate-ID issues
    // when nodes belong to multiple nodesets (the inverse mapping over-expands).
    // Also filter: only export a load for a nodeset if ALL nodes in that
    // set have that load applied — prevents superset nodesets from claiming
    // loads that only apply to a subset.
    // Dedup: each load entity is exported at most once, even if multiple
    // nodesets contain the same nodes.
    int export_lid = 1;
    std::set<entt::entity> exported_loads;
    
    for (auto ns_e : ns_view) {
        int nsid = registry.get<Component::NodeSetID>(ns_e).value;
        const auto& members = registry.get<Component::NodeSetMembers>(ns_e);
        
        // Count how many nodes in this set have each load
        std::map<entt::entity, size_t> load_node_count;
        for (auto node_e : members.members) {
            if (registry.all_of<Component::AppliedLoadRef>(node_e)) {
                const auto& applied = registry.get<Component::AppliedLoadRef>(node_e);
                for (auto load_e : applied.load_entities) {
                    if (registry.valid(load_e) && registry.all_of<Component::LoadID>(load_e)) {
                        load_node_count[load_e]++;
                    }
                }
            }
        }
        
        size_t total_nodes = members.members.size();
        for (auto [load_e, count] : load_node_count) {
            // Only export if ALL nodes in this set have this load
            if (count < total_nodes) continue;
            // Skip if this load entity has already been exported
            if (exported_loads.count(load_e)) continue;
            exported_loads.insert(load_e);
            
            json load_j;
            load_j["lid"] = export_lid++;
            load_j["nsid"] = nsid;
            
            if (registry.all_of<Component::NodalLoad>(load_e)) {
                const auto& nl = registry.get<Component::NodalLoad>(load_e);
                load_j["typeid"] = nl.type_id;
                load_j["dof"] = nl.dof;
                load_j["value"] = nl.value;
                if (registry.valid(nl.curve_entity) && registry.all_of<Component::CurveID>(nl.curve_entity)) {
                    load_j["curve"] = registry.get<Component::CurveID>(nl.curve_entity).value;
                }
            } else {
                load_j["typeid"] = 0;
            }
            j["load"].push_back(load_j);
        }
    }

    // --- 8. Export Boundaries (Inverse application logic) ---
    j["boundary"] = json::array();
    // Use a unique counter for exported BIDs to avoid duplicate-ID issues
    // when nodes belong to multiple nodesets. Also filter: only export a
    // boundary for a nodeset if ALL nodes in that set have that boundary.
    int export_bid = 1;
    for (auto ns_e : ns_view) {
        int nsid = registry.get<Component::NodeSetID>(ns_e).value;
        const auto& members = registry.get<Component::NodeSetMembers>(ns_e);
        
        // Count how many nodes in this set have each boundary
        std::map<entt::entity, size_t> bnd_node_count;
        for (auto node_e : members.members) {
            if (registry.all_of<Component::AppliedBoundaryRef>(node_e)) {
                const auto& applied = registry.get<Component::AppliedBoundaryRef>(node_e);
                for (auto bnd_e : applied.boundary_entities) {
                    if (registry.valid(bnd_e) && registry.all_of<Component::BoundaryID>(bnd_e)) {
                        bnd_node_count[bnd_e]++;
                    }
                }
            }
        }
        
        size_t total_nodes = members.members.size();
        for (auto [bnd_e, count] : bnd_node_count) {
            // Only export if ALL nodes in this set have this boundary
            if (count < total_nodes) continue;
            
            json bnd_j;
            bnd_j["bid"] = export_bid++;
            bnd_j["nsid"] = nsid;
            
            if (registry.all_of<Component::BoundarySPC>(bnd_e)) {
                const auto& spc = registry.get<Component::BoundarySPC>(bnd_e);
                bnd_j["typeid"] = spc.type_id;
                bnd_j["dof"] = spc.dof;
                bnd_j["value"] = spc.value;
            } else {
                bnd_j["typeid"] = 0;
            }
            j["boundary"].push_back(bnd_j);
        }
    }

    // --- 9. Export Analysis ---
    j["analysis"] = json::array();
    auto analysis_view = registry.view<Component::AnalysisID>();
    for (auto e : analysis_view) {
        int aid = registry.get<Component::AnalysisID>(e).value;
        json a_j;
        a_j["aid"] = aid;
        
        if (registry.all_of<Component::AnalysisType>(e)) {
            a_j["analysis_type"] = registry.get<Component::AnalysisType>(e).value;
        }
        if (registry.all_of<Component::EndTime>(e)) {
            a_j["endtime"] = registry.get<Component::EndTime>(e).value;
        }
        if (registry.all_of<Component::FixedTimeStep>(e)) {
            a_j["fixed_time_step"] = registry.get<Component::FixedTimeStep>(e).value;
        }
        j["analysis"].push_back(a_j);
    }

    // --- 10. Export Output ---
    auto output_e = data_context.output_entity;
    if (registry.valid(output_e)) {
        json o_j;
        if (registry.all_of<Component::NodeOutput>(output_e)) {
            o_j["node_output"] = registry.get<Component::NodeOutput>(output_e).node_output;
        }
        if (registry.all_of<Component::ElementOutput>(output_e)) {
            o_j["element_output"] = registry.get<Component::ElementOutput>(output_e).element_output;
        }
        if (registry.all_of<Component::OutputIntervalTime>(output_e)) {
            o_j["interval_time"] = registry.get<Component::OutputIntervalTime>(output_e).interval_time;
        }
        j["output"] = o_j;
    }

    // Write to file
    std::ofstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("JsonExporter could not open file for writing: {}", filepath);
        return false;
    }

    file << std::setw(4) << j << std::endl;
    file.close();

    spdlog::info("JsonExporter finished successfully. Data saved to {}", filepath);
    return true;
}
