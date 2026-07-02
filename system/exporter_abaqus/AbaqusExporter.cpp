// AbaqusExporter.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "exporter_abaqus/AbaqusExporter.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"
#include "components/simdroid_components.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <map>
#include <unordered_map>

// ============================================================================
// Anonymous namespace - internal helpers
// ============================================================================
namespace {

// ---------------------------------------------------------------------------
// Numeric type ID -> Abaqus element type string (inverse of abaqus_type_to_id)
// ---------------------------------------------------------------------------

std::string id_to_abaqus_type(int type_id) {
    switch (type_id) {
        // Solid
        case 308: return "C3D8R";
        case 304: return "C3D4";
        case 310: return "C3D10M";
        case 320: return "C3D20R";
        case 306: return "C3D6";
        // Shell
        case 204: return "S4R";
        case 208: return "S8R";
        case 203: return "S3R";
        // Truss / Beam
        case 102: return "T3D2";
        case 103: return "B31";
        default:
            spdlog::warn("Unknown element type id {}, defaulting to C3D8R", type_id);
            return "C3D8R";
    }
}

// NovaFEA dof string -> Abaqus dof integer
int dof_string_to_int(const std::string& dof) {
    if (dof == "x")  return 1;
    if (dof == "y")  return 2;
    if (dof == "z")  return 3;
    if (dof == "rx") return 4;
    if (dof == "ry") return 5;
    if (dof == "rz") return 6;
    // Fallback: try numeric
    try {
        return std::stoi(dof);
    } catch (...) {
        return 1;
    }
}

// Helper: write a node ID with Abaqus-style right-justified width-10 fields
// (abaqus accepts plain comma-separated too, but fixed-width matches T01.inp)
void write_node_line(std::ostream& os, int id, double x, double y, double z) {
    os << std::right << std::setw(10) << id << ","
       << std::setw(13) << std::setprecision(6) << std::showpoint << x << ","
       << std::setw(13) << y << ","
       << std::setw(13) << z << "\n";
}

void write_int_field(std::ostream& os, int val, bool first = false) {
    if (!first) os << ",";
    os << std::right << std::setw(10) << val;
}

// Collect node IDs from a node set entity (sorted for stable output)
std::vector<int> collect_node_ids(const entt::registry& reg, entt::entity set_e) {
    std::vector<int> ids;
    if (reg.all_of<Component::NodeSetMembers>(set_e)) {
        const auto& members = reg.get<Component::NodeSetMembers>(set_e);
        for (auto node : members.members) {
            if (reg.all_of<Component::NodeID>(node)) {
                ids.push_back(reg.get<Component::NodeID>(node).value);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::vector<int> collect_element_ids(const entt::registry& reg, entt::entity set_e) {
    std::vector<int> ids;
    if (reg.all_of<Component::ElementSetMembers>(set_e)) {
        const auto& members = reg.get<Component::ElementSetMembers>(set_e);
        for (auto elem : members.members) {
            if (reg.all_of<Component::ElementID>(elem)) {
                ids.push_back(reg.get<Component::ElementID>(elem).value);
            }
        }
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Find material name for a property via SimdroidPart linkage
std::string find_material_name(const entt::registry& reg, entt::entity prop_e) {
    auto view = reg.view<const Component::SimdroidPart>();
    for (auto e : view) {
        const auto& part = reg.get<Component::SimdroidPart>(e);
        if (part.section == prop_e && reg.valid(part.material)) {
            // Abaqus material is identified by NAME, not ID.
            // We synthesize "Material-<mid>" to keep it deterministic.
            if (reg.all_of<Component::MaterialID>(part.material)) {
                int mid = reg.get<Component::MaterialID>(part.material).value;
                return "Material-" + std::to_string(mid);
            }
        }
    }
    return "Material-1";
}

// Find material entity by name "Material-<mid>"
entt::entity find_material_by_name(const entt::registry& reg, const std::string& name) {
    // Parse "Material-<mid>"
    const std::string prefix = "Material-";
    if (name.rfind(prefix, 0) != 0) return entt::null;
    int mid;
    try {
        mid = std::stoi(name.substr(prefix.size()));
    } catch (...) {
        return entt::null;
    }
    auto view = reg.view<const Component::MaterialID>();
    for (auto e : view) {
        if (reg.get<Component::MaterialID>(e).value == mid) return e;
    }
    return entt::null;
}

// Find node set entity by name
entt::entity find_nodeset_by_name(const entt::registry& reg, const std::string& name) {
    auto view = reg.view<const Component::SetName>();
    for (auto e : view) {
        if (reg.get<Component::SetName>(e).value == name) return e;
    }
    return entt::null;
}

// Find amplitude (curve) entity by name
entt::entity find_curve_by_name(const entt::registry& reg, const std::string& name) {
    // Curve names are stored in CurveID? No — we store curve entities and reference them.
    // For export we need a name. Abaqus AMPLITUDE uses NAME; we synthesize "Amp-<cid>".
    const std::string prefix = "Amp-";
    if (name.rfind(prefix, 0) != 0) return entt::null;
    int cid;
    try {
        cid = std::stoi(name.substr(prefix.size()));
    } catch (...) {
        return entt::null;
    }
    auto view = reg.view<const Component::CurveID>();
    for (auto e : view) {
        if (reg.get<Component::CurveID>(e).value == cid) return e;
    }
    return entt::null;
}

} // anonymous namespace

// ============================================================================
// AbaqusExporter::save - public entry point
// ============================================================================

bool AbaqusExporter::save(const std::string& filepath, const DataContext& data_context) {
    const auto& registry = data_context.registry;
    std::ofstream out(filepath);
    if (!out.is_open()) {
        spdlog::error("AbaqusExporter could not open file: {}", filepath);
        return false;
    }

    spdlog::info("AbaqusExporter started for file: {}", filepath);

    // Header
    out << "**\n";
    out << "** ABAQUS Input Deck - Exported by NovaFEA AbaqusExporter\n";
    out << "**\n";
    out << "*HEADING\n";
    out << "NovaFEA exported model\n";
    out << "**\n";

    // -----------------------------------------------------------------------
    // *NODE
    // -----------------------------------------------------------------------
    out << "*NODE\n";
    {
        auto view = registry.view<const Component::NodeID, const Component::Position>();
        // Sort by node ID for deterministic output
        std::vector<std::pair<int, std::tuple<double, double, double>>> nodes;
        for (auto e : view) {
            int id = view.get<const Component::NodeID>(e).value;
            const auto& pos = view.get<const Component::Position>(e);
            nodes.emplace_back(id, std::make_tuple(pos.x, pos.y, pos.z));
        }
        std::sort(nodes.begin(), nodes.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [id, coords] : nodes) {
            write_node_line(out, id, std::get<0>(coords), std::get<1>(coords), std::get<2>(coords));
        }
    }

    // -----------------------------------------------------------------------
    // *ELEMENT
    // -----------------------------------------------------------------------
    {
        // Group elements by type to emit one *ELEMENT block per type
        std::map<int, std::vector<entt::entity>> by_type;
        auto view = registry.view<const Component::ElementID, const Component::ElementType>();
        for (auto e : view) {
            int tid = view.get<const Component::ElementType>(e).type_id;
            by_type[tid].push_back(e);
        }

        for (const auto& [tid, entities] : by_type) {
            std::string type_str = id_to_abaqus_type(tid);
            out << "*ELEMENT, TYPE=" << type_str << "\n";

            // Sort by element ID
            std::vector<std::pair<int, entt::entity>> elems;
            for (auto e : entities) {
                int eid = registry.get<Component::ElementID>(e).value;
                elems.emplace_back(eid, e);
            }
            std::sort(elems.begin(), elems.end(),
                      [](const auto& a, const auto& b) { return a.first < b.first; });

            for (const auto& [eid, e] : elems) {
                write_int_field(out, eid, true);
                if (registry.all_of<Component::Connectivity>(e)) {
                    const auto& conn = registry.get<Component::Connectivity>(e);
                    for (auto node : conn.nodes) {
                        if (registry.all_of<Component::NodeID>(node)) {
                            write_int_field(out, registry.get<Component::NodeID>(node).value);
                        }
                    }
                }
                out << "\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // *ELSET / *SOLID SECTION (for each property/part)
    // -----------------------------------------------------------------------
    {
        // Build property -> element_set mapping from SimdroidPart
        auto part_view = registry.view<const Component::SimdroidPart>();
        for (auto e : part_view) {
            const auto& part = registry.get<Component::SimdroidPart>(e);
            if (!registry.valid(part.section) || !registry.valid(part.element_set)) continue;

            // Generate a unique elset name for this section
            std::string elset_name = "ELSET_PROP_";
            if (registry.all_of<Component::PropertyID>(part.section)) {
                elset_name += std::to_string(registry.get<Component::PropertyID>(part.section).value);
            } else {
                elset_name += "1";
            }

            // Emit *ELSET
            out << "*ELSET, ELSET=" << elset_name << "\n";
            auto elem_ids = collect_element_ids(registry, part.element_set);
            // Abaqus limits 16 integers per continuation line; use simple one-per-line for safety
            for (size_t i = 0; i < elem_ids.size(); ++i) {
                out << std::right << std::setw(10) << elem_ids[i];
                if (i + 1 < elem_ids.size()) out << ",";
                out << "\n";
            }

            // Emit *SOLID SECTION
            std::string mat_name = find_material_name(registry, part.section);
            out << "*SOLID SECTION, ELSET=" << elset_name
                << ", MATERIAL=" << mat_name << "\n";
            out << ",\n"; // data line (empty for solid)
        }
    }

    // -----------------------------------------------------------------------
    // *NSET and *ELSET (user-defined sets, excluding section-related ones)
    // -----------------------------------------------------------------------
    {
        // Node sets
        auto ns_view = registry.view<const Component::SetName, const Component::NodeSetMembers>();
        for (auto e : ns_view) {
            const auto& name = registry.get<Component::SetName>(e).value;
            // Skip the internally-generated set if any; export all user sets
            out << "*NSET, NSET=" << name << "\n";
            auto ids = collect_node_ids(registry, e);
            for (size_t i = 0; i < ids.size(); ++i) {
                out << std::right << std::setw(10) << ids[i];
                if (i + 1 < ids.size()) out << ",";
                out << "\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // *MATERIAL
    // -----------------------------------------------------------------------
    {
        auto mat_view = registry.view<const Component::MaterialID>();
        for (auto e : mat_view) {
            int mid = registry.get<Component::MaterialID>(e).value;
            std::string mat_name = "Material-" + std::to_string(mid);
            out << "*MATERIAL, NAME=" << mat_name << "\n";

            if (registry.all_of<Component::LinearElasticParams>(e)) {
                const auto& p = registry.get<Component::LinearElasticParams>(e);
                out << "*DENSITY\n";
                out << std::setprecision(6) << std::showpoint << p.rho << ",0.0\n";
                out << "*ELASTIC, TYPE=ISOTROPIC\n";
                out << std::setprecision(6) << std::showpoint << p.E << ","
                    << std::setprecision(6) << std::showpoint << p.nu << ",0.0\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // *AMPLITUDE
    // -----------------------------------------------------------------------
    {
        auto curve_view = registry.view<const Component::CurveID, const Component::Curve>();
        for (auto e : curve_view) {
            int cid = registry.get<Component::CurveID>(e).value;
            std::string amp_name = "Amp-" + std::to_string(cid);
            const auto& c = registry.get<Component::Curve>(e);
            out << "*AMPLITUDE, NAME=" << amp_name << "\n";
            for (size_t i = 0; i < c.x.size(); ++i) {
                out << std::setprecision(6) << std::showpoint << c.x[i] << ","
                    << std::setprecision(6) << std::showpoint << c.y[i];
                if (i + 1 < c.x.size()) out << ",";
                out << "\n";
            }
        }
    }

    // -----------------------------------------------------------------------
    // *STEP
    // -----------------------------------------------------------------------
    {
        if (data_context.analysis_entity != entt::null &&
            registry.valid(data_context.analysis_entity)) {

            auto a = data_context.analysis_entity;
            out << "*STEP, NLGEOM=YES";
            out << "\n";

            // *DYNAMIC
            std::string step_type = "DynamicExplicit";
            if (registry.all_of<Component::AnalysisType>(a)) {
                step_type = registry.get<Component::AnalysisType>(a).value;
            }

            if (step_type == "DynamicExplicit" || step_type == "DynamicImplicit") {
                out << "*DYNAMIC, EXPLICIT, DIRECT USER CONTROL\n";
            } else {
                out << "*DYNAMIC, EXPLICIT\n";
            }

            double dt = 0.001, end_time = 1.0;
            if (registry.all_of<Component::FixedTimeStep>(a)) {
                dt = registry.get<Component::FixedTimeStep>(a).value;
            }
            if (registry.all_of<Component::EndTime>(a)) {
                end_time = registry.get<Component::EndTime>(a).value;
            }
            out << std::setprecision(6) << std::showpoint << dt << ","
                << std::setprecision(6) << std::showpoint << end_time << "\n";

            // *BOUNDARY
            {
                // Gather boundary entities, find which node set they apply to
                auto b_view = registry.view<const Component::BoundarySPC>();
                // Build reverse map: node -> boundary entities
                // But BoundarySPC is the definition; we need to know which nodeset it applies to.
                // In AbaqusParser, we attached AppliedBoundaryRef to nodes.
                // To invert: for each BoundarySPC entity, find the nodeset that references it.
                // Simpler: iterate nodes, find AppliedBoundaryRef, collect unique boundary entities
                // and the node IDs they apply to. Then group by boundary entity.
                std::map<entt::entity, std::vector<int>> b_to_nodes;
                auto node_view = registry.view<const Component::NodeID, const Component::AppliedBoundaryRef>();
                for (auto n : node_view) {
                    int nid = node_view.get<const Component::NodeID>(n).value;
                    const auto& ref = node_view.get<const Component::AppliedBoundaryRef>(n);
                    for (auto b : ref.boundary_entities) {
                        b_to_nodes[b].push_back(nid);
                    }
                }

                // For each unique boundary definition, emit a *BOUNDARY block
                // However, Abaqus *BOUNDARY references a node set, not individual nodes.
                // Strategy: create an internal node set per boundary entity.
                int bset_counter = 0;
                for (const auto& [b_ent, nids] : b_to_nodes) {
                    if (!registry.all_of<Component::BoundarySPC>(b_ent)) continue;
                    const auto& spc = registry.get<Component::BoundarySPC>(b_ent);
                    int dof = dof_string_to_int(spc.dof);

                    std::string bset_name = "BCSET_" + std::to_string(++bset_counter);
                    out << "*NSET, NSET=" << bset_name << "\n";
                    for (size_t i = 0; i < nids.size(); ++i) {
                        out << std::right << std::setw(10) << nids[i];
                        if (i + 1 < nids.size()) out << ",";
                        out << "\n";
                    }

                    out << "*BOUNDARY\n";
                    out << bset_name << "," << dof << ", ," 
                        << std::setprecision(6) << std::showpoint << spc.value << "\n";
                }
            }

            // *CLOAD
            {
                std::map<entt::entity, std::vector<int>> l_to_nodes;
                auto node_view = registry.view<const Component::NodeID, const Component::AppliedLoadRef>();
                for (auto n : node_view) {
                    int nid = node_view.get<const Component::NodeID>(n).value;
                    const auto& ref = node_view.get<const Component::AppliedLoadRef>(n);
                    for (auto l : ref.load_entities) {
                        l_to_nodes[l].push_back(nid);
                    }
                }

                int lset_counter = 0;
                for (const auto& [l_ent, nids] : l_to_nodes) {
                    if (!registry.all_of<Component::NodalLoad>(l_ent)) continue;
                    const auto& load = registry.get<Component::NodalLoad>(l_ent);
                    int dof = dof_string_to_int(load.dof);

                    std::string lset_name = "LDSET_" + std::to_string(++lset_counter);
                    out << "*NSET, NSET=" << lset_name << "\n";
                    for (size_t i = 0; i < nids.size(); ++i) {
                        out << std::right << std::setw(10) << nids[i];
                        if (i + 1 < nids.size()) out << ",";
                        out << "\n";
                    }

                    out << "*CLOAD";
                    if (load.curve_entity != entt::null &&
                        registry.all_of<Component::CurveID>(load.curve_entity)) {
                        int cid = registry.get<Component::CurveID>(load.curve_entity).value;
                        out << ", AMPLITUDE=Amp-" << std::to_string(cid);
                    }
                    out << "\n";
                    out << lset_name << "," << dof << ","
                        << std::setprecision(6) << std::showpoint << load.value << "\n";
                }
            }

            // Output controls
            out << "*OUTPUT, FIELD, VARIABLE=PRESELECT\n";
            out << "*OUTPUT, HISTORY, VARIABLE=PRESELECT\n";
            out << "*END STEP\n";
        }
    }

    out.close();
    spdlog::info("AbaqusExporter finished: {}", filepath);
    return true;
}
