/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once
#include "PartGraph.h"
#include <entt/entt.hpp>
#include "../simdroid/SimdroidInspector.h"
#include "../../data_center/components/simdroid_components.h"
#include "../../data_center/components/load_components.h"
#include "../../data_center/components/material_components.h"
#include "../../data_center/components/property_components.h"
#include "../../data_center/components/mesh_components.h"
#include <set>
#include <map>
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>
#include <limits>
#include <numeric>
#include <spdlog/spdlog.h>

namespace Component {
    struct NodeID;
}

class GraphBuilder {
public:
    static PartGraph build(entt::registry& registry, SimdroidInspector& inspector,
                           double contact_gap = -1.0) {
        PartGraph graph;
        const double effective_contact_gap = contact_gap;

        // Ensure index is built
        if (!inspector.is_built) {
            inspector.build(registry);
        }

        // 1. Initialize nodes (Parts)
        auto view_parts = registry.view<const Component::SimdroidPart>();
        for (auto entity : view_parts) {
            const auto& part = view_parts.get<const Component::SimdroidPart>(entity);
            graph.add_node(part.name);
            auto& node = graph.nodes[part.name];

            // Get material info (e.g. IsotropicElastic)
            if (registry.valid(part.material) && registry.all_of<Component::MaterialModel>(part.material)) {
                node.material_info = registry.get<Component::MaterialModel>(part.material).value;
            }

            // Get property info: generate short summary based on different Property components
            if (registry.valid(part.section)) {
                const entt::entity sec = part.section;
                std::string info;

                if (registry.all_of<Component::ShellProperty>(sec)) {
                    const auto& p = registry.get<Component::ShellProperty>(sec);
                    info = "Shell: t=[" +
                           std::to_string(p.thickness[0]) + "," +
                           std::to_string(p.thickness[1]) + "," +
                           std::to_string(p.thickness[2]) + "," +
                           std::to_string(p.thickness[3]) + "], N=" +
                           std::to_string(p.integration_points);
                } else if (registry.all_of<Component::BeamProperty>(sec)) {
                    const auto& p = registry.get<Component::BeamProperty>(sec);
                    info = "Beam: A=" + std::to_string(p.area) +
                           ", Iyy=" + std::to_string(p.iyy) +
                           ", Izz=" + std::to_string(p.izz);
                } else if (registry.all_of<Component::FiberBeamProperty>(sec)) {
                    const auto& p = registry.get<Component::FiberBeamProperty>(sec);
                    info = "FiberBeam: pattern=" + p.pattern +
                           ", NIP=" + std::to_string(p.integration_points);
                } else if (registry.all_of<Component::AxialSpringDamperProperty>(sec)) {
                    const auto& p = registry.get<Component::AxialSpringDamperProperty>(sec);
                    info = "Spring: K=" + std::to_string(p.stiffness) +
                           ", C=" + std::to_string(p.damping);
                } else if (registry.all_of<Component::BeamSpringProperty>(sec)) {
                    const auto& p = registry.get<Component::BeamSpringProperty>(sec);
                    info = "BeamSpring: K[0]=" + std::to_string(p.linear_stiffness[0]);
                } else if (registry.all_of<Component::SolidShCompProperty>(sec)) {
                    const auto& p = registry.get<Component::SolidShCompProperty>(sec);
                    info = "SolidShComp: layers=" + std::to_string(p.layer_thicks.size());
                } else if (registry.all_of<Component::SolidShellProperty>(sec)) {
                    const auto& p = registry.get<Component::SolidShellProperty>(sec);
                    info = "SolidShell: Inpts=[" +
                           std::to_string(p.integration_points[0]) + "," +
                           std::to_string(p.integration_points[1]) + "," +
                           std::to_string(p.integration_points[2]) + "]";
                } else if (registry.all_of<Component::SolidProperty>(sec)) {
                    info = "Solid: type=" + std::to_string(registry.get<Component::SolidProperty>(sec).type_id);
                    if (registry.all_of<Component::Formulation>(sec))
                        info += ", form=" + registry.get<Component::Formulation>(sec).value;
                    if (registry.all_of<Component::IntegrationPoints>(sec))
                        info += ", npts=" + std::to_string(registry.get<Component::IntegrationPoints>(sec).value);
                    if (registry.all_of<Component::HourglassControl>(sec))
                        info += ", hg=" + registry.get<Component::HourglassControl>(sec).value;
                }

                node.property_info = std::move(info);
            }
        }

        // -------------------------------------------------------
        // 2. Process Contacts (explicit connections)
        //    Also read ContactTypeTag for contact type details (Tie / Type7 / Type24)
        // -------------------------------------------------------
        auto view_contacts = registry.view<const Component::ContactBase, const Component::ContactTypeTag>();
        for (auto entity : view_contacts) {
            const auto& contact = view_contacts.get<const Component::ContactBase>(entity);
            const auto& type_tag = view_contacts.get<const Component::ContactTypeTag>(entity);

            // Map ContactInterType to specific algorithm name
            // /INTER/TYPE2 (Tie), /INTER/TYPE7 (N-S), /INTER/TYPE24 (General)
            std::string sub_type;
            switch (type_tag.type) {
                case Component::ContactInterType::Tie:
                    sub_type = "Tie";
                    break;
                case Component::ContactInterType::NodeToSurface:
                    sub_type = "Type7";
                    break;
                case Component::ContactInterType::General:
                    sub_type = "Type24";
                    break;
                default:
                    sub_type = "Unknown";
                    break;
            }

            auto master_parts = get_parts_from_set(registry, inspector, contact.master_entity);
            auto slave_parts  = get_parts_from_set(registry, inspector, contact.slave_entity);

            // GeneralContact self-contact (slave_entity == null): spatial proximity search
            if (type_tag.type == Component::ContactInterType::General &&
                contact.slave_entity == entt::null && master_parts.size() > 1) {
                double gap = std::isfinite(effective_contact_gap) && effective_contact_gap > 0.0
                                 ? effective_contact_gap
                                 : contact_defined_gap(registry, entity);
                if (!(gap > 0.0)) {
                    gap = auto_calculate_contact_gap(registry, inspector, contact.master_entity);
                    spdlog::info("GeneralContact '{}': auto contact gap = {:.6f}", contact.name, gap);
                } else {
                    spdlog::info("GeneralContact '{}': configured contact gap = {:.6f}", contact.name, gap);
                }
                auto pairs = detect_general_contact_pairs(
                    registry, inspector, contact.master_entity, gap);
                for (const auto& [pa, pb] : pairs) {
                    graph.add_edge(pa, pb, ConnectionType::Contact, 1.0, 1, "GeneralContact");
                    graph.add_edge(pb, pa, ConnectionType::Contact, 1.0, 1, "GeneralContact");
                }
                spdlog::info("GeneralContact '{}': found {} contact pairs (gap={:.4f})",
                             contact.name, pairs.size(), gap);
            } else {
                for (const auto& m : master_parts) {
                    for (const auto& s : slave_parts) {
                        if (m != s) {
                            // Contact connections are typically "strong", weight set low (1.0)
                            graph.add_edge(m, s, ConnectionType::Contact, 1.0, 1, sub_type);
                            graph.add_edge(s, m, ConnectionType::Contact, 1.0, 1, sub_type);
                        }
                    }
                }
            }
        }

        // -------------------------------------------------------
        // 3. Process Shared Nodes (implicit topological connections) - core algorithm
        // -------------------------------------------------------
        // Logic: iterate each node -> find which Parts it belongs to -> if >1 Parts, connect them
        
        // Temporary storage: pair<PartA, PartB> -> SharedNodeCount
        std::map<std::pair<std::string, std::string>, int> shared_topology_map;

        for (const auto& [nid, elem_ids] : inspector.nid_to_elems) {
            // If a node is referenced by only one element or only elements from the same Part, no connection
            if (elem_ids.empty()) continue;

            // Collect all Parts that reference this node
            std::vector<std::string> parts_sharing_this_node;
            parts_sharing_this_node.reserve(4); // Reserve a small amount, usually no more than 4

            for (int eid : elem_ids) {
                // Use Inspector's O(1) lookup
                if (inspector.eid_to_part.count(eid)) {
                    const std::string& p_name = inspector.eid_to_part.at(eid);
                    // Avoid duplicates (std::unique requires sorting, manual check is faster here)
                    bool already_added = false;
                    for (const auto& existing : parts_sharing_this_node) {
                        if (existing == p_name) { already_added = true; break; }
                    }
                    if (!already_added) {
                        parts_sharing_this_node.push_back(p_name);
                    }
                }
            }

            // If this node is shared by multiple Parts, establish pairwise connections
            if (parts_sharing_this_node.size() > 1) {
                // Sort to ensure pair key consistency: (A, B) vs (B, A)
                std::sort(parts_sharing_this_node.begin(), parts_sharing_this_node.end());
                
                for (size_t i = 0; i < parts_sharing_this_node.size(); ++i) {
                    for (size_t j = i + 1; j < parts_sharing_this_node.size(); ++j) {
                        shared_topology_map[{parts_sharing_this_node[i], parts_sharing_this_node[j]}]++;
                    }
                }
            }
        }

        // Write statistics to Graph
        for (const auto& [pair, count] : shared_topology_map) {
            // Threshold filtering: e.g. fewer than 3 shared nodes may be noise? Keep all for now
            // Shared Node weight logic: more shared nodes = tighter connection (lower weight)
            double weight = (count > 100) ? 0.1 : (count > 10 ? 0.5 : 2.0);
            
            graph.add_edge(pair.first, pair.second, ConnectionType::SharedNode, weight, count);
            graph.add_edge(pair.second, pair.first, ConnectionType::SharedNode, weight, count);
        }

        // -------------------------------------------------------
        // 4. Mark Load / Constraint Part membership
        //    Map loaded/constrained nodes to their owning PartNodes
        //    via node -> element -> Part mapping
        // -------------------------------------------------------
        {
            // 4.1 Process Nodal Load -> is_load_part
            auto view_node_load = registry.view<const Component::AppliedLoadRef, const Component::NodeID>();
            for (auto entity : view_node_load) {
                const int nid = view_node_load.get<const Component::NodeID>(entity).value;
                auto it_ne = inspector.nid_to_elems.find(nid);
                if (it_ne == inspector.nid_to_elems.end()) continue;

                for (int eid : it_ne->second) {
                    auto it_part = inspector.eid_to_part.find(eid);
                    if (it_part == inspector.eid_to_part.end()) continue;
                    auto node_it = graph.nodes.find(it_part->second);
                    if (node_it != graph.nodes.end()) {
                        node_it->second.is_load_part = true;
                    }
                }
            }

            // 4.2 Process Boundary conditions -> is_constraint_part
            auto view_node_fix = registry.view<const Component::AppliedBoundaryRef, const Component::NodeID>();
            for (auto entity : view_node_fix) {
                const int nid = view_node_fix.get<const Component::NodeID>(entity).value;
                auto it_ne = inspector.nid_to_elems.find(nid);
                if (it_ne == inspector.nid_to_elems.end()) continue;

                for (int eid : it_ne->second) {
                    auto it_part = inspector.eid_to_part.find(eid);
                    if (it_part == inspector.eid_to_part.end()) continue;
                    auto node_it = graph.nodes.find(it_part->second);
                    if (node_it != graph.nodes.end()) {
                        node_it->second.is_constraint_part = true;
                    }
                }
            }
        }

        return graph;
    }

private:
    // Helper: resolve Parts contained in a Set Entity
    static std::vector<std::string> get_parts_from_set(entt::registry& reg, SimdroidInspector& insp, entt::entity set_entity) {
        std::vector<std::string> parts;
        if (!reg.valid(set_entity)) return parts;

        std::set<std::string> unique_parts;

        // 1) Element Set: directly via ElementID / OriginalID -> Part
        if (reg.all_of<Component::ElementSetMembers>(set_entity)) {
            const auto& members = reg.get<Component::ElementSetMembers>(set_entity).members;
            for (auto ent : members) {
                int eid = -1;
                if (reg.all_of<Component::ElementID>(ent)) {
                    eid = reg.get<Component::ElementID>(ent).value;
                } else if (reg.all_of<Component::OriginalID>(ent)) {
                    eid = reg.get<Component::OriginalID>(ent).value;
                }
                if (eid >= 0 && insp.eid_to_part.count(eid)) {
                    unique_parts.insert(insp.eid_to_part.at(eid));
                }
            }
        }

        // 2) Node Set: via NodeID -> nid_to_elems -> eid_to_part
        if (reg.all_of<Component::NodeSetMembers>(set_entity)) {
            const auto& members = reg.get<Component::NodeSetMembers>(set_entity).members;
            for (auto node_ent : members) {
                if (!reg.valid(node_ent) || !reg.all_of<Component::NodeID>(node_ent)) continue;
                int nid = reg.get<Component::NodeID>(node_ent).value;
                auto it = insp.nid_to_elems.find(nid);
                if (it == insp.nid_to_elems.end()) continue;
                for (int eid : it->second) {
                    if (insp.eid_to_part.count(eid)) {
                        unique_parts.insert(insp.eid_to_part.at(eid));
                    }
                }
            }
        }

        // 3) Surface Set: via SurfaceParentElement -> ElementID -> eid_to_part
        if (reg.all_of<Component::SurfaceSetMembers>(set_entity)) {
            const auto& members = reg.get<Component::SurfaceSetMembers>(set_entity).members;
            for (auto surf_ent : members) {
                if (!reg.valid(surf_ent) || !reg.all_of<Component::SurfaceParentElement>(surf_ent)) continue;
                entt::entity parent_elem = reg.get<Component::SurfaceParentElement>(surf_ent).element;
                if (!reg.valid(parent_elem)) continue;

                int eid = -1;
                if (reg.all_of<Component::ElementID>(parent_elem)) {
                    eid = reg.get<Component::ElementID>(parent_elem).value;
                } else if (reg.all_of<Component::OriginalID>(parent_elem)) {
                    eid = reg.get<Component::OriginalID>(parent_elem).value;
                }
                if (eid >= 0 && insp.eid_to_part.count(eid)) {
                    unique_parts.insert(insp.eid_to_part.at(eid));
                }
            }
        }

        parts.assign(unique_parts.begin(), unique_parts.end());
        return parts;
    }

    // -----------------------------------------------------------------
    // GeneralContact spatial proximity contact search
    // -----------------------------------------------------------------

    using Vec3 = std::array<double, 3>;

    struct AABB {
        Vec3 min{{std::numeric_limits<double>::max(), std::numeric_limits<double>::max(),
                  std::numeric_limits<double>::max()}};
        Vec3 max{{std::numeric_limits<double>::lowest(), std::numeric_limits<double>::lowest(),
                  std::numeric_limits<double>::lowest()}};

        void expand(const Vec3& p) {
            for (int axis = 0; axis < 3; ++axis) {
                min[axis] = std::min(min[axis], p[axis]);
                max[axis] = std::max(max[axis], p[axis]);
            }
        }

        void expand(const AABB& other) {
            expand(other.min);
            expand(other.max);
        }
    };

    struct Triangle {
        Vec3 p[3];
        AABB box;
        Vec3 centroid;
    };

    struct BvhNode {
        AABB box;
        size_t begin = 0;
        size_t end = 0;
        int left = -1;
        int right = -1;

        bool is_leaf() const { return left < 0; }
    };

    struct PartSurface {
        std::vector<Triangle> triangles;
        std::vector<size_t> order;
        std::vector<BvhNode> bvh;
        AABB box;
    };

    static Vec3 subtract(const Vec3& a, const Vec3& b) {
        return {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    }

    static Vec3 add(const Vec3& a, const Vec3& b) {
        return {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
    }

    static Vec3 scale(const Vec3& a, double value) {
        return {a[0] * value, a[1] * value, a[2] * value};
    }

    static double dot(const Vec3& a, const Vec3& b) {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    }

    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {a[1] * b[2] - a[2] * b[1],
                a[2] * b[0] - a[0] * b[2],
                a[0] * b[1] - a[1] * b[0]};
    }

    static double norm_squared(const Vec3& a) { return dot(a, a); }

    static double aabb_distance_squared(const AABB& a, const AABB& b) {
        double distance_squared = 0.0;
        for (int axis = 0; axis < 3; ++axis) {
            double delta = 0.0;
            if (a.max[axis] < b.min[axis]) delta = b.min[axis] - a.max[axis];
            else if (b.max[axis] < a.min[axis]) delta = a.min[axis] - b.max[axis];
            distance_squared += delta * delta;
        }
        return distance_squared;
    }

    static double segment_distance_squared(const Vec3& p1, const Vec3& q1,
                                           const Vec3& p2, const Vec3& q2) {
        constexpr double eps = 1e-30;
        const Vec3 d1 = subtract(q1, p1);
        const Vec3 d2 = subtract(q2, p2);
        const Vec3 r = subtract(p1, p2);
        const double a = dot(d1, d1);
        const double e = dot(d2, d2);
        const double f = dot(d2, r);
        double s = 0.0;
        double t = 0.0;

        if (a <= eps && e <= eps) return norm_squared(r);
        if (a <= eps) {
            t = std::clamp(f / e, 0.0, 1.0);
        } else {
            const double c = dot(d1, r);
            if (e <= eps) {
                s = std::clamp(-c / a, 0.0, 1.0);
            } else {
                const double b = dot(d1, d2);
                const double denominator = a * e - b * b;
                if (denominator > eps) s = std::clamp((b * f - c * e) / denominator, 0.0, 1.0);
                t = (b * s + f) / e;
                if (t < 0.0) {
                    t = 0.0;
                    s = std::clamp(-c / a, 0.0, 1.0);
                } else if (t > 1.0) {
                    t = 1.0;
                    s = std::clamp((b - c) / a, 0.0, 1.0);
                }
            }
        }

        return norm_squared(subtract(add(p1, scale(d1, s)), add(p2, scale(d2, t))));
    }

    static double point_triangle_distance_squared(const Vec3& point, const Triangle& triangle) {
        const Vec3 ab = subtract(triangle.p[1], triangle.p[0]);
        const Vec3 ac = subtract(triangle.p[2], triangle.p[0]);
        if (norm_squared(cross(ab, ac)) <= 1e-28) {
            return std::min({segment_distance_squared(point, point, triangle.p[0], triangle.p[1]),
                             segment_distance_squared(point, point, triangle.p[1], triangle.p[2]),
                             segment_distance_squared(point, point, triangle.p[2], triangle.p[0])});
        }

        const Vec3 ap = subtract(point, triangle.p[0]);
        const double d1 = dot(ab, ap);
        const double d2 = dot(ac, ap);
        if (d1 <= 0.0 && d2 <= 0.0) return norm_squared(ap);

        const Vec3 bp = subtract(point, triangle.p[1]);
        const double d3 = dot(ab, bp);
        const double d4 = dot(ac, bp);
        if (d3 >= 0.0 && d4 <= d3) return norm_squared(bp);

        const double vc = d1 * d4 - d3 * d2;
        if (vc <= 0.0 && d1 >= 0.0 && d3 <= 0.0) {
            const double v = d1 / (d1 - d3);
            return norm_squared(subtract(point, add(triangle.p[0], scale(ab, v))));
        }

        const Vec3 cp = subtract(point, triangle.p[2]);
        const double d5 = dot(ab, cp);
        const double d6 = dot(ac, cp);
        if (d6 >= 0.0 && d5 <= d6) return norm_squared(cp);

        const double vb = d5 * d2 - d1 * d6;
        if (vb <= 0.0 && d2 >= 0.0 && d6 <= 0.0) {
            const double w = d2 / (d2 - d6);
            return norm_squared(subtract(point, add(triangle.p[0], scale(ac, w))));
        }

        const double va = d3 * d6 - d5 * d4;
        if (va <= 0.0 && (d4 - d3) >= 0.0 && (d5 - d6) >= 0.0) {
            const Vec3 bc = subtract(triangle.p[2], triangle.p[1]);
            const double w = (d4 - d3) / ((d4 - d3) + (d5 - d6));
            return norm_squared(subtract(point, add(triangle.p[1], scale(bc, w))));
        }

        const double denominator = 1.0 / (va + vb + vc);
        const double v = vb * denominator;
        const double w = vc * denominator;
        return norm_squared(subtract(point,
            add(triangle.p[0], add(scale(ab, v), scale(ac, w)))));
    }

    static bool segment_intersects_triangle(const Vec3& start, const Vec3& end,
                                            const Triangle& triangle) {
        constexpr double eps = 1e-12;
        const Vec3 direction = subtract(end, start);
        const Vec3 edge1 = subtract(triangle.p[1], triangle.p[0]);
        const Vec3 edge2 = subtract(triangle.p[2], triangle.p[0]);
        const Vec3 h = cross(direction, edge2);
        const double determinant = dot(edge1, h);
        if (std::abs(determinant) <= eps) return false;
        const double inverse = 1.0 / determinant;
        const Vec3 s = subtract(start, triangle.p[0]);
        const double u = inverse * dot(s, h);
        if (u < -eps || u > 1.0 + eps) return false;
        const Vec3 q = cross(s, edge1);
        const double v = inverse * dot(direction, q);
        if (v < -eps || u + v > 1.0 + eps) return false;
        const double t = inverse * dot(edge2, q);
        return t >= -eps && t <= 1.0 + eps;
    }

    static double triangle_distance_squared(const Triangle& a, const Triangle& b) {
        for (int edge = 0; edge < 3; ++edge) {
            if (segment_intersects_triangle(a.p[edge], a.p[(edge + 1) % 3], b) ||
                segment_intersects_triangle(b.p[edge], b.p[(edge + 1) % 3], a))
                return 0.0;
        }

        double best = std::numeric_limits<double>::max();
        for (int i = 0; i < 3; ++i) {
            best = std::min(best, point_triangle_distance_squared(a.p[i], b));
            best = std::min(best, point_triangle_distance_squared(b.p[i], a));
            for (int j = 0; j < 3; ++j) {
                best = std::min(best, segment_distance_squared(
                    a.p[i], a.p[(i + 1) % 3], b.p[j], b.p[(j + 1) % 3]));
            }
        }
        return best;
    }

    static Triangle make_triangle(const Vec3& a, const Vec3& b, const Vec3& c) {
        Triangle triangle{{a, b, c}, {}, {}};
        triangle.box.expand(a);
        triangle.box.expand(b);
        triangle.box.expand(c);
        triangle.centroid = scale(add(add(a, b), c), 1.0 / 3.0);
        return triangle;
    }

    static std::unordered_map<std::string, PartSurface>
    collect_part_surfaces(entt::registry& reg, SimdroidInspector& insp,
                          entt::entity surface_set_entity) {
        std::unordered_map<std::string, PartSurface> surfaces;
        if (!reg.valid(surface_set_entity) ||
            !reg.all_of<Component::SurfaceSetMembers>(surface_set_entity))
            return surfaces;

        const auto& members = reg.get<Component::SurfaceSetMembers>(surface_set_entity).members;
        for (auto surface_entity : members) {
            if (!reg.valid(surface_entity) ||
                !reg.all_of<Component::SurfaceConnectivity, Component::SurfaceParentElement>(surface_entity))
                continue;

            const entt::entity parent = reg.get<Component::SurfaceParentElement>(surface_entity).element;
            if (!reg.valid(parent)) continue;
            int element_id = -1;
            if (reg.all_of<Component::ElementID>(parent))
                element_id = reg.get<Component::ElementID>(parent).value;
            else if (reg.all_of<Component::OriginalID>(parent))
                element_id = reg.get<Component::OriginalID>(parent).value;
            const auto part_it = insp.eid_to_part.find(element_id);
            if (part_it == insp.eid_to_part.end()) continue;

            std::vector<Vec3> points;
            const auto& connectivity = reg.get<Component::SurfaceConnectivity>(surface_entity);
            points.reserve(connectivity.nodes.size());
            for (auto node : connectivity.nodes) {
                if (!reg.valid(node) || !reg.all_of<Component::Position>(node)) continue;
                const auto& position = reg.get<Component::Position>(node);
                points.push_back({position.x, position.y, position.z});
            }
            if (points.size() < 3) continue;

            auto& part_surface = surfaces[part_it->second];
            for (size_t i = 1; i + 1 < points.size(); ++i) {
                Triangle triangle = make_triangle(points[0], points[i], points[i + 1]);
                part_surface.box.expand(triangle.box);
                part_surface.triangles.push_back(std::move(triangle));
            }
        }
        return surfaces;
    }

    static int build_bvh_node(PartSurface& surface, size_t begin, size_t end) {
        const int node_index = static_cast<int>(surface.bvh.size());
        surface.bvh.push_back({});
        AABB box;
        AABB centroid_box;
        for (size_t i = begin; i < end; ++i) {
            const Triangle& triangle = surface.triangles[surface.order[i]];
            box.expand(triangle.box);
            centroid_box.expand(triangle.centroid);
        }
        surface.bvh[node_index].box = box;
        surface.bvh[node_index].begin = begin;
        surface.bvh[node_index].end = end;

        if (end - begin <= 8) return node_index;
        int axis = 0;
        for (int candidate = 1; candidate < 3; ++candidate) {
            if (centroid_box.max[candidate] - centroid_box.min[candidate] >
                centroid_box.max[axis] - centroid_box.min[axis])
                axis = candidate;
        }
        const size_t middle = begin + (end - begin) / 2;
        std::nth_element(surface.order.begin() + begin, surface.order.begin() + middle,
                         surface.order.begin() + end,
                         [&](size_t lhs, size_t rhs) {
                             return surface.triangles[lhs].centroid[axis] <
                                    surface.triangles[rhs].centroid[axis];
                         });
        const int left = build_bvh_node(surface, begin, middle);
        const int right = build_bvh_node(surface, middle, end);
        surface.bvh[node_index].left = left;
        surface.bvh[node_index].right = right;
        return node_index;
    }

    static void build_bvh(PartSurface& surface) {
        surface.order.resize(surface.triangles.size());
        std::iota(surface.order.begin(), surface.order.end(), size_t{0});
        surface.bvh.reserve(surface.triangles.size() * 2);
        if (!surface.triangles.empty()) build_bvh_node(surface, 0, surface.triangles.size());
    }

    static bool bvh_surfaces_within_gap(const PartSurface& a, const PartSurface& b,
                                        double gap_squared) {
        if (a.bvh.empty() || b.bvh.empty()) return false;
        std::vector<std::pair<int, int>> stack{{0, 0}};
        while (!stack.empty()) {
            const auto [a_index, b_index] = stack.back();
            stack.pop_back();
            const BvhNode& an = a.bvh[a_index];
            const BvhNode& bn = b.bvh[b_index];
            if (aabb_distance_squared(an.box, bn.box) > gap_squared) continue;

            if (an.is_leaf() && bn.is_leaf()) {
                for (size_t ai = an.begin; ai < an.end; ++ai) {
                    const Triangle& at = a.triangles[a.order[ai]];
                    for (size_t bi = bn.begin; bi < bn.end; ++bi) {
                        const Triangle& bt = b.triangles[b.order[bi]];
                        if (aabb_distance_squared(at.box, bt.box) <= gap_squared &&
                            triangle_distance_squared(at, bt) <= gap_squared)
                            return true;
                    }
                }
            } else if (bn.is_leaf() || (!an.is_leaf() && an.end - an.begin >= bn.end - bn.begin)) {
                stack.push_back({an.left, b_index});
                stack.push_back({an.right, b_index});
            } else {
                stack.push_back({a_index, bn.left});
                stack.push_back({a_index, bn.right});
            }
        }
        return false;
    }

    /// Detect part pairs whose actual surface triangles are within the requested gap.
    /// Surface ownership comes from SurfaceParentElement; BVHs keep the face-distance search scalable.
    static std::vector<std::pair<std::string, std::string>>
    detect_general_contact_pairs(entt::registry& reg, SimdroidInspector& insp,
                                 entt::entity surface_set_entity, double distance_threshold) {
        std::vector<std::pair<std::string, std::string>> result;
        if (!std::isfinite(distance_threshold) || distance_threshold <= 0.0) return result;

        auto part_surfaces = collect_part_surfaces(reg, insp, surface_set_entity);
        if (part_surfaces.size() < 2) return result;
        for (auto& [name, surface] : part_surfaces) build_bvh(surface);

        std::vector<std::string> part_names;
        part_names.reserve(part_surfaces.size());
        for (const auto& [name, surface] : part_surfaces) {
            if (!surface.triangles.empty()) part_names.push_back(name);
        }
        std::sort(part_names.begin(), part_names.end());

        const double gap_squared = distance_threshold * distance_threshold;
        for (size_t i = 0; i < part_names.size(); ++i) {
            const auto& a = part_surfaces.at(part_names[i]);
            for (size_t j = i + 1; j < part_names.size(); ++j) {
                const auto& b = part_surfaces.at(part_names[j]);
                if (aabb_distance_squared(a.box, b.box) > gap_squared) continue;
                if (bvh_surfaces_within_gap(a, b, gap_squared))
                    result.push_back({part_names[i], part_names[j]});
            }
        }
        return result;
    }
    static double contact_defined_gap(entt::registry& reg, entt::entity contact_entity) {
        if (reg.all_of<Component::ContactFormulation>(contact_entity)) {
            const double value = reg.get<Component::ContactFormulation>(contact_entity).search_distance;
            if (std::isfinite(value) && value > 0.0) return value;
        }
        if (reg.all_of<Component::ContactGapControl>(contact_entity)) {
            const auto& control = reg.get<Component::ContactGapControl>(contact_entity);
            const double value = std::max({control.gap_max, control.slave_gap_max,
                                           control.master_gap_max});
            if (std::isfinite(value) && value > 0.0) return value;
        }
        return -1.0;
    }

    /// Estimate a small geometric tolerance from the median surface edge length.
    /// All surface faces participate, so numbering/order and repeated corner nodes do not bias the result.
    static double auto_calculate_contact_gap(entt::registry& reg, SimdroidInspector&,
                                             entt::entity surface_set_entity) {
        if (!reg.valid(surface_set_entity) ||
            !reg.all_of<Component::SurfaceSetMembers>(surface_set_entity))
            return 1e-6;

        std::vector<double> edge_lengths;
        AABB overall_box;
        const auto& members = reg.get<Component::SurfaceSetMembers>(surface_set_entity).members;
        edge_lengths.reserve(members.size() * 4);
        for (auto surface_entity : members) {
            if (!reg.valid(surface_entity) ||
                !reg.all_of<Component::SurfaceConnectivity>(surface_entity))
                continue;
            std::vector<Vec3> points;
            for (auto node : reg.get<Component::SurfaceConnectivity>(surface_entity).nodes) {
                if (!reg.valid(node) || !reg.all_of<Component::Position>(node)) continue;
                const auto& position = reg.get<Component::Position>(node);
                points.push_back({position.x, position.y, position.z});
                overall_box.expand(points.back());
            }
            if (points.size() < 2) continue;
            for (size_t i = 0; i < points.size(); ++i) {
                const double length = std::sqrt(norm_squared(
                    subtract(points[i], points[(i + 1) % points.size()])));
                if (std::isfinite(length) && length > 0.0) edge_lengths.push_back(length);
            }
        }

        if (edge_lengths.empty()) return 1e-6;
        const size_t middle = edge_lengths.size() / 2;
        std::nth_element(edge_lengths.begin(), edge_lengths.begin() + middle, edge_lengths.end());
        const double median_edge = edge_lengths[middle];
        const double diagonal = std::sqrt(norm_squared(subtract(overall_box.max, overall_box.min)));
        const double numerical_floor = std::max(diagonal * 1e-9, 1e-9);
        const double gap = std::max(median_edge * 0.05, numerical_floor);
        spdlog::info("GeneralContact surface median edge = {:.6f}; tolerance = 5%", median_edge);
        return gap;
    }
};
