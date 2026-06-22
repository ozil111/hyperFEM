/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "remesh/ConnectionPreservingRemesher.h"

#include "analysis/GraphBuilder.h"
#include "TopologyData.h"
#include "components/load_components.h"
#include "components/material_components.h"
#include "components/mesh_components.h"
#include "components/property_components.h"
#include "components/simdroid_components.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <set>
#include <sstream>
#include <unordered_map>

namespace {

std::string connection_type_to_string(ConnectionType type) {
    switch (type) {
    case ConnectionType::Contact:
        return "Contact";
    case ConnectionType::SharedNode:
        return "SharedNode";
    case ConnectionType::MPC:
        return "MPC";
    }
    return "Unknown";
}

std::string property_type_name(entt::registry& registry, entt::entity section) {
    if (!registry.valid(section)) return "None";
    if (registry.all_of<Component::ShellProperty>(section)) return "ShellProperty";
    if (registry.all_of<Component::BeamProperty>(section)) return "BeamProperty";
    if (registry.all_of<Component::FiberBeamProperty>(section)) return "FiberBeamProperty";
    if (registry.all_of<Component::AxialSpringDamperProperty>(section)) return "AxialSpringDamperProperty";
    if (registry.all_of<Component::BeamSpringProperty>(section)) return "BeamSpringProperty";
    if (registry.all_of<Component::SolidShCompProperty>(section)) return "SolidShCompProperty";
    if (registry.all_of<Component::SolidShellProperty>(section)) return "SolidShellProperty";
    if (registry.all_of<Component::SolidAdvancedProperty>(section)) return "SolidAdvancedProperty";
    if (registry.all_of<Component::SolidProperty>(section)) return "SolidProperty";
    if (registry.all_of<Component::TrussProperty>(section)) return "TrussProperty";
    if (registry.all_of<Component::CohesiveProperty>(section)) return "CohesiveProperty";
    return "UnknownProperty";
}

std::string material_type_name(entt::registry& registry, entt::entity material) {
    if (!registry.valid(material)) return "None";
    if (registry.all_of<Component::MaterialModel>(material)) {
        return registry.get<Component::MaterialModel>(material).value;
    }
    if (registry.all_of<Component::LinearElasticParams>(material)) return "LinearElasticParams";
    if (registry.all_of<Component::IsotropicPlasticParams>(material)) return "IsotropicPlasticParams";
    if (registry.all_of<Component::RateDependentPlasticParams>(material)) return "RateDependentPlasticParams";
    if (registry.all_of<Component::HyperelasticMode>(material)) return "HyperelasticMode";
    return "UnknownMaterial";
}

std::string interface_key(const InterfaceSignature& sig) {
    std::ostringstream os;
    os << sig.source_part << "|" << sig.target_part << "|"
       << connection_type_to_string(sig.type) << "|" << sig.sub_type;
    return os.str();
}

std::string part_signature_key(const PartRemeshPlan& part) {
    std::ostringstream os;
    os << part.part_name << "|" << part.property_type << "|" << part.material_type << "|";
    for (const auto& [type_id, count] : part.element_type_counts) {
        (void)count;
        os << type_id << ",";
    }
    return os.str();
}

struct StructuredGridInfo {
    std::vector<double> xs;
    std::vector<double> ys;
    std::vector<double> zs;
};

constexpr double kCoordTol = 1e-9;

// Hex8 → 6 Tet4 standard decomposition table.
// Hex8 corner ordering matches the Connectivity used by remesh_structured_hex8:
// {n0,n1,n2,n3,n4,n5,n6,n7} = bottom CCW then top CCW.
constexpr int kTetTable[6][4] = {
    {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
    {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6}
};

bool nearly_equal(double a, double b) {
    return std::abs(a - b) <= kCoordTol;
}

void unique_push_sorted(std::vector<double>& values, double value) {
    values.push_back(value);
}

void sort_unique_coords(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end(), [](double a, double b) {
        return nearly_equal(a, b);
    }), values.end());
}

std::size_t coord_index(const std::vector<double>& values, double value) {
    auto it = std::lower_bound(values.begin(), values.end(), value - kCoordTol);
    if (it != values.end() && nearly_equal(*it, value)) {
        return static_cast<std::size_t>(std::distance(values.begin(), it));
    }
    return values.size();
}

std::vector<int> choose_axis_indices(int original_cells, int target_cells) {
    target_cells = std::max(1, std::min(target_cells, original_cells));
    std::vector<int> indices;
    indices.reserve(static_cast<std::size_t>(target_cells) + 1);
    for (int i = 0; i <= target_cells; ++i) {
        const double t = static_cast<double>(i) * static_cast<double>(original_cells) /
                         static_cast<double>(target_cells);
        int idx = static_cast<int>(std::round(t));
        idx = std::max(0, std::min(idx, original_cells));
        if (indices.empty() || indices.back() != idx) {
            indices.push_back(idx);
        }
    }
    if (indices.front() != 0) indices.insert(indices.begin(), 0);
    if (indices.back() != original_cells) indices.push_back(original_cells);
    return indices;
}

std::array<int, 3> choose_target_cells(const std::array<int, 3>& original_cells,
                                       int target_element_count) {
    target_element_count = std::max(1, target_element_count);

    std::array<int, 3> best{1, 1, 1};
    int best_product = 1;
    double best_score = std::numeric_limits<double>::max();

    for (int x = 1; x <= original_cells[0]; ++x) {
        for (int y = 1; y <= original_cells[1]; ++y) {
            for (int z = 1; z <= original_cells[2]; ++z) {
                const int product = x * y * z;
                if (product > target_element_count) continue;

                const double sx = static_cast<double>(x) / static_cast<double>(original_cells[0]);
                const double sy = static_cast<double>(y) / static_cast<double>(original_cells[1]);
                const double sz = static_cast<double>(z) / static_cast<double>(original_cells[2]);
                const double mean = (sx + sy + sz) / 3.0;
                const double shape_penalty =
                    (sx - mean) * (sx - mean) +
                    (sy - mean) * (sy - mean) +
                    (sz - mean) * (sz - mean);
                const double count_penalty =
                    static_cast<double>(target_element_count - product) /
                    static_cast<double>(target_element_count);
                const double score = count_penalty * 1000.0 + shape_penalty;

                if (product > best_product ||
                    (product == best_product && score < best_score)) {
                    best = {x, y, z};
                    best_product = product;
                    best_score = score;
                }
            }
        }
    }

    return best;
}

entt::entity find_set_by_name(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto entity : view) {
        if (view.get<const Component::SetName>(entity).value == name) return entity;
    }
    return entt::null;
}

std::vector<entt::entity> all_nodes_sorted(entt::registry& registry) {
    std::vector<std::pair<int, entt::entity>> pairs;
    auto view = registry.view<const Component::NodeID>();
    for (auto e : view) pairs.emplace_back(view.get<const Component::NodeID>(e).value, e);
    std::sort(pairs.begin(), pairs.end());
    std::vector<entt::entity> out;
    out.reserve(pairs.size());
    for (const auto& [id, e] : pairs) {
        (void)id;
        out.push_back(e);
    }
    return out;
}

entt::entity nearest_node(entt::registry& registry,
                          const std::vector<entt::entity>& candidates,
                          const Component::Position& pos) {
    entt::entity best = entt::null;
    double best_d2 = std::numeric_limits<double>::max();
    for (auto node : candidates) {
        if (!registry.valid(node) || !registry.all_of<Component::Position>(node)) continue;
        const auto& p = registry.get<Component::Position>(node);
        const double dx = p.x - pos.x;
        const double dy = p.y - pos.y;
        const double dz = p.z - pos.z;
        const double d2 = dx * dx + dy * dy + dz * dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            best = node;
        }
    }
    return best;
}

void append_unique(std::vector<entt::entity>& items, entt::entity value) {
    if (value == entt::null) return;
    if (std::find(items.begin(), items.end(), value) == items.end()) {
        items.push_back(value);
    }
}

// Collect node entities referenced by a set entity, supporting both NodeSetMembers
// and SurfaceSetMembers (via SurfaceConnectivity).
std::vector<entt::entity> collect_nodes_from_set(entt::registry& registry,
                                                  entt::entity set_entity) {
    std::vector<entt::entity> nodes;
    if (!registry.valid(set_entity)) return nodes;
    if (registry.all_of<Component::NodeSetMembers>(set_entity)) {
        for (auto n : registry.get<Component::NodeSetMembers>(set_entity).members) {
            append_unique(nodes, n);
        }
    }
    if (registry.all_of<Component::SurfaceSetMembers>(set_entity)) {
        for (auto s : registry.get<Component::SurfaceSetMembers>(set_entity).members) {
            if (!registry.valid(s) || !registry.all_of<Component::SurfaceConnectivity>(s)) continue;
            for (auto n : registry.get<Component::SurfaceConnectivity>(s).nodes) {
                append_unique(nodes, n);
            }
        }
    }
    return nodes;
}

void reapply_loads_and_boundaries_from_blueprint(DataContext& ctx) {
    auto& registry = ctx.registry;
    if (ctx.simdroid_blueprint.is_null()) return;

    auto apply_to_node_set = [&](const std::string& set_name,
                                 const std::vector<entt::entity>& defs,
                                 bool is_load) {
        entt::entity set_entity = find_set_by_name(registry, set_name);
        if (set_entity == entt::null || !registry.all_of<Component::NodeSetMembers>(set_entity)) return;
        const auto& members = registry.get<Component::NodeSetMembers>(set_entity).members;
        for (auto node : members) {
            if (!registry.valid(node)) continue;
            if (is_load) {
                auto& applied = registry.get_or_emplace<Component::AppliedLoadRef>(node);
                for (auto def : defs) append_unique(applied.load_entities, def);
            } else {
                auto& applied = registry.get_or_emplace<Component::AppliedBoundaryRef>(node);
                for (auto def : defs) append_unique(applied.boundary_entities, def);
            }
        }
    };

    if (ctx.simdroid_blueprint.contains("Load") && ctx.simdroid_blueprint["Load"].is_object()) {
        for (auto it = ctx.simdroid_blueprint["Load"].begin(); it != ctx.simdroid_blueprint["Load"].end(); ++it) {
            if (!it.value().is_object()) continue;
            std::string set_name = it.value().value("NodeSet", "");
            if (set_name.empty()) set_name = it.value().value("Set", "");
            if (set_name.empty()) continue;

            std::vector<entt::entity> defs;
            auto view = registry.view<const Component::SetName>();
            for (auto e : view) {
                if (view.get<const Component::SetName>(e).value != it.key()) continue;
                if (registry.all_of<Component::NodalLoad>(e) ||
                    registry.all_of<Component::BaseAccelerationLoad>(e)) {
                    defs.push_back(e);
                }
            }
            apply_to_node_set(set_name, defs, true);
        }
    }

    if (ctx.simdroid_blueprint.contains("Constraint") &&
        ctx.simdroid_blueprint["Constraint"].is_object() &&
        ctx.simdroid_blueprint["Constraint"].contains("Boundary") &&
        ctx.simdroid_blueprint["Constraint"]["Boundary"].is_object()) {
        const auto& boundaries = ctx.simdroid_blueprint["Constraint"]["Boundary"];
        for (auto it = boundaries.begin(); it != boundaries.end(); ++it) {
            if (!it.value().is_object()) continue;
            std::string set_name = it.value().value("NodeSet", "");
            if (set_name.empty()) set_name = it.value().value("Set", "");
            if (set_name.empty()) continue;

            std::vector<entt::entity> defs;
            auto view = registry.view<const Component::SetName>();
            for (auto e : view) {
                if (view.get<const Component::SetName>(e).value != it.key()) continue;
                if (registry.all_of<Component::BoundarySPC>(e)) defs.push_back(e);
            }
            apply_to_node_set(set_name, defs, false);
        }
    }
}

} // namespace

bool InterfaceSignature::operator<(const InterfaceSignature& rhs) const {
    if (source_part != rhs.source_part) return source_part < rhs.source_part;
    if (target_part != rhs.target_part) return target_part < rhs.target_part;
    if (type != rhs.type) return static_cast<int>(type) < static_cast<int>(rhs.type);
    return sub_type < rhs.sub_type;
}

bool InterfaceSignature::same_identity(const InterfaceSignature& rhs) const {
    return source_part == rhs.source_part &&
           target_part == rhs.target_part &&
           type == rhs.type &&
           sub_type == rhs.sub_type;
}

std::vector<InterfaceSignature>
ConnectionPreservingRemesher::collect_interface_signatures(const PartGraph& graph) {
    std::map<InterfaceSignature, int> merged;

    for (const auto& [source, node] : graph.nodes) {
        for (const auto& edge : node.edges) {
            InterfaceSignature sig;
            sig.source_part = std::min(source, edge.target_part);
            sig.target_part = std::max(source, edge.target_part);
            sig.type = edge.type;
            sig.sub_type = edge.sub_type;
            sig.count = edge.count;
            merged[sig] += edge.count;
        }
    }

    std::vector<InterfaceSignature> signatures;
    signatures.reserve(merged.size());
    for (const auto& [sig, count] : merged) {
        InterfaceSignature copied = sig;
        copied.count = count;
        signatures.push_back(std::move(copied));
    }
    return signatures;
}

std::vector<ProtectedPartInfo>
ConnectionPreservingRemesher::extract_protected_entities(entt::registry& registry,
                                                         SimdroidInspector& inspector) {
    if (!inspector.is_built) {
        inspector.build(registry);
    }

    std::map<std::string, ProtectedPartInfo> info_by_part;
    {
        auto part_view = registry.view<const Component::SimdroidPart>();
        for (auto part_entity : part_view) {
            const auto& part = part_view.get<const Component::SimdroidPart>(part_entity);
            info_by_part[part.name].part_name = part.name;
        }
    }

    // Assign a node entity to every part that references it via its elements.
    auto assign_node_to_parts = [&](entt::entity node_entity,
                                    std::vector<entt::entity> ProtectedPartInfo::* field) {
        if (!registry.valid(node_entity) || !registry.all_of<Component::NodeID>(node_entity)) return;
        const int nid = registry.get<Component::NodeID>(node_entity).value;
        auto it = inspector.nid_to_elems.find(nid);
        if (it == inspector.nid_to_elems.end()) return;
        for (int eid : it->second) {
            auto pit = inspector.eid_to_part.find(eid);
            if (pit == inspector.eid_to_part.end()) continue;
            auto iit = info_by_part.find(pit->second);
            if (iit == info_by_part.end()) continue;
            append_unique(iit->second.*field, node_entity);
        }
    };

    // 1. Shared nodes: a node referenced by elements of more than one part.
    for (const auto& [nid, elem_ids] : inspector.nid_to_elems) {
        if (elem_ids.empty()) continue;
        std::set<std::string> parts_for_node;
        for (int eid : elem_ids) {
            auto pit = inspector.eid_to_part.find(eid);
            if (pit != inspector.eid_to_part.end()) {
                parts_for_node.insert(pit->second);
            }
        }
        if (parts_for_node.size() <= 1) continue;

        auto nid_it = inspector.nid_to_entity.find(nid);
        if (nid_it == inspector.nid_to_entity.end()) continue;
        entt::entity node_entity = nid_it->second;
        for (const auto& part_name : parts_for_node) {
            auto iit = info_by_part.find(part_name);
            if (iit != info_by_part.end()) {
                append_unique(iit->second.shared_nodes, node_entity);
            }
        }
    }

    // 2. Contact nodes: nodes on master/slave surfaces of every ContactBase.
    {
        auto view = registry.view<const Component::ContactBase>();
        for (auto entity : view) {
            const auto& contact = view.get<const Component::ContactBase>(entity);
            for (auto node_entity : collect_nodes_from_set(registry, contact.master_entity)) {
                assign_node_to_parts(node_entity, &ProtectedPartInfo::contact_nodes);
            }
            for (auto node_entity : collect_nodes_from_set(registry, contact.slave_entity)) {
                assign_node_to_parts(node_entity, &ProtectedPartInfo::contact_nodes);
            }
        }
    }

    // 3. Loaded nodes: nodes carrying AppliedLoadRef.
    {
        auto view = registry.view<const Component::AppliedLoadRef>();
        for (auto entity : view) {
            assign_node_to_parts(entity, &ProtectedPartInfo::loaded_nodes);
        }
    }

    // 4. Constrained nodes: nodes carrying AppliedBoundaryRef.
    {
        auto view = registry.view<const Component::AppliedBoundaryRef>();
        for (auto entity : view) {
            assign_node_to_parts(entity, &ProtectedPartInfo::constrained_nodes);
        }
    }

    std::vector<ProtectedPartInfo> result;
    result.reserve(info_by_part.size());
    for (auto& [name, info] : info_by_part) {
        result.push_back(std::move(info));
    }
    return result;
}

RemeshPlan ConnectionPreservingRemesher::build_plan(entt::registry& registry,
                                                    SimdroidInspector& inspector,
                                                    const RemeshOptions& options) {
    RemeshPlan plan;
    plan.options = options;

    RemeshOptions normalized = options;
    if (normalized.target_compression_ratio < 1.0) {
        normalized.target_compression_ratio = 1.0;
        plan.warnings.push_back("target_compression_ratio was below 1.0 and was clamped to 1.0");
    }
    if (normalized.min_elements_per_part < 1) {
        normalized.min_elements_per_part = 1;
        plan.warnings.push_back("min_elements_per_part was below 1 and was clamped to 1");
    }
    plan.options = normalized;

    PartGraph graph = GraphBuilder::build(registry, inspector);
    plan.interfaces = collect_interface_signatures(graph);

    auto protected_infos = extract_protected_entities(registry, inspector);
    std::map<std::string, const ProtectedPartInfo*> protected_by_name;
    for (const auto& info : protected_infos) {
        protected_by_name[info.part_name] = &info;
    }

    auto part_view = registry.view<const Component::SimdroidPart>();
    for (auto part_entity : part_view) {
        const auto& part = part_view.get<const Component::SimdroidPart>(part_entity);

        PartRemeshPlan part_plan;
        part_plan.part_name = part.name;
        part_plan.property_type = property_type_name(registry, part.section);
        part_plan.material_type = material_type_name(registry, part.material);

        auto graph_it = graph.nodes.find(part.name);
        if (graph_it != graph.nodes.end()) {
            part_plan.has_load = graph_it->second.is_load_part;
            part_plan.has_constraint = graph_it->second.is_constraint_part;
        }

        auto prot_it = protected_by_name.find(part.name);
        if (prot_it != protected_by_name.end()) {
            part_plan.protected_shared_node_count =
                static_cast<int>(prot_it->second->shared_nodes.size());
            part_plan.protected_contact_node_count =
                static_cast<int>(prot_it->second->contact_nodes.size());
            part_plan.protected_loaded_node_count =
                static_cast<int>(prot_it->second->loaded_nodes.size());
            part_plan.protected_constrained_node_count =
                static_cast<int>(prot_it->second->constrained_nodes.size());
        }

        if (registry.valid(part.element_set) &&
            registry.all_of<Component::ElementSetMembers>(part.element_set)) {
            const auto& members = registry.get<Component::ElementSetMembers>(part.element_set).members;
            part_plan.original_element_count = static_cast<int>(members.size());
            for (auto element_entity : members) {
                if (!registry.valid(element_entity) ||
                    !registry.all_of<Component::ElementType>(element_entity)) {
                    continue;
                }
                const int type_id = registry.get<Component::ElementType>(element_entity).type_id;
                part_plan.element_type_counts[type_id]++;
            }
        }

        int protected_floor = normalized.min_elements_per_part;
        protected_floor = std::max(protected_floor,
                                   static_cast<int>(part_plan.element_type_counts.size()));
        if (normalized.preserve_interface_elements && graph_it != graph.nodes.end()) {
            protected_floor = std::max(protected_floor,
                                       static_cast<int>(graph_it->second.edges.size()));
        }
        if (part_plan.has_load || part_plan.has_constraint) {
            protected_floor = std::max(protected_floor, 2);
        }

        const int compressed = static_cast<int>(
            std::ceil(part_plan.original_element_count / normalized.target_compression_ratio));
        part_plan.target_element_count = std::max(protected_floor, compressed);
        if (part_plan.original_element_count > 0) {
            part_plan.target_element_count = std::min(part_plan.target_element_count,
                                                      part_plan.original_element_count);
        }

        plan.original_element_count += part_plan.original_element_count;
        plan.target_element_count += part_plan.target_element_count;
        plan.parts.push_back(std::move(part_plan));
    }

    std::sort(plan.parts.begin(), plan.parts.end(), [](const auto& a, const auto& b) {
        return a.part_name < b.part_name;
    });

    return plan;
}

RemeshValidationResult
ConnectionPreservingRemesher::validate_preservation(const RemeshPlan& before,
                                                    const RemeshPlan& after) {
    RemeshValidationResult result;

    std::set<std::string> before_parts;
    std::set<std::string> after_parts;
    std::set<std::string> before_part_signatures;
    std::set<std::string> after_part_signatures;

    for (const auto& part : before.parts) {
        before_parts.insert(part.part_name);
        before_part_signatures.insert(part_signature_key(part));
    }
    for (const auto& part : after.parts) {
        after_parts.insert(part.part_name);
        after_part_signatures.insert(part_signature_key(part));
    }

    for (const auto& name : before_parts) {
        if (after_parts.find(name) == after_parts.end()) {
            result.errors.push_back("Missing part after remesh: " + name);
        }
    }
    for (const auto& name : after_parts) {
        if (before_parts.find(name) == before_parts.end()) {
            result.errors.push_back("Unexpected new part after remesh: " + name);
        }
    }
    for (const auto& sig : before_part_signatures) {
        if (after_part_signatures.find(sig) == after_part_signatures.end()) {
            result.errors.push_back("Part metadata or element type signature changed: " + sig);
        }
    }

    std::set<std::string> before_interfaces;
    std::set<std::string> after_interfaces;
    for (const auto& sig : before.interfaces) before_interfaces.insert(interface_key(sig));
    for (const auto& sig : after.interfaces) after_interfaces.insert(interface_key(sig));

    for (const auto& key : before_interfaces) {
        if (after_interfaces.find(key) == after_interfaces.end()) {
            result.errors.push_back("Missing interface after remesh: " + key);
        }
    }
    for (const auto& key : after_interfaces) {
        if (before_interfaces.find(key) == before_interfaces.end()) {
            result.errors.push_back("Unexpected new interface after remesh: " + key);
        }
    }

    result.valid = result.errors.empty();
    return result;
}

RemeshValidationResult
ConnectionPreservingRemesher::validate_preservation_detailed(
    entt::registry& registry,
    const nlohmann::json& blueprint,
    const RemeshPlan& after) {
    RemeshValidationResult result;

    // 1. Check that all NodeSetMembers are non-empty
    {
        auto view = registry.view<Component::NodeSetMembers>();
        for (auto entity : view) {
            const auto& members = view.get<Component::NodeSetMembers>(entity).members;
            if (members.empty()) {
                std::string name = "unnamed";
                if (registry.all_of<Component::SetName>(entity)) {
                    name = registry.get<Component::SetName>(entity).value;
                }
                result.errors.push_back("NodeSet is empty after remesh: " + name);
            }
        }
    }

    // 2. Check that all ElementSetMembers are non-empty
    {
        auto view = registry.view<Component::ElementSetMembers>();
        for (auto entity : view) {
            const auto& members = view.get<Component::ElementSetMembers>(entity).members;
            if (members.empty()) {
                std::string name = "unnamed";
                if (registry.all_of<Component::SetName>(entity)) {
                    name = registry.get<Component::SetName>(entity).value;
                }
                result.errors.push_back("ElementSet is empty after remesh: " + name);
            }
        }
    }

    // 3. Check that all SurfaceSetMembers are non-empty (warning level)
    {
        auto view = registry.view<Component::SurfaceSetMembers>();
        for (auto entity : view) {
            const auto& members = view.get<Component::SurfaceSetMembers>(entity).members;
            if (members.empty()) {
                std::string name = "unnamed";
                if (registry.all_of<Component::SetName>(entity)) {
                    name = registry.get<Component::SetName>(entity).value;
                }
                result.warnings.push_back("SurfaceSet is empty after remesh: " + name);
            }
        }
    }

    // 4. Check sets referenced by loads in the blueprint
    if (blueprint.contains("Load") && blueprint["Load"].is_object()) {
        for (auto it = blueprint["Load"].begin(); it != blueprint["Load"].end(); ++it) {
            if (!it.value().is_object()) continue;
            std::string set_name = it.value().value("NodeSet", "");
            if (set_name.empty()) set_name = it.value().value("Set", "");
            if (set_name.empty()) continue;

            auto set_entity = find_set_by_name(registry, set_name);
            if (set_entity == entt::null) {
                result.errors.push_back("Load target set missing after remesh: " + set_name
                    + " (referenced by load: " + it.key() + ")");
            } else if (registry.all_of<Component::NodeSetMembers>(set_entity)) {
                if (registry.get<Component::NodeSetMembers>(set_entity).members.empty()) {
                    result.errors.push_back("Load target set is empty after remesh: " + set_name
                        + " (referenced by load: " + it.key() + ")");
                }
            }
        }
    }

    // 5. Check sets referenced by boundaries in the blueprint
    if (blueprint.contains("Constraint") && blueprint["Constraint"].is_object() &&
        blueprint["Constraint"].contains("Boundary") &&
        blueprint["Constraint"]["Boundary"].is_object()) {
        const auto& boundaries = blueprint["Constraint"]["Boundary"];
        for (auto it = boundaries.begin(); it != boundaries.end(); ++it) {
            if (!it.value().is_object()) continue;
            std::string set_name = it.value().value("NodeSet", "");
            if (set_name.empty()) set_name = it.value().value("Set", "");
            if (set_name.empty()) continue;

            auto set_entity = find_set_by_name(registry, set_name);
            if (set_entity == entt::null) {
                result.errors.push_back("Boundary target set missing after remesh: " + set_name
                    + " (referenced by boundary: " + it.key() + ")");
            } else if (registry.all_of<Component::NodeSetMembers>(set_entity)) {
                if (registry.get<Component::NodeSetMembers>(set_entity).members.empty()) {
                    result.errors.push_back("Boundary target set is empty after remesh: " + set_name
                        + " (referenced by boundary: " + it.key() + ")");
                }
            }
        }
    }

    // 6. Check sets referenced by part properties in the blueprint
    if (blueprint.contains("PartProperty") && blueprint["PartProperty"].is_object()) {
        for (auto it = blueprint["PartProperty"].begin();
             it != blueprint["PartProperty"].end(); ++it) {
            if (!it.value().is_object()) continue;
            const std::string ele_set_name = it.value().value("EleSet", "");
            if (ele_set_name.empty()) continue;

            auto set_entity = find_set_by_name(registry, ele_set_name);
            if (set_entity == entt::null) {
                result.errors.push_back("Part property target set missing after remesh: " + ele_set_name
                    + " (referenced by: " + it.key() + ")");
            } else if (registry.all_of<Component::ElementSetMembers>(set_entity)) {
                if (registry.get<Component::ElementSetMembers>(set_entity).members.empty()) {
                    result.errors.push_back("Part property target set is empty after remesh: " + ele_set_name
                        + " (referenced by: " + it.key() + ")");
                }
            }
        }
    }

    // 7. Verify load coverage: if before had loads, after should have AppliedLoadRef nodes
    bool before_has_load = false;
    for (const auto& part : after.parts) {
        if (part.has_load) { before_has_load = true; break; }
    }
    if (before_has_load) {
        auto load_view = registry.view<Component::AppliedLoadRef>();
        bool has_applied_loads = (load_view.begin() != load_view.end());
        if (!has_applied_loads) {
            result.errors.push_back(
                "Loads were present before remesh but no nodes have AppliedLoadRef after remesh");
        }
    }

    // 8. Verify boundary coverage
    bool before_has_constraint = false;
    for (const auto& part : after.parts) {
        if (part.has_constraint) { before_has_constraint = true; break; }
    }
    if (before_has_constraint) {
        auto bc_view = registry.view<Component::AppliedBoundaryRef>();
        bool has_applied_boundaries = (bc_view.begin() != bc_view.end());
        if (!has_applied_boundaries) {
            result.errors.push_back(
                "Boundaries were present before remesh but no nodes have AppliedBoundaryRef after remesh");
        }
    }

    // 9. Verify element count consistency between plan and registry
    {
        auto elem_view = registry.view<Component::ElementID>();
        int actual_count = 0;
        for (auto e : elem_view) { (void)e; ++actual_count; }
        if (actual_count != after.original_element_count) {
            result.errors.push_back(
                "Element count mismatch: plan reports " + std::to_string(after.original_element_count)
                + " but registry has " + std::to_string(actual_count));
        }
    }

    result.valid = result.errors.empty();
    return result;
}

nlohmann::json RemeshPlan::to_json() const {
    nlohmann::json j;
    j["options"] = {
        {"target_compression_ratio", options.target_compression_ratio},
        {"min_elements_per_part", options.min_elements_per_part},
        {"preserve_interface_elements", options.preserve_interface_elements}
    };
    j["summary"] = {
        {"original_element_count", original_element_count},
        {"target_element_count", target_element_count},
        {"estimated_compression_ratio",
         target_element_count == 0 ? 0.0
                                   : static_cast<double>(original_element_count) /
                                         static_cast<double>(target_element_count)}
    };
    j["warnings"] = warnings;

    j["parts"] = nlohmann::json::array();
    for (const auto& part : parts) {
        nlohmann::json types = nlohmann::json::object();
        for (const auto& [type_id, count] : part.element_type_counts) {
            types[std::to_string(type_id)] = count;
        }
        j["parts"].push_back({
            {"part_name", part.part_name},
            {"original_element_count", part.original_element_count},
            {"target_element_count", part.target_element_count},
            {"element_type_counts", types},
            {"property_type", part.property_type},
            {"material_type", part.material_type},
            {"has_load", part.has_load},
            {"has_constraint", part.has_constraint},
            {"protected_shared_node_count", part.protected_shared_node_count},
            {"protected_contact_node_count", part.protected_contact_node_count},
            {"protected_loaded_node_count", part.protected_loaded_node_count},
            {"protected_constrained_node_count", part.protected_constrained_node_count}
        });
    }

    j["interfaces"] = nlohmann::json::array();
    for (const auto& sig : interfaces) {
        j["interfaces"].push_back({
            {"source_part", sig.source_part},
            {"target_part", sig.target_part},
            {"type", connection_type_to_string(sig.type)},
            {"sub_type", sig.sub_type},
            {"count", sig.count}
        });
    }
    return j;
}

nlohmann::json RemeshValidationResult::to_json() const {
    return {
        {"valid", valid},
        {"errors", errors},
        {"warnings", warnings}
    };
}

nlohmann::json RemeshExecutionResult::to_json() const {
    return {
        {"success", success},
        {"message", message},
        {"warnings", warnings},
        {"before", before.to_json()},
        {"after", after.to_json()},
        {"validation", validation.to_json()}
    };
}

bool ConnectionPreservingRemesher::write_plan_json(const RemeshPlan& plan,
                                                   const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) return false;
    out << plan.to_json().dump(2) << '\n';
    return static_cast<bool>(out);
}

RemeshExecutionResult
ConnectionPreservingRemesher::remesh_structured_hex8(DataContext& ctx,
                                                     SimdroidInspector& inspector,
                                                     const RemeshOptions& options) {
    RemeshExecutionResult result;
    auto& registry = ctx.registry;

    inspector.build(registry);
    result.before = build_plan(registry, inspector, options);

    if (result.before.parts.empty()) {
        result.message = "no parts to remesh";
        return result;
    }

    // Every part must be a single-type Hex8 structured grid.
    for (const auto& part_plan : result.before.parts) {
        if (part_plan.element_type_counts.size() != 1 ||
            part_plan.element_type_counts.find(308) == part_plan.element_type_counts.end()) {
            result.message = "structured Hex8 remesh requires every part to use only Hex8 (308): " + part_plan.part_name;
            return result;
        }
        if (part_plan.original_element_count <= 0 || part_plan.target_element_count <= 0) {
            result.message = "part has no remeshable elements: " + part_plan.part_name;
            return result;
        }
    }

    // Per-part structured grid extraction.
    struct PartMeshInfo {
        const Component::SimdroidPart* part = nullptr;
        StructuredGridInfo grid;
        std::array<int, 3> original_cells{0, 0, 0};
        std::array<int, 3> target_cells{0, 0, 0};
        std::vector<int> ix, iy, iz;
        std::vector<entt::entity> new_nodes;
        std::vector<entt::entity> new_elements;
    };
    std::vector<PartMeshInfo> part_infos;
    part_infos.reserve(result.before.parts.size());

    {
        auto part_view = registry.view<const Component::SimdroidPart>();
        for (auto part_entity : part_view) {
            const auto& part = part_view.get<const Component::SimdroidPart>(part_entity);

            const PartRemeshPlan* pp = nullptr;
            for (const auto& candidate : result.before.parts) {
                if (candidate.part_name == part.name) { pp = &candidate; break; }
            }
            if (pp == nullptr) continue;

            if (!registry.valid(part.element_set) ||
                !registry.all_of<Component::ElementSetMembers>(part.element_set)) {
                result.message = "Part element set is missing: " + part.name;
                return result;
            }

            PartMeshInfo info;
            info.part = &part;

            std::set<entt::entity> part_node_set;
            const auto& elem_members = registry.get<Component::ElementSetMembers>(part.element_set).members;
            for (auto elem_entity : elem_members) {
                if (!registry.valid(elem_entity) ||
                    !registry.all_of<Component::Connectivity>(elem_entity)) continue;
                for (auto node_entity : registry.get<Component::Connectivity>(elem_entity).nodes) {
                    if (registry.valid(node_entity) && registry.all_of<Component::Position>(node_entity)) {
                        part_node_set.insert(node_entity);
                    }
                }
            }

            for (auto node_entity : part_node_set) {
                const auto& p = registry.get<Component::Position>(node_entity);
                info.grid.xs.push_back(p.x);
                info.grid.ys.push_back(p.y);
                info.grid.zs.push_back(p.z);
            }
            sort_unique_coords(info.grid.xs);
            sort_unique_coords(info.grid.ys);
            sort_unique_coords(info.grid.zs);

            if (info.grid.xs.size() < 2 || info.grid.ys.size() < 2 || info.grid.zs.size() < 2) {
                result.message = "node coordinates do not form a 3D grid for part: " + part.name;
                return result;
            }

            info.original_cells = {
                static_cast<int>(info.grid.xs.size()) - 1,
                static_cast<int>(info.grid.ys.size()) - 1,
                static_cast<int>(info.grid.zs.size()) - 1
            };
            if (info.original_cells[0] * info.original_cells[1] * info.original_cells[2] != pp->original_element_count) {
                result.message = "Hex8 element count does not match a complete structured grid for part: " + part.name;
                return result;
            }

            info.target_cells = choose_target_cells(info.original_cells, pp->target_element_count);
            info.ix = choose_axis_indices(info.original_cells[0], info.target_cells[0]);
            info.iy = choose_axis_indices(info.original_cells[1], info.target_cells[1]);
            info.iz = choose_axis_indices(info.original_cells[2], info.target_cells[2]);

            part_infos.push_back(std::move(info));
        }
    }

    if (part_infos.size() != result.before.parts.size()) {
        result.message = "could not build structured grid info for every part";
        return result;
    }

    struct OldNodeSetSnapshot {
        entt::entity set_entity;
        std::vector<Component::Position> positions;
    };
    std::vector<OldNodeSetSnapshot> node_set_snapshots;
    {
        auto set_view = registry.view<Component::NodeSetMembers>();
        for (auto set_entity : set_view) {
            OldNodeSetSnapshot snapshot;
            snapshot.set_entity = set_entity;
            for (auto node : set_view.get<Component::NodeSetMembers>(set_entity).members) {
                if (registry.valid(node) && registry.all_of<Component::Position>(node)) {
                    snapshot.positions.push_back(registry.get<Component::Position>(node));
                }
            }
            node_set_snapshots.push_back(std::move(snapshot));
        }
    }

    // Snapshot surface sets by their corner-node positions so they can be rebuilt
    // via nearest-centroid matching after the new boundary surfaces are generated.
    struct SurfaceSetSnapshot {
        entt::entity set_entity;
        std::vector<std::vector<Component::Position>> surface_corners;
    };
    std::vector<SurfaceSetSnapshot> surface_set_snapshots;
    {
        auto set_view = registry.view<Component::SurfaceSetMembers>();
        for (auto set_entity : set_view) {
            SurfaceSetSnapshot snapshot;
            snapshot.set_entity = set_entity;
            for (auto surf : set_view.get<Component::SurfaceSetMembers>(set_entity).members) {
                if (!registry.valid(surf) || !registry.all_of<Component::SurfaceConnectivity>(surf)) continue;
                std::vector<Component::Position> corners;
                for (auto node : registry.get<Component::SurfaceConnectivity>(surf).nodes) {
                    if (registry.valid(node) && registry.all_of<Component::Position>(node)) {
                        corners.push_back(registry.get<Component::Position>(node));
                    }
                }
                if (!corners.empty()) snapshot.surface_corners.push_back(std::move(corners));
            }
            surface_set_snapshots.push_back(std::move(snapshot));
        }
    }

    std::vector<entt::entity> old_mesh_entities;
    {
        auto view = registry.view<Component::NodeID>();
        for (auto e : view) old_mesh_entities.push_back(e);
    }
    {
        auto view = registry.view<Component::ElementID>();
        for (auto e : view) append_unique(old_mesh_entities, e);
    }
    {
        auto view = registry.view<Component::SurfaceID>();
        for (auto e : view) append_unique(old_mesh_entities, e);
    }

    for (auto e : old_mesh_entities) {
        if (registry.valid(e)) registry.destroy(e);
    }

    if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        registry.ctx().erase<std::unique_ptr<TopologyData>>();
    }

    // Generate new nodes and elements per part with globally contiguous IDs.
    int next_node_id = 0;
    int next_element_id = 0;
    for (auto& info : part_infos) {
        const auto& g = info.grid;
        const auto& ix = info.ix;
        const auto& iy = info.iy;
        const auto& iz = info.iz;

        info.new_nodes.reserve(ix.size() * iy.size() * iz.size());
        auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity& {
            return info.new_nodes[(k * iy.size() + j) * ix.size() + i];
        };

        for (std::size_t k = 0; k < iz.size(); ++k) {
            for (std::size_t j = 0; j < iy.size(); ++j) {
                for (std::size_t i = 0; i < ix.size(); ++i) {
                    auto node = registry.create();
                    registry.emplace<Component::Position>(node, g.xs[ix[i]], g.ys[iy[j]], g.zs[iz[k]]);
                    registry.emplace<Component::NodeID>(node, next_node_id);
                    registry.emplace<Component::OriginalID>(node, next_node_id);
                    info.new_nodes.push_back(node);
                    ++next_node_id;
                }
            }
        }

        info.new_elements.reserve((ix.size() - 1) * (iy.size() - 1) * (iz.size() - 1));
        auto element_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity {
            return info.new_elements[(k * (iy.size() - 1) + j) * (ix.size() - 1) + i];
        };

        for (std::size_t k = 0; k + 1 < iz.size(); ++k) {
            for (std::size_t j = 0; j + 1 < iy.size(); ++j) {
                for (std::size_t i = 0; i + 1 < ix.size(); ++i) {
                    auto elem = registry.create();
                    registry.emplace<Component::ElementID>(elem, next_element_id);
                    registry.emplace<Component::OriginalID>(elem, next_element_id);
                    registry.emplace<Component::ElementType>(elem, 308);
                    registry.emplace<Component::PropertyRef>(elem, info.part->section);
                    registry.emplace<Component::Connectivity>(elem, Component::Connectivity{{
                        node_at(i,     j,     k),
                        node_at(i + 1, j,     k),
                        node_at(i + 1, j + 1, k),
                        node_at(i,     j + 1, k),
                        node_at(i,     j,     k + 1),
                        node_at(i + 1, j,     k + 1),
                        node_at(i + 1, j + 1, k + 1),
                        node_at(i,     j + 1, k + 1)
                    }});
                    info.new_elements.push_back(elem);
                    ++next_element_id;
                }
            }
        }

        if (registry.valid(info.part->element_set)) {
            auto& members = registry.get_or_emplace<Component::ElementSetMembers>(info.part->element_set).members;
            members = info.new_elements;
        }
    }

    // Rebuild element sets referenced by PartProperty (map EleSet name -> part's new elements).
    if (ctx.simdroid_blueprint.contains("PartProperty") &&
        ctx.simdroid_blueprint["PartProperty"].is_object()) {
        std::map<std::string, const std::vector<entt::entity>*> eset_by_name;
        for (const auto& info : part_infos) {
            if (registry.valid(info.part->element_set) &&
                registry.all_of<Component::SetName>(info.part->element_set)) {
                eset_by_name[registry.get<Component::SetName>(info.part->element_set).value] = &info.new_elements;
            }
        }
        for (auto it = ctx.simdroid_blueprint["PartProperty"].begin();
             it != ctx.simdroid_blueprint["PartProperty"].end(); ++it) {
            if (!it.value().is_object()) continue;
            const std::string ele_set_name = it.value().value("EleSet", "");
            if (ele_set_name.empty()) continue;
            auto mit = eset_by_name.find(ele_set_name);
            if (mit == eset_by_name.end()) continue;
            entt::entity set_entity = find_set_by_name(registry, ele_set_name);
            if (!registry.valid(set_entity)) continue;
            registry.get_or_emplace<Component::ElementSetMembers>(set_entity).members = *mit->second;
        }
    }

    const auto sorted_new_nodes = all_nodes_sorted(registry);
    for (const auto& snapshot : node_set_snapshots) {
        if (!registry.valid(snapshot.set_entity)) continue;
        auto& members = registry.get_or_emplace<Component::NodeSetMembers>(snapshot.set_entity).members;
        members.clear();
        for (const auto& pos : snapshot.positions) {
            append_unique(members, nearest_node(registry, sorted_new_nodes, pos));
        }
    }

    // Generate boundary surfaces per part with globally contiguous surface IDs.
    std::vector<entt::entity> all_new_surfaces;
    int next_surface_id = next_element_id;
    for (auto& info : part_infos) {
        const auto& ix = info.ix;
        const auto& iy = info.iy;
        const auto& iz = info.iz;
        auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity& {
            return info.new_nodes[(k * iy.size() + j) * ix.size() + i];
        };
        auto element_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity {
            return info.new_elements[(k * (iy.size() - 1) + j) * (ix.size() - 1) + i];
        };
        auto make_surface = [&](const std::vector<entt::entity>& nodes, entt::entity parent) {
            auto surf = registry.create();
            registry.emplace<Component::SurfaceID>(surf, next_surface_id);
            registry.emplace<Component::OriginalID>(surf, next_surface_id);
            registry.emplace<Component::SurfaceConnectivity>(surf, Component::SurfaceConnectivity{nodes});
            registry.emplace<Component::SurfaceParentElement>(surf, parent);
            all_new_surfaces.push_back(surf);
            ++next_surface_id;
        };

        const std::size_t nx = ix.size() - 1;
        const std::size_t ny = iy.size() - 1;
        const std::size_t nz = iz.size() - 1;
        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                make_surface({node_at(0, j, k), node_at(0, j + 1, k), node_at(0, j + 1, k + 1), node_at(0, j, k + 1)},
                             element_at(0, j, k));
                make_surface({node_at(nx, j, k), node_at(nx, j, k + 1), node_at(nx, j + 1, k + 1), node_at(nx, j + 1, k)},
                             element_at(nx - 1, j, k));
            }
        }
        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t i = 0; i < nx; ++i) {
                make_surface({node_at(i, 0, k), node_at(i, 0, k + 1), node_at(i + 1, 0, k + 1), node_at(i + 1, 0, k)},
                             element_at(i, 0, k));
                make_surface({node_at(i, ny, k), node_at(i + 1, ny, k), node_at(i + 1, ny, k + 1), node_at(i, ny, k + 1)},
                             element_at(i, ny - 1, k));
            }
        }
        for (std::size_t j = 0; j < ny; ++j) {
            for (std::size_t i = 0; i < nx; ++i) {
                make_surface({node_at(i, j, 0), node_at(i + 1, j, 0), node_at(i + 1, j + 1, 0), node_at(i, j + 1, 0)},
                             element_at(i, j, 0));
                make_surface({node_at(i, j, nz), node_at(i, j + 1, nz), node_at(i + 1, j + 1, nz), node_at(i + 1, j, nz)},
                             element_at(i, j, nz - 1));
            }
        }
    }

    // Precompute centroids of all new surfaces for nearest-match reconstruction.
    struct SurfaceCentroid {
        entt::entity surface;
        double cx, cy, cz;
    };
    std::vector<SurfaceCentroid> surface_centroids;
    surface_centroids.reserve(all_new_surfaces.size());
    for (auto surf : all_new_surfaces) {
        if (!registry.all_of<Component::SurfaceConnectivity>(surf)) continue;
        const auto& conn = registry.get<Component::SurfaceConnectivity>(surf).nodes;
        double cx = 0.0, cy = 0.0, cz = 0.0;
        int n = 0;
        for (auto node : conn) {
            if (!registry.all_of<Component::Position>(node)) continue;
            const auto& p = registry.get<Component::Position>(node);
            cx += p.x; cy += p.y; cz += p.z; ++n;
        }
        if (n == 0) continue;
        surface_centroids.push_back({surf, cx / n, cy / n, cz / n});
    }

    // Rebuild surface sets by nearest-centroid matching against the old snapshots.
    for (const auto& snapshot : surface_set_snapshots) {
        if (!registry.valid(snapshot.set_entity)) continue;
        auto& members = registry.get_or_emplace<Component::SurfaceSetMembers>(snapshot.set_entity).members;
        members.clear();
        for (const auto& corners : snapshot.surface_corners) {
            if (corners.empty() || surface_centroids.empty()) continue;
            double tcx = 0.0, tcy = 0.0, tcz = 0.0;
            for (const auto& p : corners) { tcx += p.x; tcy += p.y; tcz += p.z; }
            tcx /= static_cast<double>(corners.size());
            tcy /= static_cast<double>(corners.size());
            tcz /= static_cast<double>(corners.size());

            entt::entity best = entt::null;
            double best_d2 = std::numeric_limits<double>::max();
            for (const auto& sc : surface_centroids) {
                const double dx = sc.cx - tcx;
                const double dy = sc.cy - tcy;
                const double dz = sc.cz - tcz;
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best_d2) { best_d2 = d2; best = sc.surface; }
            }
            append_unique(members, best);
        }
    }

    reapply_loads_and_boundaries_from_blueprint(ctx);

    inspector.build(registry);
    result.after = build_plan(registry, inspector, options);
    result.validation = validate_preservation(result.before, result.after);

    auto detailed = validate_preservation_detailed(registry, ctx.simdroid_blueprint, result.after);
    result.validation.errors.insert(result.validation.errors.end(),
                                    detailed.errors.begin(), detailed.errors.end());
    result.validation.warnings.insert(result.validation.warnings.end(),
                                      detailed.warnings.begin(), detailed.warnings.end());
    result.validation.valid = result.validation.valid && detailed.valid;

    result.success = result.validation.valid;
    result.message = result.success ? "structured Hex8 remesh completed" : "preservation validation failed";
    result.warnings.push_back("structured Hex8 remesh is debug-only and only preserves coarse connectivity contracts");
    return result;
}

RemeshExecutionResult
ConnectionPreservingRemesher::remesh_structured_tet4(DataContext& ctx,
                                                     SimdroidInspector& inspector,
                                                     const RemeshOptions& options) {
    RemeshExecutionResult result;
    auto& registry = ctx.registry;

    inspector.build(registry);
    result.before = build_plan(registry, inspector, options);

    if (result.before.parts.empty()) {
        result.message = "no parts to remesh";
        return result;
    }

    // Every part must be a single-type Tet4 (304) or Tet10 (310) mesh.
    int output_element_type = 0;
    for (const auto& part_plan : result.before.parts) {
        if (part_plan.element_type_counts.size() != 1) {
            result.message = "structured Tet remesh requires every part to use a single element type: " + part_plan.part_name;
            return result;
        }
        const int type_id = part_plan.element_type_counts.begin()->first;
        if (type_id != 304 && type_id != 310) {
            result.message = "structured Tet remesh requires every part to use only Tet4 (304) or Tet10 (310): " + part_plan.part_name;
            return result;
        }
        if (output_element_type == 0) {
            output_element_type = type_id;
        } else if (output_element_type != type_id) {
            result.message = "structured Tet remesh requires all parts to use the same element type: " + part_plan.part_name;
            return result;
        }
        if (part_plan.original_element_count <= 0 || part_plan.target_element_count <= 0) {
            result.message = "part has no remeshable elements: " + part_plan.part_name;
            return result;
        }
    }

    // Per-part bounding-box structured grid generation.
    // For unstructured Tet4 we cannot extract a grid from node coordinates;
    // instead we compute the bounding box, estimate an equivalent structured
    // resolution from the aspect ratio, and generate uniform grid coordinates.
    struct PartMeshInfo {
        const Component::SimdroidPart* part = nullptr;
        StructuredGridInfo grid;
        std::array<int, 3> original_cells{0, 0, 0};
        std::array<int, 3> target_cells{0, 0, 0};
        std::vector<int> ix, iy, iz;
        std::vector<entt::entity> new_nodes;
        std::vector<entt::entity> new_elements;
    };
    std::vector<PartMeshInfo> part_infos;
    part_infos.reserve(result.before.parts.size());

    {
        auto part_view = registry.view<const Component::SimdroidPart>();
        for (auto part_entity : part_view) {
            const auto& part = part_view.get<const Component::SimdroidPart>(part_entity);

            const PartRemeshPlan* pp = nullptr;
            for (const auto& candidate : result.before.parts) {
                if (candidate.part_name == part.name) { pp = &candidate; break; }
            }
            if (pp == nullptr) continue;

            if (!registry.valid(part.element_set) ||
                !registry.all_of<Component::ElementSetMembers>(part.element_set)) {
                result.message = "Part element set is missing: " + part.name;
                return result;
            }

            PartMeshInfo info;
            info.part = &part;

            // Collect all part node positions and compute bounding box.
            double min_x = std::numeric_limits<double>::max();
            double min_y = std::numeric_limits<double>::max();
            double min_z = std::numeric_limits<double>::max();
            double max_x = std::numeric_limits<double>::lowest();
            double max_y = std::numeric_limits<double>::lowest();
            double max_z = std::numeric_limits<double>::lowest();

            std::set<entt::entity> part_node_set;
            const auto& elem_members = registry.get<Component::ElementSetMembers>(part.element_set).members;
            for (auto elem_entity : elem_members) {
                if (!registry.valid(elem_entity) ||
                    !registry.all_of<Component::Connectivity>(elem_entity)) continue;
                for (auto node_entity : registry.get<Component::Connectivity>(elem_entity).nodes) {
                    if (registry.valid(node_entity) && registry.all_of<Component::Position>(node_entity)) {
                        if (part_node_set.insert(node_entity).second) {
                            const auto& p = registry.get<Component::Position>(node_entity);
                            min_x = std::min(min_x, p.x); max_x = std::max(max_x, p.x);
                            min_y = std::min(min_y, p.y); max_y = std::max(max_y, p.y);
                            min_z = std::min(min_z, p.z); max_z = std::max(max_z, p.z);
                        }
                    }
                }
            }

            if (part_node_set.empty()) {
                result.message = "no nodes found for part: " + part.name;
                return result;
            }

            const double dx = max_x - min_x;
            const double dy = max_y - min_y;
            const double dz = max_z - min_z;
            const double max_dim = std::max({dx, dy, dz});
            if (max_dim <= 0.0) {
                result.message = "degenerate bounding box for part: " + part.name;
                return result;
            }

            // Estimate an equivalent structured resolution proportional to the
            // bounding box aspect ratio. The base resolution gives the longest
            // dimension a cell count of kBoundingBoxBase; shorter dimensions
            // are scaled proportionally.
            constexpr int kBoundingBoxBase = 100;
            info.original_cells = {
                std::max(1, static_cast<int>(std::round(kBoundingBoxBase * dx / max_dim))),
                std::max(1, static_cast<int>(std::round(kBoundingBoxBase * dy / max_dim))),
                std::max(1, static_cast<int>(std::round(kBoundingBoxBase * dz / max_dim)))
            };

            // Generate uniform grid coordinates within the bounding box.
            info.grid.xs.resize(static_cast<std::size_t>(info.original_cells[0]) + 1);
            info.grid.ys.resize(static_cast<std::size_t>(info.original_cells[1]) + 1);
            info.grid.zs.resize(static_cast<std::size_t>(info.original_cells[2]) + 1);
            for (int i = 0; i <= info.original_cells[0]; ++i)
                info.grid.xs[static_cast<std::size_t>(i)] = min_x + dx * i / info.original_cells[0];
            for (int j = 0; j <= info.original_cells[1]; ++j)
                info.grid.ys[static_cast<std::size_t>(j)] = min_y + dy * j / info.original_cells[1];
            for (int k = 0; k <= info.original_cells[2]; ++k)
                info.grid.zs[static_cast<std::size_t>(k)] = min_z + dz * k / info.original_cells[2];

            // Target Hex8 cell count = target Tet4 count / 6 (each Hex8 → 6 Tet4).
            const int target_hex = std::max(1, pp->target_element_count / 6);
            info.target_cells = choose_target_cells(info.original_cells, target_hex);
            info.ix = choose_axis_indices(info.original_cells[0], info.target_cells[0]);
            info.iy = choose_axis_indices(info.original_cells[1], info.target_cells[1]);
            info.iz = choose_axis_indices(info.original_cells[2], info.target_cells[2]);

            part_infos.push_back(std::move(info));
        }
    }

    if (part_infos.size() != result.before.parts.size()) {
        result.message = "could not build structured grid info for every part";
        return result;
    }

    struct OldNodeSetSnapshot {
        entt::entity set_entity;
        std::vector<Component::Position> positions;
    };
    std::vector<OldNodeSetSnapshot> node_set_snapshots;
    {
        auto set_view = registry.view<Component::NodeSetMembers>();
        for (auto set_entity : set_view) {
            OldNodeSetSnapshot snapshot;
            snapshot.set_entity = set_entity;
            for (auto node : set_view.get<Component::NodeSetMembers>(set_entity).members) {
                if (registry.valid(node) && registry.all_of<Component::Position>(node)) {
                    snapshot.positions.push_back(registry.get<Component::Position>(node));
                }
            }
            node_set_snapshots.push_back(std::move(snapshot));
        }
    }

    struct SurfaceSetSnapshot {
        entt::entity set_entity;
        std::vector<std::vector<Component::Position>> surface_corners;
    };
    std::vector<SurfaceSetSnapshot> surface_set_snapshots;
    {
        auto set_view = registry.view<Component::SurfaceSetMembers>();
        for (auto set_entity : set_view) {
            SurfaceSetSnapshot snapshot;
            snapshot.set_entity = set_entity;
            for (auto surf : set_view.get<Component::SurfaceSetMembers>(set_entity).members) {
                if (!registry.valid(surf) || !registry.all_of<Component::SurfaceConnectivity>(surf)) continue;
                std::vector<Component::Position> corners;
                for (auto node : registry.get<Component::SurfaceConnectivity>(surf).nodes) {
                    if (registry.valid(node) && registry.all_of<Component::Position>(node)) {
                        corners.push_back(registry.get<Component::Position>(node));
                    }
                }
                if (!corners.empty()) snapshot.surface_corners.push_back(std::move(corners));
            }
            surface_set_snapshots.push_back(std::move(snapshot));
        }
    }

    std::vector<entt::entity> old_mesh_entities;
    {
        auto view = registry.view<Component::NodeID>();
        for (auto e : view) old_mesh_entities.push_back(e);
    }
    {
        auto view = registry.view<Component::ElementID>();
        for (auto e : view) append_unique(old_mesh_entities, e);
    }
    {
        auto view = registry.view<Component::SurfaceID>();
        for (auto e : view) append_unique(old_mesh_entities, e);
    }

    for (auto e : old_mesh_entities) {
        if (registry.valid(e)) registry.destroy(e);
    }

    if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        registry.ctx().erase<std::unique_ptr<TopologyData>>();
    }

    // Generate new nodes and elements per part with globally contiguous IDs.
    int next_node_id = 0;
    int next_element_id = 0;
    for (auto& info : part_infos) {
        const auto& g = info.grid;
        const auto& ix = info.ix;
        const auto& iy = info.iy;
        const auto& iz = info.iz;

        info.new_nodes.reserve(ix.size() * iy.size() * iz.size());
        auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity& {
            return info.new_nodes[(k * iy.size() + j) * ix.size() + i];
        };

        for (std::size_t k = 0; k < iz.size(); ++k) {
            for (std::size_t j = 0; j < iy.size(); ++j) {
                for (std::size_t i = 0; i < ix.size(); ++i) {
                    auto node = registry.create();
                    registry.emplace<Component::Position>(node, g.xs[ix[i]], g.ys[iy[j]], g.zs[iz[k]]);
                    registry.emplace<Component::NodeID>(node, next_node_id);
                    registry.emplace<Component::OriginalID>(node, next_node_id);
                    info.new_nodes.push_back(node);
                    ++next_node_id;
                }
            }
        }

        // Each structured cell produces 6 Tet elements (Tet4 or Tet10).
        const std::size_t num_cells = (ix.size() - 1) * (iy.size() - 1) * (iz.size() - 1);
        info.new_elements.reserve(num_cells * 6);

        // For Tet10, midside nodes are shared across elements via this map.
        std::map<std::pair<entt::entity, entt::entity>, entt::entity> midside_nodes;
        auto get_or_create_midside = [&](entt::entity a, entt::entity b) -> entt::entity {
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            auto it = midside_nodes.find(key);
            if (it != midside_nodes.end()) return it->second;
            const auto& pa = registry.get<Component::Position>(a);
            const auto& pb = registry.get<Component::Position>(b);
            auto mid = registry.create();
            registry.emplace<Component::Position>(mid, (pa.x + pb.x) * 0.5,
                                                       (pa.y + pb.y) * 0.5,
                                                       (pa.z + pb.z) * 0.5);
            registry.emplace<Component::NodeID>(mid, next_node_id);
            registry.emplace<Component::OriginalID>(mid, next_node_id);
            ++next_node_id;
            midside_nodes[key] = mid;
            return mid;
        };

        for (std::size_t k = 0; k + 1 < iz.size(); ++k) {
            for (std::size_t j = 0; j + 1 < iy.size(); ++j) {
                for (std::size_t i = 0; i + 1 < ix.size(); ++i) {
                    entt::entity hex_nodes[8] = {
                        node_at(i,     j,     k),
                        node_at(i + 1, j,     k),
                        node_at(i + 1, j + 1, k),
                        node_at(i,     j + 1, k),
                        node_at(i,     j,     k + 1),
                        node_at(i + 1, j,     k + 1),
                        node_at(i + 1, j + 1, k + 1),
                        node_at(i,     j + 1, k + 1)
                    };
                    for (int t = 0; t < 6; ++t) {
                        const int a = kTetTable[t][0];
                        const int b = kTetTable[t][1];
                        const int c = kTetTable[t][2];
                        const int d = kTetTable[t][3];

                        auto elem = registry.create();
                        registry.emplace<Component::ElementID>(elem, next_element_id);
                        registry.emplace<Component::OriginalID>(elem, next_element_id);
                        registry.emplace<Component::ElementType>(elem, output_element_type);
                        registry.emplace<Component::PropertyRef>(elem, info.part->section);

                        if (output_element_type == 310) {
                            // Tet10: 4 corner nodes + 6 midside nodes.
                            // Ordering: [0..3]=corners, [4]=mid(0,1), [5]=mid(1,2),
                            //           [6]=mid(2,0), [7]=mid(0,3), [8]=mid(1,3), [9]=mid(2,3)
                            registry.emplace<Component::Connectivity>(elem, Component::Connectivity{{
                                hex_nodes[a], hex_nodes[b], hex_nodes[c], hex_nodes[d],
                                get_or_create_midside(hex_nodes[a], hex_nodes[b]),
                                get_or_create_midside(hex_nodes[b], hex_nodes[c]),
                                get_or_create_midside(hex_nodes[c], hex_nodes[a]),
                                get_or_create_midside(hex_nodes[a], hex_nodes[d]),
                                get_or_create_midside(hex_nodes[b], hex_nodes[d]),
                                get_or_create_midside(hex_nodes[c], hex_nodes[d])
                            }});
                        } else {
                            // Tet4: 4 corner nodes only.
                            registry.emplace<Component::Connectivity>(elem, Component::Connectivity{{
                                hex_nodes[a], hex_nodes[b], hex_nodes[c], hex_nodes[d]
                            }});
                        }
                        info.new_elements.push_back(elem);
                        ++next_element_id;
                    }
                }
            }
        }

        if (registry.valid(info.part->element_set)) {
            auto& members = registry.get_or_emplace<Component::ElementSetMembers>(info.part->element_set).members;
            members = info.new_elements;
        }
    }

    // Rebuild element sets referenced by PartProperty (map EleSet name -> part's new elements).
    if (ctx.simdroid_blueprint.contains("PartProperty") &&
        ctx.simdroid_blueprint["PartProperty"].is_object()) {
        std::map<std::string, const std::vector<entt::entity>*> eset_by_name;
        for (const auto& info : part_infos) {
            if (registry.valid(info.part->element_set) &&
                registry.all_of<Component::SetName>(info.part->element_set)) {
                eset_by_name[registry.get<Component::SetName>(info.part->element_set).value] = &info.new_elements;
            }
        }
        for (auto it = ctx.simdroid_blueprint["PartProperty"].begin();
             it != ctx.simdroid_blueprint["PartProperty"].end(); ++it) {
            if (!it.value().is_object()) continue;
            const std::string ele_set_name = it.value().value("EleSet", "");
            if (ele_set_name.empty()) continue;
            auto mit = eset_by_name.find(ele_set_name);
            if (mit == eset_by_name.end()) continue;
            entt::entity set_entity = find_set_by_name(registry, ele_set_name);
            if (!registry.valid(set_entity)) continue;
            registry.get_or_emplace<Component::ElementSetMembers>(set_entity).members = *mit->second;
        }
    }

    const auto sorted_new_nodes = all_nodes_sorted(registry);
    for (const auto& snapshot : node_set_snapshots) {
        if (!registry.valid(snapshot.set_entity)) continue;
        auto& members = registry.get_or_emplace<Component::NodeSetMembers>(snapshot.set_entity).members;
        members.clear();
        for (const auto& pos : snapshot.positions) {
            append_unique(members, nearest_node(registry, sorted_new_nodes, pos));
        }
    }

    // Generate boundary surfaces per part with globally contiguous surface IDs.
    // Each cell's first Tet4 is used as the SurfaceParentElement.
    std::vector<entt::entity> all_new_surfaces;
    int next_surface_id = next_element_id;
    for (auto& info : part_infos) {
        const auto& ix = info.ix;
        const auto& iy = info.iy;
        const auto& iz = info.iz;
        auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity& {
            return info.new_nodes[(k * iy.size() + j) * ix.size() + i];
        };
        auto element_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity {
            std::size_t cell_idx = (k * (iy.size() - 1) + j) * (ix.size() - 1) + i;
            return info.new_elements[cell_idx * 6];
        };
        auto make_surface = [&](const std::vector<entt::entity>& nodes, entt::entity parent) {
            auto surf = registry.create();
            registry.emplace<Component::SurfaceID>(surf, next_surface_id);
            registry.emplace<Component::OriginalID>(surf, next_surface_id);
            registry.emplace<Component::SurfaceConnectivity>(surf, Component::SurfaceConnectivity{nodes});
            registry.emplace<Component::SurfaceParentElement>(surf, parent);
            all_new_surfaces.push_back(surf);
            ++next_surface_id;
        };

        const std::size_t nx = ix.size() - 1;
        const std::size_t ny = iy.size() - 1;
        const std::size_t nz = iz.size() - 1;
        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t j = 0; j < ny; ++j) {
                make_surface({node_at(0, j, k), node_at(0, j + 1, k), node_at(0, j + 1, k + 1), node_at(0, j, k + 1)},
                             element_at(0, j, k));
                make_surface({node_at(nx, j, k), node_at(nx, j, k + 1), node_at(nx, j + 1, k + 1), node_at(nx, j + 1, k)},
                             element_at(nx - 1, j, k));
            }
        }
        for (std::size_t k = 0; k < nz; ++k) {
            for (std::size_t i = 0; i < nx; ++i) {
                make_surface({node_at(i, 0, k), node_at(i, 0, k + 1), node_at(i + 1, 0, k + 1), node_at(i + 1, 0, k)},
                             element_at(i, 0, k));
                make_surface({node_at(i, ny, k), node_at(i + 1, ny, k), node_at(i + 1, ny, k + 1), node_at(i, ny, k + 1)},
                             element_at(i, ny - 1, k));
            }
        }
        for (std::size_t j = 0; j < ny; ++j) {
            for (std::size_t i = 0; i < nx; ++i) {
                make_surface({node_at(i, j, 0), node_at(i + 1, j, 0), node_at(i + 1, j + 1, 0), node_at(i, j + 1, 0)},
                             element_at(i, j, 0));
                make_surface({node_at(i, j, nz), node_at(i, j + 1, nz), node_at(i + 1, j + 1, nz), node_at(i + 1, j, nz)},
                             element_at(i, j, nz - 1));
            }
        }
    }

    struct SurfaceCentroid {
        entt::entity surface;
        double cx, cy, cz;
    };
    std::vector<SurfaceCentroid> surface_centroids;
    surface_centroids.reserve(all_new_surfaces.size());
    for (auto surf : all_new_surfaces) {
        if (!registry.all_of<Component::SurfaceConnectivity>(surf)) continue;
        const auto& conn = registry.get<Component::SurfaceConnectivity>(surf).nodes;
        double cx = 0.0, cy = 0.0, cz = 0.0;
        int n = 0;
        for (auto node : conn) {
            if (!registry.all_of<Component::Position>(node)) continue;
            const auto& p = registry.get<Component::Position>(node);
            cx += p.x; cy += p.y; cz += p.z; ++n;
        }
        if (n == 0) continue;
        surface_centroids.push_back({surf, cx / n, cy / n, cz / n});
    }

    for (const auto& snapshot : surface_set_snapshots) {
        if (!registry.valid(snapshot.set_entity)) continue;
        auto& members = registry.get_or_emplace<Component::SurfaceSetMembers>(snapshot.set_entity).members;
        members.clear();
        for (const auto& corners : snapshot.surface_corners) {
            if (corners.empty() || surface_centroids.empty()) continue;
            double tcx = 0.0, tcy = 0.0, tcz = 0.0;
            for (const auto& p : corners) { tcx += p.x; tcy += p.y; tcz += p.z; }
            tcx /= static_cast<double>(corners.size());
            tcy /= static_cast<double>(corners.size());
            tcz /= static_cast<double>(corners.size());

            entt::entity best = entt::null;
            double best_d2 = std::numeric_limits<double>::max();
            for (const auto& sc : surface_centroids) {
                const double dx = sc.cx - tcx;
                const double dy = sc.cy - tcy;
                const double dz = sc.cz - tcz;
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 < best_d2) { best_d2 = d2; best = sc.surface; }
            }
            append_unique(members, best);
        }
    }

    reapply_loads_and_boundaries_from_blueprint(ctx);

    inspector.build(registry);
    result.after = build_plan(registry, inspector, options);
    result.validation = validate_preservation(result.before, result.after);

    auto detailed = validate_preservation_detailed(registry, ctx.simdroid_blueprint, result.after);
    result.validation.errors.insert(result.validation.errors.end(),
                                    detailed.errors.begin(), detailed.errors.end());
    result.validation.warnings.insert(result.validation.warnings.end(),
                                      detailed.warnings.begin(), detailed.warnings.end());
    result.validation.valid = result.validation.valid && detailed.valid;

    result.success = result.validation.valid;
    result.message = result.success ? "structured Tet remesh completed" : "preservation validation failed";
    result.warnings.push_back("structured Tet remesh is debug-only and only preserves coarse connectivity contracts");
    return result;
}

RemeshExecutionResult
ConnectionPreservingRemesher::remesh(DataContext& ctx,
                                     SimdroidInspector& inspector,
                                     const RemeshOptions& options) {
    inspector.build(ctx.registry);
    RemeshPlan plan = build_plan(ctx.registry, inspector, options);

    if (plan.parts.empty()) {
        RemeshExecutionResult result;
        result.message = "no parts to remesh";
        return result;
    }

    bool all_hex8 = true;
    bool all_tet = true;
    for (const auto& part : plan.parts) {
        if (part.element_type_counts.size() != 1) {
            all_hex8 = false;
            all_tet = false;
            break;
        }
        const int type_id = part.element_type_counts.begin()->first;
        if (type_id != 308) all_hex8 = false;
        if (type_id != 304 && type_id != 310) all_tet = false;
    }

    if (all_hex8) return remesh_structured_hex8(ctx, inspector, options);
    if (all_tet) return remesh_structured_tet4(ctx, inspector, options);

    RemeshExecutionResult result;
    result.message = "no suitable remesh strategy for mixed/unsupported element types";
    return result;
}
