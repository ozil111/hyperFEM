/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once

#include "PartGraph.h"
#include "simdroid/SimdroidInspector.h"
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <vector>

struct RemeshOptions {
    double target_compression_ratio = 100.0;
    int min_elements_per_part = 1;
    bool preserve_interface_elements = true;
};

struct PartRemeshPlan {
    std::string part_name;
    int original_element_count = 0;
    int target_element_count = 0;
    std::map<int, int> element_type_counts;
    std::string property_type;
    std::string material_type;
    bool has_load = false;
    bool has_constraint = false;
};

struct InterfaceSignature {
    std::string source_part;
    std::string target_part;
    ConnectionType type = ConnectionType::SharedNode;
    std::string sub_type;
    int count = 0;

    bool operator<(const InterfaceSignature& rhs) const;
    bool same_identity(const InterfaceSignature& rhs) const;
};

struct RemeshPlan {
    RemeshOptions options;
    int original_element_count = 0;
    int target_element_count = 0;
    std::vector<PartRemeshPlan> parts;
    std::vector<InterfaceSignature> interfaces;
    std::vector<std::string> warnings;

    nlohmann::json to_json() const;
};

struct RemeshValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    nlohmann::json to_json() const;
};

class ConnectionPreservingRemesher {
public:
    static RemeshPlan build_plan(entt::registry& registry,
                                 SimdroidInspector& inspector,
                                 const RemeshOptions& options = {});

    static RemeshValidationResult validate_preservation(const RemeshPlan& before,
                                                        const RemeshPlan& after);

    static bool write_plan_json(const RemeshPlan& plan, const std::string& output_path);

private:
    static std::vector<InterfaceSignature> collect_interface_signatures(const PartGraph& graph);
};
