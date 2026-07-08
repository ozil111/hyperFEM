#pragma once

// Internal shared header for SimdroidParser implementation files.
// Contains utility functions, enum mapping tables, and helper structures
// that were originally in anonymous namespaces within SimdroidParser.cpp.

#include "components/mesh_components.h"
#include "components/analysis_component.h"
#include "components/simdroid_components.h"
#include "components/material_components.h"
#include "../../data_center/components/load_components.h"
#include "../../data_center/components/property_components.h"
#include "../parser_base/string_utils.h"

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace SimdroidParserDetail {

// ============================================================
// ID Range descriptor
// ============================================================
struct IdRange {
    int start = 0;
    int end = 0;
    int step = 1;
};

// ============================================================
// String utilities
// ============================================================

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

inline bool is_numeric_token(const std::string& tok) {
    if (tok.empty()) return false;
    size_t start = 0;
    if (tok[0] == '-' || tok[0] == '+') start = 1;
    if (start >= tok.size()) return false;
    for (size_t i = start; i < tok.size(); ++i) {
        if (tok[i] == ':') continue;
        if (!std::isdigit(static_cast<unsigned char>(tok[i]))) return false;
    }
    return true;
}

inline std::vector<std::string> extract_set_refs(const std::string& id_string) {
    std::vector<std::string> refs;
    for (const auto& tok : split_ws_and_commas(id_string)) {
        if (!is_numeric_token(tok)) {
            refs.push_back(tok);
        }
    }
    return refs;
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

// ============================================================
// Vector normalization
// ============================================================

inline std::tuple<double, double, double> normalize(double x, double y, double z) {
    const double len = std::sqrt(x * x + y * y + z * z);
    if (len < 1e-9) return {0.0, 0.0, 0.0};
    return {x / len, y / len, z / len};
}

// ============================================================
// String-to-Enum mapping tables for Contact parameters
// ============================================================

const inline std::unordered_map<std::string, Component::ContactInterType> kContactTypeMap = {
    {"nodetosurfacetie",     Component::ContactInterType::Tie},
    {"surfacetosurfacetie",  Component::ContactInterType::Tie},
    {"nodetosurface",        Component::ContactInterType::NodeToSurface},
    {"nodetosurfacecontact", Component::ContactInterType::NodeToSurface},
    {"surfacetosurface",     Component::ContactInterType::NodeToSurface},
    {"generalcontact",       Component::ContactInterType::General},
};

const inline std::unordered_map<std::string, Component::ContactFormulationType> kFormulationMap = {
    {"standard",           Component::ContactFormulationType::Standard},
    {"optimized",          Component::ContactFormulationType::Optimized},
    {"failshbrick",        Component::ContactFormulationType::FailShBrick},
    {"failsh",             Component::ContactFormulationType::FailSh},
    {"failbrick",          Component::ContactFormulationType::FailBrick},
    {"penalty",            Component::ContactFormulationType::Penalty},
    {"standswitchpenal",   Component::ContactFormulationType::StandSwitchPenal},
    {"optimswitchpenal",   Component::ContactFormulationType::OptimSwitchPenal},
};

const inline std::unordered_map<std::string, Component::InterfaceStiffnessType> kInterfaceStiffnessMap = {
    {"default", Component::InterfaceStiffnessType::Default},
    {"main",    Component::InterfaceStiffnessType::Main},
    {"maximum", Component::InterfaceStiffnessType::Maximum},
    {"minimum", Component::InterfaceStiffnessType::Minimum},
};

const inline std::unordered_map<std::string, Component::SearchMethodType> kSearchMethodMap = {
    {"box",     Component::SearchMethodType::Box},
    {"segment", Component::SearchMethodType::Segment},
};

const inline std::unordered_map<std::string, Component::FrictionLawType> kFrictionLawMap = {
    {"coulomb",  Component::FrictionLawType::Coulomb},
    {"viscous",  Component::FrictionLawType::Viscous},
    {"darmstad", Component::FrictionLawType::Darmstad},
    {"renard",   Component::FrictionLawType::Renard},
    {"expdecay", Component::FrictionLawType::ExpDecay},
};

const inline std::unordered_map<std::string, Component::GapFlagType> kGapFlagMap = {
    {"variable",         Component::GapFlagType::Variable},
    {"variablescale",    Component::GapFlagType::VariableScale},
    {"variablescalepen", Component::GapFlagType::VariableScalePen},
    {"gapmin",           Component::GapFlagType::Gapmin},
};

const inline std::unordered_map<std::string, Component::IgnoreType> kIgnoreMap = {
    {"length",        Component::IgnoreType::Length},
    {"mainthick",     Component::IgnoreType::MainThick},
    {"mainthickzero", Component::IgnoreType::MainThickZero},
};

const inline std::unordered_map<std::string, Component::FailModeType> kFailModeMap = {
    {"max",  Component::FailModeType::Max},
    {"quad", Component::FailModeType::Quad},
};

const inline std::unordered_map<std::string, Component::RuptureFlagType> kRuptureFlagMap = {
    {"tractioncompress", Component::RuptureFlagType::TractionCompress},
    {"traction",         Component::RuptureFlagType::Traction},
};

template <typename EnumT>
EnumT lookup_enum(const std::unordered_map<std::string, EnumT>& map,
                  const std::string& raw, EnumT fallback) {
    auto it = map.find(to_lower_copy(raw));
    return it != map.end() ? it->second : fallback;
}

// Stiffness form int mapping (TYPE7/TYPE24): string -> Istf code
const inline std::unordered_map<std::string, int> kStiffnessFormMap = {
    {"stiffnessfactor", 1}, {"average", 2}, {"maximum", 3},
    {"minimum", 4}, {"masteronly", 1000},
};

// ============================================================
// Mesh Set Definitions (pre-scan from DAT file)
// ============================================================
struct MeshSetDefs {
    std::unordered_map<std::string, std::vector<IdRange>> element_sets;
    std::unordered_map<std::string, std::vector<IdRange>> parts_ranges;
    std::unordered_map<std::string, std::vector<IdRange>> node_sets;
    std::unordered_map<std::string, std::vector<IdRange>> surface_sets;
    // Set-to-set references (set name -> list of referenced set names)
    std::unordered_map<std::string, std::vector<std::string>> element_set_refs;
    std::unordered_map<std::string, std::vector<std::string>> part_set_refs;
    std::unordered_map<std::string, std::vector<std::string>> node_set_refs;
    std::unordered_map<std::string, std::vector<std::string>> surface_set_refs;
};

void collect_set_definitions_from_file(const std::string& path, MeshSetDefs& defs);

} // namespace SimdroidParserDetail
