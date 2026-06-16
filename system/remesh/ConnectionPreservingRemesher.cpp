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
            {"has_constraint", part.has_constraint}
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

    if (result.before.parts.size() != 1) {
        result.message = "structured Hex8 remesh currently supports exactly one Part";
        return result;
    }

    const auto& part_plan = result.before.parts.front();
    if (part_plan.element_type_counts.size() != 1 ||
        part_plan.element_type_counts.find(308) == part_plan.element_type_counts.end()) {
        result.message = "structured Hex8 remesh currently supports a single Hex8 element type";
        return result;
    }
    if (part_plan.original_element_count <= 0 || part_plan.target_element_count <= 0) {
        result.message = "model has no remeshable elements";
        return result;
    }

    auto part_view = registry.view<Component::SimdroidPart>();
    entt::entity part_entity = entt::null;
    Component::SimdroidPart* part = nullptr;
    for (auto e : part_view) {
        part_entity = e;
        part = &part_view.get<Component::SimdroidPart>(e);
        break;
    }
    if (part_entity == entt::null || part == nullptr ||
        !registry.valid(part->element_set) ||
        !registry.all_of<Component::ElementSetMembers>(part->element_set)) {
        result.message = "Part element set is missing";
        return result;
    }

    StructuredGridInfo grid;
    auto node_view = registry.view<const Component::Position, const Component::NodeID>();
    for (auto node : node_view) {
        const auto& p = node_view.get<const Component::Position>(node);
        unique_push_sorted(grid.xs, p.x);
        unique_push_sorted(grid.ys, p.y);
        unique_push_sorted(grid.zs, p.z);
    }
    sort_unique_coords(grid.xs);
    sort_unique_coords(grid.ys);
    sort_unique_coords(grid.zs);

    if (grid.xs.size() < 2 || grid.ys.size() < 2 || grid.zs.size() < 2) {
        result.message = "node coordinates do not form a 3D grid";
        return result;
    }

    const std::array<int, 3> original_cells{
        static_cast<int>(grid.xs.size()) - 1,
        static_cast<int>(grid.ys.size()) - 1,
        static_cast<int>(grid.zs.size()) - 1
    };
    if (original_cells[0] * original_cells[1] * original_cells[2] != part_plan.original_element_count) {
        result.message = "Hex8 element count does not match a complete structured coordinate grid";
        return result;
    }

    const std::array<int, 3> target_cells =
        choose_target_cells(original_cells, part_plan.target_element_count);
    const auto ix = choose_axis_indices(original_cells[0], target_cells[0]);
    const auto iy = choose_axis_indices(original_cells[1], target_cells[1]);
    const auto iz = choose_axis_indices(original_cells[2], target_cells[2]);

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

    std::vector<entt::entity> new_nodes;
    new_nodes.reserve(ix.size() * iy.size() * iz.size());
    auto node_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity& {
        return new_nodes[(k * iy.size() + j) * ix.size() + i];
    };

    int next_node_id = 0;
    for (std::size_t k = 0; k < iz.size(); ++k) {
        for (std::size_t j = 0; j < iy.size(); ++j) {
            for (std::size_t i = 0; i < ix.size(); ++i) {
                auto node = registry.create();
                registry.emplace<Component::Position>(node, grid.xs[ix[i]], grid.ys[iy[j]], grid.zs[iz[k]]);
                registry.emplace<Component::NodeID>(node, next_node_id);
                registry.emplace<Component::OriginalID>(node, next_node_id);
                new_nodes.push_back(node);
                ++next_node_id;
            }
        }
    }

    std::vector<entt::entity> new_elements;
    new_elements.reserve((ix.size() - 1) * (iy.size() - 1) * (iz.size() - 1));
    auto element_at = [&](std::size_t i, std::size_t j, std::size_t k) -> entt::entity {
        return new_elements[(k * (iy.size() - 1) + j) * (ix.size() - 1) + i];
    };

    int next_element_id = 0;
    for (std::size_t k = 0; k + 1 < iz.size(); ++k) {
        for (std::size_t j = 0; j + 1 < iy.size(); ++j) {
            for (std::size_t i = 0; i + 1 < ix.size(); ++i) {
                auto elem = registry.create();
                registry.emplace<Component::ElementID>(elem, next_element_id);
                registry.emplace<Component::OriginalID>(elem, next_element_id);
                registry.emplace<Component::ElementType>(elem, 308);
                registry.emplace<Component::PropertyRef>(elem, part->section);
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
                new_elements.push_back(elem);
                ++next_element_id;
            }
        }
    }

    const auto set_all_elements = [&](entt::entity set_entity) {
        if (!registry.valid(set_entity)) return;
        auto& members = registry.get_or_emplace<Component::ElementSetMembers>(set_entity).members;
        members = new_elements;
    };
    set_all_elements(part->element_set);

    if (ctx.simdroid_blueprint.contains("PartProperty") &&
        ctx.simdroid_blueprint["PartProperty"].is_object()) {
        for (auto it = ctx.simdroid_blueprint["PartProperty"].begin();
             it != ctx.simdroid_blueprint["PartProperty"].end(); ++it) {
            if (!it.value().is_object()) continue;
            const std::string ele_set_name = it.value().value("EleSet", "");
            if (ele_set_name.empty()) continue;
            set_all_elements(find_set_by_name(registry, ele_set_name));
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

    std::vector<entt::entity> new_surfaces;
    int next_surface_id = static_cast<int>(new_elements.size());
    auto make_surface = [&](const std::vector<entt::entity>& nodes, entt::entity parent) {
        auto surf = registry.create();
        registry.emplace<Component::SurfaceID>(surf, next_surface_id);
        registry.emplace<Component::OriginalID>(surf, next_surface_id);
        registry.emplace<Component::SurfaceConnectivity>(surf, Component::SurfaceConnectivity{nodes});
        registry.emplace<Component::SurfaceParentElement>(surf, parent);
        new_surfaces.push_back(surf);
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

    auto surface_set_view = registry.view<Component::SurfaceSetMembers>();
    for (auto set_entity : surface_set_view) {
        surface_set_view.get<Component::SurfaceSetMembers>(set_entity).members = new_surfaces;
    }

    reapply_loads_and_boundaries_from_blueprint(ctx);

    inspector.build(registry);
    result.after = build_plan(registry, inspector, options);
    result.validation = validate_preservation(result.before, result.after);
    result.success = result.validation.valid;
    result.message = result.success ? "structured Hex8 remesh completed" : "preservation validation failed";
    result.warnings.push_back("structured Hex8 remesh is debug-only and only preserves coarse connectivity contracts");
    return result;
}
