// AbaqusParser.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "parser_abaqus/AbaqusParser.h"
#include "components/mesh_components.h"
#include "components/material_components.h"
#include "components/property_components.h"
#include "components/load_components.h"
#include "components/analysis_component.h"
#include "components/simdroid_components.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <unordered_map>

// ============================================================================
// Anonymous namespace - internal helpers
// ============================================================================
namespace {

// ---------------------------------------------------------------------------
// String utilities
// ---------------------------------------------------------------------------

inline void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

inline std::string trim_copy(std::string s) {
    trim(s);
    return s;
}

inline std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return std::toupper(c);
    });
    return s;
}

/// Split a line by comma, no trimming (caller trims as needed).
inline std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> result;
    std::stringstream ss(line);
    std::string item;
    while (std::getline(ss, item, ',')) {
        result.push_back(item);
    }
    return result;
}

/// Convert Abaqus DOF integer (1/2/3) to NovaFEA dof string ("x"/"y"/"z").
inline std::string dof_int_to_string(int dof) {
    switch (dof) {
        case 1:  return "x";
        case 2:  return "y";
        case 3:  return "z";
        case 4:  return "rx";
        case 5:  return "ry";
        case 6:  return "rz";
        default: return std::to_string(dof);
    }
}

// ---------------------------------------------------------------------------
// Abaqus element type string -> NovaFEA numeric type ID
// ---------------------------------------------------------------------------

int abaqus_type_to_id(const std::string& type) {
    std::string t = to_upper(trim_copy(type));
    // Solid
    if (t == "C3D8R" || t == "C3D8" || t == "C3D8I" || t == "C3D8H")  return 308;
    if (t == "C3D4")                                                   return 304;
    if (t == "C3D10" || t == "C3D10M" || t == "C3D10H" || t == "C3D10I") return 310;
    if (t == "C3D20" || t == "C3D20R" || t == "C3D20H")              return 320;
    if (t == "C3D6" || t == "C3D6R")                                   return 306;
    // Shell
    if (t == "S4R" || t == "S4" || t == "S4R5" || t == "QUAD4")       return 204;
    if (t == "S8R" || t == "S8R5" || t == "S8")                       return 208;
    if (t == "S3" || t == "S3R" || t == "STRI3")                      return 203;
    // Beam / Truss
    if (t == "T3D2" || t == "T3D2H")                                   return 102;
    if (t == "B31" || t == "B31H" || t == "T3D3")                      return 103;
    if (t == "B32" || t == "B32H")                                     return 103;
    spdlog::warn("Unknown Abaqus element type '{}', defaulting to 308 (Hexa8)", type);
    return 308;
}

// ---------------------------------------------------------------------------
// AbaqusBlock: a keyword line + its data lines
// ---------------------------------------------------------------------------

struct AbaqusBlock {
    std::string keyword;                                   // upper-case, e.g. "ELEMENT"
    std::map<std::string, std::string> params;             // upper-case key -> value
    std::vector<std::string> data_lines;                   // raw data lines (untrimmed)
};

/// Parse a keyword line like "*ELEMENT,TYPE=C3D8R" into an AbaqusBlock.
AbaqusBlock parse_keyword_line(const std::string& line) {
    AbaqusBlock block;
    std::string content = line.substr(1); // strip leading '*'
    auto parts = split_csv(content);

    if (!parts.empty()) {
        block.keyword = to_upper(trim_copy(parts[0]));
    }
    for (size_t i = 1; i < parts.size(); ++i) {
        std::string p = trim_copy(parts[i]);
        if (p.empty()) continue;
        auto eq = p.find('=');
        if (eq != std::string::npos) {
            std::string key = to_upper(trim_copy(p.substr(0, eq)));
            std::string val = trim_copy(p.substr(eq + 1));
            block.params[key] = val;
        } else {
            block.params[to_upper(p)] = ""; // flag-style parameter, e.g. GENERATE
        }
    }
    return block;
}

/// Read the entire file into a list of AbaqusBlock.
std::vector<AbaqusBlock> read_blocks(std::ifstream& file) {
    std::vector<AbaqusBlock> blocks;
    std::string line;

    while (std::getline(file, line)) {
        // Strip carriage return (Windows line endings)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // Abaqus comment: lines starting with '**'
        if (line.size() >= 2 && line[0] == '*' && line[1] == '*') continue;

        std::string trimmed = trim_copy(line);
        if (trimmed.empty()) continue;

        if (trimmed[0] == '*') {
            blocks.push_back(parse_keyword_line(trimmed));
        } else {
            // Data line belongs to the most recent keyword block
            if (!blocks.empty()) {
                blocks.back().data_lines.push_back(trimmed);
            }
        }
    }
    return blocks;
}

/// Keywords that are sub-options of *MATERIAL (consumed as part of material block).
bool is_material_option(const std::string& kw) {
    static const std::set<std::string> opts = {
        "DENSITY", "ELASTIC", "PLASTIC", "HYPERELASTIC", "VISCOELASTIC",
        "DAMPING", "EXPANSION", "CONDUCTIVITY", "SPECIFIC HEAT",
        "RATE DEPENDENT", "CREEP", "USER MATERIAL",
        "DAMAGE INITIATION", "DAMAGE EVOLUTION", "FAIL STRESS",
        "SHEAR FAILURE", "TENSILE FAILURE"
    };
    return opts.count(kw) > 0;
}

// ---------------------------------------------------------------------------
// Parse-context bundle
// ---------------------------------------------------------------------------

struct ParseCtx {
    entt::registry& registry;
    std::unordered_map<int, entt::entity> node_id_map;
    std::unordered_map<int, entt::entity> element_id_map;
    std::unordered_map<std::string, entt::entity> nodeset_name_map;
    std::unordered_map<std::string, entt::entity> elset_name_map;
    std::unordered_map<std::string, entt::entity> material_name_map;
    std::unordered_map<std::string, entt::entity> curve_name_map;

    int material_id_counter  = 1;
    int property_id_counter  = 1;
    int curve_id_counter     = 1;
    int analysis_id_counter  = 1;
    int boundary_id_counter  = 1;
    int load_id_counter      = 1;
};

// ---------------------------------------------------------------------------
// Block processors
// ---------------------------------------------------------------------------

void process_nodes(const AbaqusBlock& block, ParseCtx& ctx) {
    for (const auto& line : block.data_lines) {
        auto f = split_csv(line);
        if (f.size() < 4) continue;
        int id = std::stoi(trim_copy(f[0]));
        double x = std::stod(trim_copy(f[1]));
        double y = std::stod(trim_copy(f[2]));
        double z = std::stod(trim_copy(f[3]));

        auto e = ctx.registry.create();
        ctx.registry.emplace<Component::NodeID>(e, id);
        ctx.registry.emplace<Component::OriginalID>(e, id);
        ctx.registry.emplace<Component::Position>(e, x, y, z);
        ctx.node_id_map[id] = e;
    }
}

void process_elements(const AbaqusBlock& block, ParseCtx& ctx) {
    int type_id = 308;
    auto it = block.params.find("TYPE");
    if (it != block.params.end()) {
        type_id = abaqus_type_to_id(it->second);
    }

    for (const auto& line : block.data_lines) {
        auto f = split_csv(line);
        if (f.size() < 2) continue;

        int eid = std::stoi(trim_copy(f[0]));
        if (ctx.element_id_map.count(eid)) {
            spdlog::warn("Duplicate element ID {}, skipping", eid);
            continue;
        }

        std::vector<entt::entity> nodes;
        for (size_t i = 1; i < f.size(); ++i) {
            std::string s = trim_copy(f[i]);
            if (s.empty()) continue;
            int nid = std::stoi(s);
            auto nit = ctx.node_id_map.find(nid);
            if (nit != ctx.node_id_map.end()) {
                nodes.push_back(nit->second);
            } else {
                spdlog::warn("Element {} references undefined node {}", eid, nid);
            }
        }

        auto e = ctx.registry.create();
        ctx.registry.emplace<Component::ElementID>(e, eid);
        ctx.registry.emplace<Component::OriginalID>(e, eid);
        ctx.registry.emplace<Component::ElementType>(e, type_id);
        Component::Connectivity conn;
        conn.nodes = std::move(nodes);
        ctx.registry.emplace<Component::Connectivity>(e, std::move(conn));
        ctx.element_id_map[eid] = e;
    }
}

void process_nset(const AbaqusBlock& block, ParseCtx& ctx) {
    auto nit = block.params.find("NSET");
    if (nit == block.params.end()) {
        spdlog::warn("*NSET without NSET name, skipping");
        return;
    }
    std::string name = nit->second;
    bool generate = block.params.count("GENERATE") > 0;

    entt::entity set_e;
    auto sit = ctx.nodeset_name_map.find(name);
    if (sit != ctx.nodeset_name_map.end()) {
        set_e = sit->second;
    } else {
        set_e = ctx.registry.create();
        ctx.registry.emplace<Component::SetName>(set_e, name);
        ctx.registry.emplace<Component::NodeSetMembers>(set_e);
        ctx.nodeset_name_map[name] = set_e;
    }
    auto& members = ctx.registry.get<Component::NodeSetMembers>(set_e);

    if (generate) {
        for (const auto& line : block.data_lines) {
            auto f = split_csv(line);
            if (f.size() < 2) continue;
            int start = std::stoi(trim_copy(f[0]));
            int end   = std::stoi(trim_copy(f[1]));
            int step  = (f.size() > 2 && !trim_copy(f[2]).empty())
                        ? std::stoi(trim_copy(f[2])) : 1;
            for (int id = start; id <= end; id += step) {
                auto it = ctx.node_id_map.find(id);
                if (it != ctx.node_id_map.end()) {
                    members.members.push_back(it->second);
                }
            }
        }
    } else {
        for (const auto& line : block.data_lines) {
            auto f = split_csv(line);
            for (auto& field : f) {
                std::string s = trim_copy(field);
                if (s.empty()) continue;
                // Try integer (node ID)
                bool is_int = true;
                for (char c : s) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        is_int = false;
                        break;
                    }
                }
                if (is_int) {
                    int id = std::stoi(s);
                    auto it = ctx.node_id_map.find(id);
                    if (it != ctx.node_id_map.end()) {
                        members.members.push_back(it->second);
                    } else {
                        spdlog::warn("NSET '{}' references undefined node {}", name, id);
                    }
                } else {
                    // Reference to another node set by name
                    auto ref = ctx.nodeset_name_map.find(s);
                    if (ref != ctx.nodeset_name_map.end()) {
                        auto& ref_members = ctx.registry.get<Component::NodeSetMembers>(ref->second);
                        members.members.insert(members.members.end(),
                                               ref_members.members.begin(),
                                               ref_members.members.end());
                    } else {
                        spdlog::warn("NSET '{}' references unknown set '{}'", name, s);
                    }
                }
            }
        }
    }
}

void process_elset(const AbaqusBlock& block, ParseCtx& ctx) {
    auto nit = block.params.find("ELSET");
    if (nit == block.params.end()) {
        spdlog::warn("*ELSET without ELSET name, skipping");
        return;
    }
    std::string name = nit->second;
    bool generate = block.params.count("GENERATE") > 0;

    entt::entity set_e;
    auto sit = ctx.elset_name_map.find(name);
    if (sit != ctx.elset_name_map.end()) {
        set_e = sit->second;
    } else {
        set_e = ctx.registry.create();
        ctx.registry.emplace<Component::SetName>(set_e, name);
        ctx.registry.emplace<Component::ElementSetMembers>(set_e);
        ctx.elset_name_map[name] = set_e;
    }
    auto& members = ctx.registry.get<Component::ElementSetMembers>(set_e);

    if (generate) {
        for (const auto& line : block.data_lines) {
            auto f = split_csv(line);
            if (f.size() < 2) continue;
            int start = std::stoi(trim_copy(f[0]));
            int end   = std::stoi(trim_copy(f[1]));
            int step  = (f.size() > 2 && !trim_copy(f[2]).empty())
                        ? std::stoi(trim_copy(f[2])) : 1;
            for (int id = start; id <= end; id += step) {
                auto it = ctx.element_id_map.find(id);
                if (it != ctx.element_id_map.end()) {
                    members.members.push_back(it->second);
                }
            }
        }
    } else {
        for (const auto& line : block.data_lines) {
            auto f = split_csv(line);
            for (auto& field : f) {
                std::string s = trim_copy(field);
                if (s.empty()) continue;
                bool is_int = true;
                for (char c : s) {
                    if (!std::isdigit(static_cast<unsigned char>(c))) {
                        is_int = false;
                        break;
                    }
                }
                if (is_int) {
                    int id = std::stoi(s);
                    auto it = ctx.element_id_map.find(id);
                    if (it != ctx.element_id_map.end()) {
                        members.members.push_back(it->second);
                    } else {
                        spdlog::warn("ELSET '{}' references undefined element {}", name, id);
                    }
                } else {
                    auto ref = ctx.elset_name_map.find(s);
                    if (ref != ctx.elset_name_map.end()) {
                        auto& ref_members = ctx.registry.get<Component::ElementSetMembers>(ref->second);
                        members.members.insert(members.members.end(),
                                               ref_members.members.begin(),
                                               ref_members.members.end());
                    } else {
                        spdlog::warn("ELSET '{}' references unknown set '{}'", name, s);
                    }
                }
            }
        }
    }
}

void process_material(const AbaqusBlock& block,
                      const std::vector<AbaqusBlock>& sub_blocks,
                      ParseCtx& ctx) {
    auto nit = block.params.find("NAME");
    if (nit == block.params.end()) {
        spdlog::warn("*MATERIAL without NAME, skipping");
        return;
    }
    std::string name = nit->second;

    auto e = ctx.registry.create();
    ctx.registry.emplace<Component::MaterialID>(e, ctx.material_id_counter++);
    ctx.registry.emplace<Component::MaterialModel>(e, "IsotropicElastic");
    ctx.material_name_map[name] = e;

    for (const auto& sub : sub_blocks) {
        if (sub.keyword == "DENSITY") {
            if (!sub.data_lines.empty()) {
                auto f = split_csv(sub.data_lines[0]);
                if (!f.empty() && !trim_copy(f[0]).empty()) {
                    double rho = std::stod(trim_copy(f[0]));
                    auto& p = ctx.registry.get_or_emplace<Component::LinearElasticParams>(e);
                    p.rho = rho;
                }
            }
        } else if (sub.keyword == "ELASTIC") {
            if (!sub.data_lines.empty()) {
                auto f = split_csv(sub.data_lines[0]);
                if (f.size() >= 2) {
                    double E  = std::stod(trim_copy(f[0]));
                    double nu = std::stod(trim_copy(f[1]));
                    auto& p = ctx.registry.get_or_emplace<Component::LinearElasticParams>(e);
                    p.E  = E;
                    p.nu = nu;
                }
            }
        }
    }
}

void process_solid_section(const AbaqusBlock& block, ParseCtx& ctx) {
    auto eit = block.params.find("ELSET");
    if (eit == block.params.end()) {
        spdlog::warn("*SOLID SECTION without ELSET, skipping");
        return;
    }
    std::string elset_name = eit->second;

    auto esit = ctx.elset_name_map.find(elset_name);
    if (esit == ctx.elset_name_map.end()) {
        spdlog::warn("SOLID SECTION references unknown ELSET '{}'", elset_name);
        return;
    }
    entt::entity elset_e = esit->second;

    // Create property entity
    auto prop_e = ctx.registry.create();
    ctx.registry.emplace<Component::PropertyID>(prop_e, ctx.property_id_counter++);
    ctx.registry.emplace<Component::SolidProperty>(prop_e, 308, 2, "enhanced");

    // Resolve material (may not exist yet at single-pass; handled by multi-pass)
    entt::entity mat_e = entt::null;
    auto mit = block.params.find("MATERIAL");
    if (mit != block.params.end()) {
        auto mref = ctx.material_name_map.find(mit->second);
        if (mref != ctx.material_name_map.end()) {
            mat_e = mref->second;
        } else {
            spdlog::warn("SOLID SECTION references unknown material '{}'", mit->second);
        }
    }

    // Create Part binding element-set + material + section
    auto part_e = ctx.registry.create();
    Component::SimdroidPart part;
    part.name        = elset_name;
    part.element_set = elset_e;
    part.material    = mat_e;
    part.section     = prop_e;
    ctx.registry.emplace<Component::SimdroidPart>(part_e, std::move(part));

    // Attach PropertyRef to every element in the elset
    if (ctx.registry.all_of<Component::ElementSetMembers>(elset_e)) {
        auto& em = ctx.registry.get<Component::ElementSetMembers>(elset_e);
        for (auto elem : em.members) {
            ctx.registry.emplace_or_replace<Component::PropertyRef>(elem, prop_e);
        }
    }
}

void process_amplitude(const AbaqusBlock& block, ParseCtx& ctx) {
    auto nit = block.params.find("NAME");
    if (nit == block.params.end()) {
        spdlog::warn("*AMPLITUDE without NAME, skipping");
        return;
    }
    std::string name = nit->second;

    auto e = ctx.registry.create();
    ctx.registry.emplace<Component::CurveID>(e, ctx.curve_id_counter++);
    auto& curve = ctx.registry.emplace<Component::Curve>(e);
    curve.type = "linear";

    for (const auto& line : block.data_lines) {
        auto f = split_csv(line);
        for (size_t i = 0; i + 1 < f.size(); i += 2) {
            std::string sx = trim_copy(f[i]);
            std::string sy = trim_copy(f[i + 1]);
            if (sx.empty() || sy.empty()) continue;
            curve.x.push_back(std::stod(sx));
            curve.y.push_back(std::stod(sy));
        }
    }
    ctx.curve_name_map[name] = e;
}

void process_step(const AbaqusBlock& block,
                  const std::vector<AbaqusBlock>& sub_blocks,
                  ParseCtx& ctx, DataContext& data_context) {
    auto nit = block.params.find("NAME");
    std::string name = (nit != block.params.end()) ? nit->second : "Step";

    auto analysis_e = ctx.registry.create();
    ctx.registry.emplace<Component::AnalysisID>(analysis_e, ctx.analysis_id_counter++);
    data_context.analysis_entity = analysis_e;

    for (const auto& sub : sub_blocks) {
        if (sub.keyword == "DYNAMIC") {
            // Check for EXPLICIT flag
            bool is_explicit = false;
            for (const auto& [k, v] : sub.params) {
                if (k == "EXPLICIT") is_explicit = true;
            }
            // DYNAMIC, EXPLICIT — "EXPLICIT" appears as a positional (value-only) param
            // parse_keyword_line stores value-only params with empty value, key=upper(token)
            // So "EXPLICIT" would be in params with key "EXPLICIT", value ""
            // Already covered above.

            ctx.registry.emplace_or_replace<Component::AnalysisType>(
                analysis_e, is_explicit ? "DynamicExplicit" : "DynamicImplicit");

            if (!sub.data_lines.empty()) {
                auto f = split_csv(sub.data_lines[0]);
                if (f.size() >= 1 && !trim_copy(f[0]).empty()) {
                    double dt = std::stod(trim_copy(f[0]));
                    ctx.registry.emplace_or_replace<Component::FixedTimeStep>(analysis_e, dt);
                }
                if (f.size() >= 2 && !trim_copy(f[1]).empty()) {
                    double end_time = std::stod(trim_copy(f[1]));
                    ctx.registry.emplace_or_replace<Component::EndTime>(analysis_e, end_time);
                }
            }
        } else if (sub.keyword == "BOUNDARY") {
            for (const auto& line : sub.data_lines) {
                auto f = split_csv(line);
                if (f.size() < 2) continue;
                std::string set_name = trim_copy(f[0]);
                std::string dof_s    = trim_copy(f[1]);
                if (dof_s.empty()) continue;
                int dof = std::stoi(dof_s);

                // Format: set, dof_start, dof_end(optional), value(optional)
                double value = 0.0;
                if (f.size() >= 4 && !trim_copy(f[3]).empty()) {
                    value = std::stod(trim_copy(f[3]));
                }

                auto b_e = ctx.registry.create();
                ctx.registry.emplace<Component::BoundaryID>(b_e, ctx.boundary_id_counter++);
                ctx.registry.emplace<Component::BoundarySPC>(b_e, 1,
                    dof_int_to_string(dof), value);

                auto sit = ctx.nodeset_name_map.find(set_name);
                if (sit != ctx.nodeset_name_map.end()) {
                    auto& members = ctx.registry.get<Component::NodeSetMembers>(sit->second);
                    for (auto node : members.members) {
                        auto& ref = ctx.registry.get_or_emplace<Component::AppliedBoundaryRef>(node);
                        ref.boundary_entities.push_back(b_e);
                    }
                } else {
                    spdlog::warn("BOUNDARY references unknown node set '{}'", set_name);
                }
            }
        } else if (sub.keyword == "CLOAD") {
            entt::entity curve_e = entt::null;
            auto ait = sub.params.find("AMPLITUDE");
            if (ait != sub.params.end()) {
                auto cit = ctx.curve_name_map.find(ait->second);
                if (cit != ctx.curve_name_map.end()) {
                    curve_e = cit->second;
                } else {
                    spdlog::warn("CLOAD references unknown amplitude '{}'", ait->second);
                }
            }

            for (const auto& line : sub.data_lines) {
                auto f = split_csv(line);
                if (f.size() < 3) continue;
                std::string set_name = trim_copy(f[0]);
                int dof   = std::stoi(trim_copy(f[1]));
                double value = std::stod(trim_copy(f[2]));

                auto l_e = ctx.registry.create();
                ctx.registry.emplace<Component::LoadID>(l_e, ctx.load_id_counter++);
                ctx.registry.emplace<Component::NodalLoad>(l_e, 1,
                    dof_int_to_string(dof), value, curve_e);

                auto sit = ctx.nodeset_name_map.find(set_name);
                if (sit != ctx.nodeset_name_map.end()) {
                    auto& members = ctx.registry.get<Component::NodeSetMembers>(sit->second);
                    for (auto node : members.members) {
                        auto& ref = ctx.registry.get_or_emplace<Component::AppliedLoadRef>(node);
                        ref.load_entities.push_back(l_e);
                    }
                } else {
                    spdlog::warn("CLOAD references unknown node set '{}'", set_name);
                }
            }
        }
        // Other step sub-keywords (OUTPUT, RESTART, BULK VISCOSITY) are silently skipped
    }
}

} // anonymous namespace

// ============================================================================
// AbaqusParser::parse - public entry point
// ============================================================================

bool AbaqusParser::parse(const std::string& filepath, DataContext& data_context) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("AbaqusParser could not open file: {}", filepath);
        return false;
    }

    spdlog::info("AbaqusParser started for file: {}", filepath);
    data_context.clear();

    // Phase 1: pre-scan into keyword blocks
    auto blocks = read_blocks(file);
    file.close();
    spdlog::debug("AbaqusParser: read {} keyword blocks", blocks.size());

    ParseCtx ctx{data_context.registry};

    // Phase 2a: nodes, elements, sets, amplitudes (no cross-entity dependencies)
    for (const auto& block : blocks) {
        if (block.keyword == "NODE") {
            process_nodes(block, ctx);
        } else if (block.keyword == "ELEMENT") {
            process_elements(block, ctx);
        } else if (block.keyword == "NSET") {
            process_nset(block, ctx);
        } else if (block.keyword == "ELSET") {
            process_elset(block, ctx);
        } else if (block.keyword == "AMPLITUDE") {
            process_amplitude(block, ctx);
        }
    }

    // Phase 2b: materials (compound blocks with sub-options)
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].keyword == "MATERIAL") {
            std::vector<AbaqusBlock> sub_blocks;
            size_t j = i + 1;
            while (j < blocks.size() && is_material_option(blocks[j].keyword)) {
                sub_blocks.push_back(blocks[j]);
                ++j;
            }
            process_material(blocks[i], sub_blocks, ctx);
            i = j - 1; // loop will increment to j
        }
    }

    // Phase 2c: solid sections (depend on elsets and materials)
    for (const auto& block : blocks) {
        if (block.keyword == "SOLID SECTION") {
            process_solid_section(block, ctx);
        }
    }

    // Phase 2d: steps (depend on nodesets, curves)
    for (size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i].keyword == "STEP") {
            std::vector<AbaqusBlock> sub_blocks;
            size_t j = i + 1;
            while (j < blocks.size() && blocks[j].keyword != "END STEP") {
                sub_blocks.push_back(blocks[j]);
                ++j;
            }
            // j points to END STEP (or end of blocks)
            process_step(blocks[i], sub_blocks, ctx, data_context);
            i = j; // skip past END STEP
        }
    }

    // Summary
    auto node_count    = data_context.registry.view<Component::Position>().size();
    auto element_count = data_context.registry.view<Component::Connectivity>().size();
    auto set_count     = data_context.registry.view<Component::SetName>().size();
    spdlog::info("AbaqusParser finished. Nodes: {}, Elements: {}, Sets: {}",
                 node_count, element_count, set_count);
    return true;
}
