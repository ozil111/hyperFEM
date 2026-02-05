#include "SimdroidParser.h"

#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include "components/simdroid_components.h"
#include "components/material_components.h"
#include "../../data_center/components/load_components.h"
#include "../parser_base/string_utils.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

using json = nlohmann::json;

// Static member definitions
std::vector<entt::entity> SimdroidParser::node_lookup{};
std::vector<entt::entity> SimdroidParser::element_lookup{};
std::vector<entt::entity> SimdroidParser::surface_lookup{};

namespace {

struct IdRange {
    int start = 0;
    int end = 0;
    int step = 1;
};

inline std::string to_lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

inline bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

inline std::optional<std::string> extract_prefix_before_bracket(const std::string& s) {
    const auto lb = s.find('[');
    if (lb == std::string::npos) return std::nullopt;
    auto prefix = s.substr(0, lb);
    trim(prefix); 
    if (prefix.empty()) return std::nullopt;
    return prefix;
}

inline std::vector<std::string> split_ws_and_commas(std::string s) {
    for (char& c : s) {
        if (c == ',' || c == '[' || c == ']') c = ' ';
    }
    std::vector<std::string> out;
    std::istringstream iss(s);
    for (std::string tok; iss >> tok;) out.push_back(tok);
    return out;
}

inline std::vector<IdRange> parse_id_ranges(const std::string& id_string) {
    std::vector<IdRange> ranges;
    for (const auto& tok : split_ws_and_commas(id_string)) {
        const auto colon1 = tok.find(':');
        if (colon1 == std::string::npos) {
            try {
                const int v = std::stoi(tok);
                ranges.push_back(IdRange{v, v, 1});
            } catch (...) {}
            continue;
        }

        const auto colon2 = tok.find(':', colon1 + 1);
        try {
            if (colon2 == std::string::npos) {
                const int a = std::stoi(tok.substr(0, colon1));
                const int b = std::stoi(tok.substr(colon1 + 1));
                ranges.push_back(IdRange{a, b, 1});
            } else {
                const int a = std::stoi(tok.substr(0, colon1));
                const int b = std::stoi(tok.substr(colon1 + 1, colon2 - (colon1 + 1)));
                const int c = std::stoi(tok.substr(colon2 + 1));
                ranges.push_back(IdRange{a, b, c == 0 ? 1 : c});
            }
        } catch (...) {}
    }
    return ranges;
}

inline entt::entity get_or_create_set_entity(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto e : view) {
        if (view.get<const Component::SetName>(e).value == name) return e;
    }
    const auto e = registry.create();
    registry.emplace<Component::SetName>(e, name);
    return e;
}

struct MeshSetDefs {
    std::unordered_map<std::string, std::vector<IdRange>> element_sets;
    std::unordered_map<std::string, std::vector<IdRange>> parts_ranges;
    std::unordered_map<std::string, std::vector<IdRange>> node_sets;
    std::unordered_map<std::string, std::vector<IdRange>> surface_sets;
};

void collect_set_definitions_from_file(const std::string& path, MeshSetDefs& defs) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string current_block; 
    std::string current_name;
    std::string current_ids;

    auto flush_current = [&](const std::string& block_type) {
        if (current_name.empty()) return;
        const auto ranges = parse_id_ranges(current_ids);
        if (ranges.empty()) return;

        if (block_type == "element") {
            auto& v = defs.element_sets[current_name];
            v.insert(v.end(), ranges.begin(), ranges.end());
        } else if (block_type == "part") {
            auto& v = defs.parts_ranges[current_name];
            v.insert(v.end(), ranges.begin(), ranges.end());
        } else if (block_type == "node") {
            auto& v = defs.node_sets[current_name];
            v.insert(v.end(), ranges.begin(), ranges.end());
        } else if (block_type == "surface") {
            auto& v = defs.surface_sets[current_name];
            v.insert(v.end(), ranges.begin(), ranges.end());
        }
        current_ids.clear(); 
    };

    std::string line;
    int brace_level = 0;
    enum State { IDLE, IN_SET_BLOCK, IN_PART_BLOCK };
    State state = IDLE;

    while (std::getline(file, line)) {
        preprocess_line(line);
        if (line.empty()) continue;

        if (state == IDLE) {
            if (line == "Set {") {
                state = IN_SET_BLOCK;
                brace_level = 1;
                current_block.clear();
                current_name.clear();
                current_ids.clear();
            } else if (line == "Part {") {
                state = IN_PART_BLOCK;
                brace_level = 1;
                current_block = "part"; 
                current_name.clear();
                current_ids.clear();
            }
            continue;
        }

        if (ends_with(line, "{")) {
            ++brace_level;
            if (state == IN_SET_BLOCK) {
                auto b = line.substr(0, line.size() - 1);
                trim(b);
                current_block = to_lower_copy(b);
            }
            continue;
        }

        if (line == "}") {
            flush_current(current_block);
            current_name.clear();
            --brace_level;
            if (brace_level <= 0) {
                state = IDLE;
                current_block.clear();
            } else if (state == IN_SET_BLOCK && brace_level == 1) {
                current_block.clear();
            }
            continue;
        }

        if (state == IN_SET_BLOCK) {
            bool is_supported = (current_block == "element" || current_block == "part" || current_block == "node" || current_block == "surface");
            if (!is_supported) continue;
        }

        bool has_open_bracket = line.find('[') != std::string::npos;
        if (has_open_bracket) {
            flush_current(current_block);
            current_name.clear(); 
            auto name_opt = extract_prefix_before_bracket(line);
            if (name_opt) current_name = *name_opt;
            
            size_t lb = line.find('[');
            std::string content = line.substr(lb + 1);
            size_t rb = content.rfind(']');
            if (rb != std::string::npos) {
                current_ids = content.substr(0, rb);
                flush_current(current_block); 
                current_name.clear();
            } else {
                current_ids = content; 
            }
        } else if (!current_name.empty()) {
            std::string segment = line;
            size_t rb = segment.rfind(']');
            if (rb != std::string::npos) {
                current_ids += " " + segment.substr(0, rb);
                flush_current(current_block);
                current_name.clear();
            } else {
                current_ids += " " + segment;
            }
        }
    }
}

} // namespace

// Helper to normalize vector
static std::tuple<double, double, double> normalize(double x, double y, double z) {
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-9) return {0.0, 0.0, 0.0};
    return {x / len, y / len, z / len};
}

bool SimdroidParser::parse(const std::string& mesh_path, const std::string& control_path, DataContext& ctx) {
    // 1. Parse Mesh DAT (sets/members must exist before applying loads/constraints)
    try {
        parse_mesh_dat(mesh_path, ctx);
    } catch (const std::exception& e) {
        spdlog::error("Error parsing mesh.dat: {}", e.what());
        return false;
    }

    // 2. Parse Control JSON
    try {
        parse_control_json(control_path, ctx);
    } catch (const std::exception& e) {
        spdlog::error("Error parsing control.json: {}", e.what());
        return false;
    }

    return true;
}

void SimdroidParser::parse_control_json(const std::string& path, DataContext& ctx) {
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("cannot open control file");
    json j = json::parse(file, nullptr, true, true);
    
    // [Blueprint Strategy] 将原始 JSON 保存到 DataContext 中作为蓝图
    // Export 时将 ECS 修改回写到此蓝图，保留所有未解析的字段
    ctx.simdroid_blueprint = j;
    spdlog::info("Simdroid blueprint saved. Unknown fields will be preserved during export.");
    
    auto& registry = ctx.registry;

    if (j.contains("Material") && j["Material"].is_object()) {
        for (auto& [key, val] : j["Material"].items()) {
            const entt::entity mat_e = registry.create();
            if (val.contains("MaterialConstants")) {
                auto& cons = val["MaterialConstants"];
                Component::LinearElasticParams params;
                if (cons.contains("E")) params.E = cons["E"];
                if (cons.contains("Nu")) params.nu = cons["Nu"];
                if (val.contains("Density")) params.rho = val["Density"];
                registry.emplace<Component::LinearElasticParams>(mat_e, params);
            }
            registry.emplace<Component::SetName>(mat_e, key);
        }
    }

    if (j.contains("PartProperty") && j["PartProperty"].is_object()) {
        for (auto it = j["PartProperty"].begin(); it != j["PartProperty"].end(); ++it) {
            const std::string part_key = it.key();
            const auto& part_info = it.value();

            std::string title = part_info.value("Title", part_key);
            std::string ele_set_name = part_info.value("EleSet", "");
            std::string mat_name = part_info.value("Material", "");

            entt::entity ele_set_entity = entt::null;
            if (!ele_set_name.empty()) {
                ele_set_entity = get_or_create_set_entity(registry, ele_set_name);
                registry.get_or_emplace<Component::ElementSetMembers>(ele_set_entity);
            }

            entt::entity mat_entity = entt::null;
            if (!mat_name.empty()) {
                auto view = registry.view<Component::SetName, Component::LinearElasticParams>();
                for(auto e : view) {
                    if(view.get<Component::SetName>(e).value == mat_name) {
                        mat_entity = e;
                        break;
                    }
                }
            }

            const entt::entity part_entity = registry.create();
            Component::SimdroidPart part;
            part.name = std::move(title);
            part.element_set = ele_set_entity;
            part.material = mat_entity;
            part.section = entt::null;
            registry.emplace<Component::SimdroidPart>(part_entity, std::move(part));
        }
    }

    if (j.contains("Contact") && j["Contact"].is_object()) {
        for (auto it = j["Contact"].begin(); it != j["Contact"].end(); ++it) {
             const std::string contact_name = it.key();
             const auto& contact_info = it.value();
             
             Component::ContactDefinition def;
             def.name = contact_name;
             def.friction = contact_info.value("Friction", 0.0);
             std::string type_str = contact_info.value("Type", "");
             
             std::string master_name, slave_name;
             
             if (type_str == "NodeToSurfaceTie" || type_str == "NodeToSurface") {
                 def.type = Component::ContactType::NodeToSurface;
                 master_name = contact_info.value("MasterFaces", "");
                 slave_name = contact_info.value("SlaveNodes", "");
             } else if (type_str == "SurfaceToSurfaceTie" || type_str == "SurfaceToSurface") {
                 def.type = Component::ContactType::SurfaceToSurface;
                 master_name = contact_info.value("MasterFaces", "");
                 slave_name = contact_info.value("SlaveFaces", "");
             } else {
                 def.type = Component::ContactType::Unknown;
             }

             if (!master_name.empty()) def.master_entity = get_or_create_set_entity(registry, master_name);
             if (!slave_name.empty()) def.slave_entity = get_or_create_set_entity(registry, slave_name);

             const entt::entity e = registry.create();
             registry.emplace<Component::ContactDefinition>(e, std::move(def));
        }
    }

    if (j.contains("Constraint") && j["Constraint"].is_object()) {
        spdlog::info("Parsing Constraints from Simdroid Control...");
        const auto& j_cons = j["Constraint"];

        // 1) Boundary Conditions (SPC)
        if (j_cons.contains("Boundary")) {
            parse_boundary_conditions(j_cons["Boundary"], registry);
        }

        // 2) Rigid Bodies (MPC)
        if (j_cons.contains("NodalRigidBody")) {
            parse_rigid_bodies(j_cons["NodalRigidBody"], registry);
        }

        // 3) Distributing Couplings (RBE3-like) - treated similar to rigid bodies for graph connectivity
        if (j_cons.contains("DistributingCoupling")) {
            parse_rigid_bodies(j_cons["DistributingCoupling"], registry);
        }

        // 4) [新增] Rigid Walls
        if (j_cons.contains("RigidWall")) {
            spdlog::info("Parsing RigidWalls...");
            parse_rigid_walls(j_cons["RigidWall"], registry);
        }
    }

    if (j.contains("Load") && j["Load"].is_object()) {
        spdlog::info("Parsing Loads from Simdroid Control...");
        parse_loads(j["Load"], registry);
    }

    // [新增] Initial Conditions
    if (j.contains("InitialCondition") && j["InitialCondition"].is_object()) {
        spdlog::info("Parsing Initial Conditions...");
        parse_initial_conditions(j["InitialCondition"], registry);
    }

    // [新增] Analysis Settings (Step)
    if (j.contains("Step") && j["Step"].is_object()) {
        spdlog::info("Parsing Analysis Settings...");
        parse_analysis_settings(j["Step"], registry, ctx);
    }
}

entt::entity SimdroidParser::find_set_by_name(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto entity : view) {
        if (view.get<const Component::SetName>(entity).value == name) {
            return entity;
        }
    }
    return entt::null;
}

void SimdroidParser::parse_boundary_conditions(const json& j_bcs, entt::registry& registry) {
    int next_boundary_id = 1;
    for (auto& [key, val] : j_bcs.items()) {
        std::string set_name = val.value("NodeSet", "");
        if (set_name.empty()) set_name = val.value("Set", ""); // fallback for older files
        if (set_name.empty()) continue;

        entt::entity set_entity = find_set_by_name(registry, set_name);
        if (set_entity == entt::null || !registry.all_of<Component::NodeSetMembers>(set_entity)) {
            spdlog::warn("Boundary '{}' refers to unknown Node Set '{}'", key, set_name);
            continue;
        }

        struct DofConfig { std::string axis; double val; };
        std::vector<DofConfig> target_dofs;

        const std::string type = val.value("Type", "Displacement");

        if (type == "Fixed" || type == "Encastre") {
            target_dofs = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}, {"rx", 0.0}, {"ry", 0.0}, {"rz", 0.0}};
        } else if (type == "Pinned") {
            target_dofs = {{"x", 0.0}, {"y", 0.0}, {"z", 0.0}};
        } else {
            // "Displacement" or specific type
            if (val.contains("U1") || val.contains("X")) target_dofs.push_back({"x", val.value("U1", val.value("X", 0.0))});
            if (val.contains("U2") || val.contains("Y")) target_dofs.push_back({"y", val.value("U2", val.value("Y", 0.0))});
            if (val.contains("U3") || val.contains("Z")) target_dofs.push_back({"z", val.value("U3", val.value("Z", 0.0))});
        }

        if (target_dofs.empty()) continue;

        const auto& node_members = registry.get<Component::NodeSetMembers>(set_entity).members;

        for (const auto& cfg : target_dofs) {
            auto bc_entity = registry.create();
            registry.emplace<Component::BoundaryID>(bc_entity, next_boundary_id++);
            registry.emplace<Component::BoundarySPC>(bc_entity, 1, cfg.axis, cfg.val);
            registry.emplace<Component::SetName>(bc_entity, key); // Debug info

            for (auto node_e : node_members) {
                if (!registry.valid(node_e)) continue;
                auto& applied = registry.get_or_emplace<Component::AppliedBoundaryRef>(node_e);
                applied.boundary_entities.push_back(bc_entity);
            }
        }
        spdlog::info("  -> Applied Boundary '{}' to {} nodes.", key, node_members.size());
    }
}

void SimdroidParser::parse_rigid_bodies(const json& j_rbs, entt::registry& registry) {
    for (auto& [key, val] : j_rbs.items()) {
        const std::string m_set_name = val.value("MasterNodeSet", "");
        const std::string s_set_name = val.value("SlaveNodeSet", "");

        entt::entity m_set = find_set_by_name(registry, m_set_name);
        entt::entity s_set = find_set_by_name(registry, s_set_name);

        const bool m_ok = (m_set != entt::null) && registry.all_of<Component::NodeSetMembers>(m_set);
        const bool s_ok = (s_set != entt::null) && registry.all_of<Component::NodeSetMembers>(s_set);

        if (m_ok && s_ok) {
            auto constraint_entity = registry.create();
            Component::RigidBodyConstraint rbc;
            rbc.master_node_set = m_set;
            rbc.slave_node_set = s_set;

            registry.emplace<Component::RigidBodyConstraint>(constraint_entity, rbc);
            registry.emplace<Component::SetName>(constraint_entity, key);

            spdlog::info("  -> Created RigidBody '{}' between '{}' and '{}'", key, m_set_name, s_set_name);
        } else {
            spdlog::warn("RigidBody '{}' missing sets: Master='{}', Slave='{}'", key, m_set_name, s_set_name);
        }
    }
}

void SimdroidParser::parse_loads(const json& j_loads, entt::registry& registry) {
    int next_load_id = 1;
    for (auto& [key, val] : j_loads.items()) {
        std::string type = val.value("Type", "");

        // 1. Handle Nodal Loads (Force, Moment)
        if (type == "Force" || type == "Moment" || type == "ConcentratedForce") {
            std::string set_name = val.value("NodeSet", "");
            if (set_name.empty()) set_name = val.value("Set", ""); // fallback for older files
            if (set_name.empty()) continue;

            entt::entity set_entity = find_set_by_name(registry, set_name);
            if (set_entity == entt::null || !registry.all_of<Component::NodeSetMembers>(set_entity)) {
                spdlog::warn("Load '{}' refers to unknown NodeSet '{}'", key, set_name);
                continue;
            }

            double mag = val.value("Magnitude", 0.0);
            if (val.contains("Mag")) mag = val["Mag"].get<double>();
            const bool has_mag = val.contains("Magnitude") || val.contains("Mag");

            double fx = 0.0, fy = 0.0, fz = 0.0;

            if (val.contains("Direction") && val["Direction"].is_array()) {
                const auto& d = val["Direction"];
                if (d.size() >= 3) {
                    double dx = d[0].get<double>();
                    double dy = d[1].get<double>();
                    double dz = d[2].get<double>();
                    std::tie(dx, dy, dz) = normalize(dx, dy, dz);
                    fx = mag * dx;
                    fy = mag * dy;
                    fz = mag * dz;
                }
            } else {
                const double x = val.value("X", 0.0);
                const double y = val.value("Y", 0.0);
                const double z = val.value("Z", 0.0);

                // If Magnitude is provided, treat X/Y/Z as direction components; otherwise treat them as direct components.
                if (has_mag && std::abs(mag) > 0.0) {
                    double dx = x, dy = y, dz = z;
                    std::tie(dx, dy, dz) = normalize(dx, dy, dz);
                    fx = mag * dx;
                    fy = mag * dy;
                    fz = mag * dz;
                } else {
                    fx = x;
                    fy = y;
                    fz = z;
                }
            }

            struct LoadComp { std::string axis; double val; };
            std::vector<LoadComp> components;

            const bool is_moment = (type == "Moment");
            const char* ax_x = is_moment ? "rx" : "x";
            const char* ax_y = is_moment ? "ry" : "y";
            const char* ax_z = is_moment ? "rz" : "z";

            if (std::abs(fx) > 1e-12) components.push_back({ax_x, fx});
            if (std::abs(fy) > 1e-12) components.push_back({ax_y, fy});
            if (std::abs(fz) > 1e-12) components.push_back({ax_z, fz});

            const auto& node_members = registry.get<Component::NodeSetMembers>(set_entity).members;

            for (const auto& comp : components) {
                auto load_def_entity = registry.create();
                registry.emplace<Component::LoadID>(load_def_entity, next_load_id++);
                registry.emplace<Component::NodalLoad>(load_def_entity, is_moment ? 2 : 1, comp.axis, comp.val);
                registry.emplace<Component::SetName>(load_def_entity, key);

                for (auto node_entity : node_members) {
                    if (!registry.valid(node_entity)) continue;
                    auto& applied = registry.get_or_emplace<Component::AppliedLoadRef>(node_entity);
                    applied.load_entities.push_back(load_def_entity);
                }
            }
            spdlog::info("  -> Applied {} '{}' to {} nodes.", type, key, node_members.size());
        }
        // 2. Handle Pressure (Element Load) - Placeholder
        else if (type == "Pressure") {
            std::string set_name = val.value("EleSet", "");
            spdlog::info("  -> Found Pressure Load '{}' on EleSet '{}'. (Solver conversion pending)", key, set_name);
        }
    }
}

// =========================================================
// 实现：初始条件 (Initial Velocity)
// =========================================================
void SimdroidParser::parse_initial_conditions(const json& j_ics, entt::registry& registry) {
    for (auto& [key, val] : j_ics.items()) {
        if (!val.is_object()) continue;

        std::string type = val.value("Type", "");
        const std::string type_l = to_lower_copy(type);

        // Only handle initial velocity for now
        if (type_l != "velocity" && type_l != "initialvelocity" && type_l != "initial_velocity") continue;

        std::string set_name = val.value("NodeSet", "");
        if (set_name.empty()) set_name = val.value("Set", "");
        if (set_name.empty()) {
            spdlog::warn("InitialCondition '{}' missing NodeSet/Set field.", key);
            continue;
        }

        entt::entity set_entity = find_set_by_name(registry, set_name);
        if (set_entity == entt::null || !registry.all_of<Component::NodeSetMembers>(set_entity)) {
            spdlog::warn("InitialCondition '{}' refers to unknown NodeSet '{}'", key, set_name);
            continue;
        }

        const auto& node_members = registry.get<Component::NodeSetMembers>(set_entity).members;

        double vx = val.value("X", 0.0);
        double vy = val.value("Y", 0.0);
        double vz = val.value("Z", 0.0);

        // If Magnitude + Direction are provided, they override X/Y/Z
        if (val.contains("Magnitude") && val.contains("Direction") && val["Direction"].is_array()) {
            const double mag = val["Magnitude"].get<double>();
            const auto& d = val["Direction"];
            if (d.size() >= 3) {
                double nx = d[0].get<double>();
                double ny = d[1].get<double>();
                double nz = d[2].get<double>();
                std::tie(nx, ny, nz) = normalize(nx, ny, nz);
                vx = mag * nx;
                vy = mag * ny;
                vz = mag * nz;
            }
        }

        int count = 0;
        for (auto node_e : node_members) {
            if (!registry.valid(node_e)) continue;
            registry.emplace_or_replace<Component::Velocity>(node_e, vx, vy, vz);
            count++;
        }
        spdlog::info("  -> Applied Initial Velocity ({}, {}, {}) to {} nodes.", vx, vy, vz, count);
    }
}

// =========================================================
// 实现：刚性墙 (Rigid Wall)
// =========================================================
void SimdroidParser::parse_rigid_walls(const json& j_rw, entt::registry& registry) {
    int next_wall_id = 1;
    for (auto& [key, val] : j_rw.items()) {
        if (!val.is_object()) continue;

        const auto rw_entity = registry.create();

        Component::RigidWall rw{};
        rw.id = next_wall_id++;
        rw.type = val.value("Type", "Planar");
        rw.secondary_node_set = entt::null;

        // Direct parameters (optional)
        if (val.contains("Parameters") && val["Parameters"].is_array()) {
            rw.parameters = val["Parameters"].get<std::vector<double>>();
        } else {
            // Planar: Normal + Point -> ax+by+cz+d=0
            std::vector<double> normal = {0.0, 0.0, 1.0};
            std::vector<double> point = {0.0, 0.0, 0.0};

            if (val.contains("Normal") && val["Normal"].is_array()) normal = val["Normal"].get<std::vector<double>>();
            if (val.contains("Point") && val["Point"].is_array()) point = val["Point"].get<std::vector<double>>();

            if (normal.size() >= 3 && point.size() >= 3) {
                double nx = normal[0], ny = normal[1], nz = normal[2];
                std::tie(nx, ny, nz) = normalize(nx, ny, nz);
                const double d = -(nx * point[0] + ny * point[1] + nz * point[2]);
                rw.parameters = {nx, ny, nz, d};
            }
        }

        // Secondary/slave node set (optional)
        std::string slave_set_name = val.value("SecondaryNodes", "");
        if (slave_set_name.empty()) slave_set_name = val.value("SlaveNodes", "");
        if (!slave_set_name.empty()) {
            const entt::entity slave_set = find_set_by_name(registry, slave_set_name);
            if (slave_set != entt::null) {
                rw.secondary_node_set = slave_set;
            } else {
                spdlog::warn("RigidWall '{}' refers to unknown NodeSet '{}'", key, slave_set_name);
            }
        }

        registry.emplace<Component::RigidWall>(rw_entity, rw);
        registry.emplace<Component::SetName>(rw_entity, key);

        spdlog::info("  -> Created RigidWall '{}' ({})", key, rw.type);
    }
}

// =========================================================
// 实现：分析设置 (Analysis Settings)
// =========================================================
void SimdroidParser::parse_analysis_settings(const json& j_step, entt::registry& registry, DataContext& ctx) {
    if (!j_step.is_object() || j_step.empty()) return;

    const json* step_cfg = &j_step;
    const bool looks_like_step_cfg =
        j_step.contains("Type") || j_step.contains("EndTime") || j_step.contains("Duration") ||
        j_step.contains("TimeStep") || j_step.contains("Output");

    if (!looks_like_step_cfg) {
        auto it = j_step.begin();
        if (it != j_step.end() && it.value().is_object()) {
            step_cfg = &it.value();
        }
    }

    // Analysis entity (singleton)
    entt::entity analysis_entity = ctx.analysis_entity;
    if (analysis_entity == entt::null || !registry.valid(analysis_entity)) {
        analysis_entity = registry.create();
        ctx.analysis_entity = analysis_entity;
    }

    const std::string type = step_cfg->value("Type", "Explicit");
    registry.emplace_or_replace<Component::AnalysisType>(analysis_entity, type);

    double end_time = step_cfg->value("EndTime", 1.0);
    if (step_cfg->contains("Duration") && (*step_cfg)["Duration"].is_number()) {
        end_time = (*step_cfg)["Duration"].get<double>();
    }
    registry.emplace_or_replace<Component::EndTime>(analysis_entity, end_time);

    if (step_cfg->contains("TimeStep") && (*step_cfg)["TimeStep"].is_number()) {
        const double dt = (*step_cfg)["TimeStep"].get<double>();
        registry.emplace_or_replace<Component::FixedTimeStep>(analysis_entity, dt);
    }

    // Output entity (singleton) - used by explicit solver
    entt::entity output_entity = ctx.output_entity;
    if (output_entity == entt::null || !registry.valid(output_entity)) {
        output_entity = registry.create();
        ctx.output_entity = output_entity;
    }

    double interval = (end_time > 0.0 ? end_time / 20.0 : 0.0);
    if (step_cfg->contains("Output") && (*step_cfg)["Output"].is_object()) {
        const auto& out = (*step_cfg)["Output"];
        interval = (end_time > 0.0 ? end_time / 100.0 : 0.0);
        interval = out.value("Interval", interval);
        if (out.contains("Frequency") && out["Frequency"].is_number()) {
            const double freq = out["Frequency"].get<double>();
            if (freq > 0.0) interval = 1.0 / freq;
        }
    }

    registry.emplace_or_replace<Component::OutputControl>(output_entity, interval);
    registry.emplace_or_replace<Component::OutputIntervalTime>(output_entity, interval);

    spdlog::info("  -> Analysis Configured: Type={}, EndTime={}, OutputInterval={}", type, end_time, interval);
}

void SimdroidParser::parse_mesh_dat(const std::string& path, DataContext& ctx) {
    MeshSetDefs defs;
    collect_set_definitions_from_file(path, defs);

    auto& registry = ctx.registry;
    std::ifstream file(path);
    if (!file.is_open()) throw std::runtime_error("cannot open mesh file");

    node_lookup.clear();
    element_lookup.clear();
    surface_lookup.clear();

    std::string line;
    std::string pending_element_line;
    std::string pending_surface_line;
    bool in_node_section = false;
    bool in_element_section = false;
    bool in_element_type_section = false;
    bool in_surface_section = false;
    bool in_surface_type_section = false;
    bool in_skip_block = false;     // Skip "Set { ... }" and "Part { ... }" definitions during geometry parsing
    int  skip_brace_level = 0;
    int current_element_type_id = 0;
    std::string current_element_block_name;
    std::string current_surface_block_name;
    
    // Safety reserve
    node_lookup.resize(10000, entt::null);
    element_lookup.resize(10000, entt::null);
    surface_lookup.resize(10000, entt::null);

    while (std::getline(file, line)) {
        preprocess_line(line);
        if (line.empty()) continue;

        // Skip definition blocks (Set/Part) — members are already collected by collect_set_definitions_from_file().
        if (in_skip_block) {
            if (ends_with(line, "{")) ++skip_brace_level;
            if (line == "}") {
                --skip_brace_level;
                if (skip_brace_level <= 0) {
                    in_skip_block = false;
                    skip_brace_level = 0;
                }
            }
            continue;
        } else {
            // Only start skipping when we're not inside core geometry blocks
            if (!in_node_section && !in_element_section && !in_surface_section) {
                if (line == "Set {") {
                    in_skip_block = true;
                    skip_brace_level = 1;
                    continue;
                }
                if (line == "Part {") {
                    in_skip_block = true;
                    skip_brace_level = 1;
                    continue;
                }
            }
        }

        if (line == "Node {") {
            in_node_section = true;
            continue;
        }
        if (line == "Element {") {
            in_element_section = true;
            current_element_block_name.clear();
            continue;
        }
        if (line == "Surface {") {
            in_surface_section = true;
            current_surface_block_name.clear();
            continue;
        }
        if (ends_with(line, "{")) {
            if (in_element_section) {
                in_element_type_section = true;
                // Extract element block name (e.g., "Hex8" from "Hex8 {")
                const size_t end_pos = line.find('{');
                std::string raw_name = (end_pos == std::string::npos) ? line : line.substr(0, end_pos);
                trim(raw_name);
                current_element_block_name = to_lower_copy(raw_name);
            } else if (in_surface_section) {
                in_surface_type_section = true;
                const size_t end_pos = line.find('{');
                std::string raw_name = (end_pos == std::string::npos) ? line : line.substr(0, end_pos);
                trim(raw_name);
                current_surface_block_name = to_lower_copy(raw_name);
            }
            continue;
        }
        if (line == "}") {
            if (in_element_type_section) {
                in_element_type_section = false;
                current_element_block_name.clear();
            } else if (in_surface_type_section) {
                in_surface_type_section = false;
                current_surface_block_name.clear();
            } else {
                in_node_section = false;
                in_element_section = false;
                in_surface_section = false;
            }
            continue;
        }

        // --- Node Parsing ---
        // --- Node Parsing ---
        if (in_node_section) {
            // [删除] 之前的暴力跳过逻辑，因为现在的坐标定义里包含了 '['
            // if (line.find('[') != std::string::npos) continue;
            
            // 1. 数据清洗：将 [, ], , 替换为空格，统一格式
            std::string clean_line = line;
            std::replace(clean_line.begin(), clean_line.end(), '[', ' ');
            std::replace(clean_line.begin(), clean_line.end(), ']', ' ');
            std::replace(clean_line.begin(), clean_line.end(), ',', ' ');

            std::stringstream ss(clean_line);
            int nid;
            double x, y, z;
            
            // 2. 尝试提取 ID, X, Y, Z
            if (!(ss >> nid >> x >> y >> z)) {
                // 如果提取失败，说明这行可能不是坐标定义（可能是 Node_Set 定义，如 "123_Set [list]"）
                // 我们在 debug 模式下记录一下，但不视为错误，直接跳过
                // spdlog::debug("Skipping non-coordinate line in Node block: {}", line);
                continue;
            }
            
            if (nid < 0) continue;

            // Expand lookup
            if (static_cast<size_t>(nid) >= node_lookup.size()) {
                // 稍微多分配一点，防止频繁扩容
                size_t new_size = std::max(static_cast<size_t>(nid) * 2, node_lookup.size() + 10000); 
                node_lookup.resize(new_size, entt::null);
            }

            auto e = registry.create();
            registry.emplace<Component::Position>(e, x, y, z);
            registry.emplace<Component::NodeID>(e, nid);
            registry.emplace<Component::OriginalID>(e, nid); 
            
            node_lookup[nid] = e;
        }
        
        // --- Element Parsing ---
        if (in_element_section) {
            // Support wrapped lists: element connectivity may span multiple lines inside '[ ... ]'
            std::string elem_line = line;
            if (!pending_element_line.empty()) {
                pending_element_line += " " + elem_line;
                if (pending_element_line.find(']') == std::string::npos) continue;
                elem_line = pending_element_line;
                pending_element_line.clear();
            } else {
                const size_t lb0 = elem_line.find('[');
                if (lb0 != std::string::npos && elem_line.find(']') == std::string::npos) {
                    pending_element_line = elem_line;
                    continue;
                }
            }

            size_t lb = elem_line.find('[');
            size_t rb = elem_line.rfind(']');
            if (lb == std::string::npos || rb == std::string::npos) continue;

            // 1. 解析 Element ID
            int eid = 0;
            try {
                // 处理 ID 后面的潜在逗号 (e.g. "357298, [...")
                std::string id_str = elem_line.substr(0, lb);
                std::replace(id_str.begin(), id_str.end(), ',', ' '); 
                eid = std::stoi(id_str);
            } catch (...) { continue; }

            if (eid < 0) continue;

            // 2. 解析 Node IDs (关键修复)
            std::string content = elem_line.substr(lb + 1, rb - lb - 1);
            
            // [修复] 暴力替换所有逗号为空格，解决 "1,2" 和 "1 2" 的兼容性问题
            std::replace(content.begin(), content.end(), ',', ' ');
            
            std::vector<int> node_ids;
            std::stringstream ss(content);
            int nid;
            while (ss >> nid) {
                node_ids.push_back(nid);
            }

            // 3. 验证节点有效性
            // 即使是退化单元（重复节点），只要 ID 存在于 lookup 表中就是有效的
            std::vector<entt::entity> valid_node_entities;
            valid_node_entities.reserve(node_ids.size());
            bool is_element_broken = false;

            for (int id : node_ids) {
                if (id >= 0 && static_cast<size_t>(id) < node_lookup.size() && node_lookup[id] != entt::null) {
                    valid_node_entities.push_back(node_lookup[id]);
                } else {
                    spdlog::warn("Element {} refers to undefined Node ID: {}", eid, id);
                    is_element_broken = true;
                    break;
                }
            }

            if (is_element_broken) continue;

            // 4. 类型推断 (包含你列出的所有类型)
            int type_id = 0;
            size_t count = node_ids.size();
            
            // 优先信任 count，因为 Simdroid 的块结构可能仅仅是分组
            if (count == 8) type_id = 308;      // Hex8 (包括退化为 Wedge 的情况)
            else if (count == 4) type_id = 304; // Tet4 (Simdroid Tet4) 或 Quad4 (Shell)
            else if (count == 10) type_id = 310;// Tet10
            else if (count == 20) type_id = 320;// Hex20
            else if (count == 3) type_id = 203; // Tri3
            else if (count == 2) type_id = 102; // Line2
            
            // 如果推断出的类型和 block 类型不一致，记录一下（但在 Simdroid 中通常以节点数为准）
            // 例如 Quad4 也是 4节点，Tet4 也是 4节点。
            // 这里可以通过 in_element_type_section 上下文来修正
            // 但为了通用性，先按节点数处理。
            // *特殊处理*: 如果是 2D 单元 Quad4 (4节点)，但被误判为 Tet4 (304)
            // 通常 3D 求解器里 304 是体单元。如果是壳单元，通常 ID 不同。
            // 鉴于目前是 Simdroid 结构求解，4节点大概率是 Tet4。
            // 如果你的 Quad4 是壳单元，可能需要根据 Element ID 范围或额外的 Property 来区分。
            // 暂时映射：4节点 -> 304 (Tet4) 或者是 204 (Quad4)? 
            // 如果 Simdroid 输入中有 Quad4，我们需要看看它的 Block Name。
            
            // 简单修正逻辑：
            if (count == 4) {
                if (current_element_block_name.find("quad") != std::string::npos) type_id = 204; // Quad4
                else type_id = 304; // Default to Tet4
            }

            // Expand lookup
            if (static_cast<size_t>(eid) >= element_lookup.size()) {
                element_lookup.resize(static_cast<size_t>(eid) * 2, entt::null);
            }

            auto e = registry.create();
            registry.emplace<Component::ElementID>(e, eid);
            registry.emplace<Component::OriginalID>(e, eid);
            registry.emplace<Component::ElementType>(e, type_id);
            
            auto& conn = registry.emplace<Component::Connectivity>(e);
            conn.nodes = std::move(valid_node_entities);

            element_lookup[eid] = e;
        }

        // --- Surface Parsing (Simdroid boundary faces/edges) ---
        // Format inside Surface/{Type} blocks:
        //   sid [n1,n2,...,parent_eid]
        // where (n1..nk) are node IDs on the face/edge, and the last entry is parent element ID.
        if (in_surface_section && in_surface_type_section) {
            // Support wrapped lists: surface definition may span multiple lines inside '[ ... ]'
            std::string surf_line = line;
            if (!pending_surface_line.empty()) {
                pending_surface_line += " " + surf_line;
                if (pending_surface_line.find(']') == std::string::npos) continue;
                surf_line = pending_surface_line;
                pending_surface_line.clear();
            } else {
                const size_t lb0 = surf_line.find('[');
                if (lb0 != std::string::npos && surf_line.find(']') == std::string::npos) {
                    pending_surface_line = surf_line;
                    continue;
                }
            }

            const size_t lb = surf_line.find('[');
            const size_t rb = surf_line.rfind(']');
            if (lb == std::string::npos || rb == std::string::npos) continue;

            int sid = 0;
            try {
                std::string id_str = surf_line.substr(0, lb);
                std::replace(id_str.begin(), id_str.end(), ',', ' ');
                sid = std::stoi(id_str);
            } catch (...) { continue; }

            if (sid < 0) continue;

            std::string content = surf_line.substr(lb + 1, rb - lb - 1);
            std::replace(content.begin(), content.end(), ',', ' ');

            std::vector<int> ids;
            ids.reserve(8);
            {
                std::stringstream ss(content);
                int v = 0;
                while (ss >> v) ids.push_back(v);
            }
            if (ids.size() < 2) continue; // must have at least 1 node + parent_eid

            const int parent_eid = ids.back();
            ids.pop_back();

            // Validate parent element
            entt::entity parent_elem_entity = entt::null;
            if (parent_eid >= 0 && static_cast<size_t>(parent_eid) < element_lookup.size()) {
                parent_elem_entity = element_lookup[parent_eid];
            }
            if (parent_elem_entity == entt::null) {
                spdlog::warn("Surface {} refers to undefined parent Element ID: {}", sid, parent_eid);
                continue;
            }

            // Validate nodes
            std::vector<entt::entity> valid_node_entities;
            valid_node_entities.reserve(ids.size());
            bool is_broken = false;
            for (int nid : ids) {
                if (nid >= 0 && static_cast<size_t>(nid) < node_lookup.size() && node_lookup[nid] != entt::null) {
                    valid_node_entities.push_back(node_lookup[nid]);
                } else {
                    spdlog::warn("Surface {} refers to undefined Node ID: {}", sid, nid);
                    is_broken = true;
                    break;
                }
            }
            if (is_broken) continue;

            // Expand lookup
            if (static_cast<size_t>(sid) >= surface_lookup.size()) {
                size_t new_size = std::max(static_cast<size_t>(sid) * 2, surface_lookup.size() + 10000);
                surface_lookup.resize(new_size, entt::null);
            }

            auto se = registry.create();
            registry.emplace<Component::SurfaceID>(se, sid);
            registry.emplace<Component::OriginalID>(se, sid); // compatibility
            auto& sc = registry.emplace<Component::SurfaceConnectivity>(se);
            sc.nodes = std::move(valid_node_entities);
            registry.emplace<Component::SurfaceParentElement>(se, parent_elem_entity);

            surface_lookup[sid] = se;
        }
    }

    // ---------------------------------------------------------------------
    // Apply Sets Definition (Post-Parsing)
    // ---------------------------------------------------------------------
    
    // Helper to add entities to sets
    auto add_to_set = [&](auto& member_list, const std::vector<IdRange>& ranges, const std::vector<entt::entity>& lookup) {
        for (const auto& r : ranges) {
            if (r.step <= 0) continue;
            for (int id = r.start; id <= r.end; id += r.step) {
                if (id >= 0 && static_cast<size_t>(id) < lookup.size()) {
                    entt::entity e = lookup[id];
                    if (e != entt::null) {
                        member_list.push_back(e);
                    }
                }
            }
        }
    };

    // 1. Node Sets
    for (const auto& [name, ranges] : defs.node_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::NodeSetMembers>(e);
        add_to_set(members.members, ranges, node_lookup);
    }

    // 2. Element Sets
    for (const auto& [name, ranges] : defs.element_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::ElementSetMembers>(e);
        add_to_set(members.members, ranges, element_lookup);
    }

    // 3. Part Sets (treat as element sets)
    for (const auto& [name, ranges] : defs.parts_ranges) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::ElementSetMembers>(e);
        add_to_set(members.members, ranges, element_lookup);
    }

    // 4. Surface Sets
    for (const auto& [name, ranges] : defs.surface_sets) {
        entt::entity e = get_or_create_set_entity(registry, name);
        auto& members = registry.get_or_emplace<Component::SurfaceSetMembers>(e);
        add_to_set(members.members, ranges, surface_lookup);
    }
}

