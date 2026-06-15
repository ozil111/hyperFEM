/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "remesh/ConnectionPreservingRemesher.h"

#include "analysis/GraphBuilder.h"
#include "components/material_components.h"
#include "components/mesh_components.h"
#include "components/property_components.h"
#include "components/simdroid_components.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>
#include <sstream>

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

bool ConnectionPreservingRemesher::write_plan_json(const RemeshPlan& plan,
                                                   const std::string& output_path) {
    std::ofstream out(output_path);
    if (!out) return false;
    out << plan.to_json().dump(2) << '\n';
    return static_cast<bool>(out);
}
