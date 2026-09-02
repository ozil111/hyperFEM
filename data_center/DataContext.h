// DataContext.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once

#include <entt/entt.hpp>
#include "nlohmann/json.hpp"
#include <memory>
#include <string>

// Forward declaration
struct TopologyData;

/**
 * @brief The central data hub for the entire application
 * @details This structure holds the single source of truth - the EnTT registry.
 * All mesh entities (nodes, elements, sets) are stored as entities with attached components.
 * Derived data structures (like TopologyData) are stored in the registry's context.
 */
struct DataContext {
    entt::registry registry;
    
    /**
     * @brief Current analysis entity (first analysis in input)
     * @details Set by the parser from the "analysis" field in JSON input.
     * Use registry.get<Component::AnalysisType>(analysis_entity).value etc. to read fields.
     */
    entt::entity analysis_entity = entt::null;

    entt::entity output_entity = entt::null;

    /**
     * @brief Command line specified result output path (from --output/-o)
     * @details When non-empty, solver-level default outputs (e.g. result/xxx.vtk)
     *          should be suppressed to avoid duplicate result files.
     */
    std::string cli_output_path;

    /**
     * @brief Command line specified CSV output prefix (from --output-csv)
     * @details When non-empty, solver writes <prefix>_disp.csv and <prefix>_elements.csv
     *          in Abaqus-compatible format after completion.
     */
    std::string cli_output_csv_prefix;

    /**
     * @brief Simdroid Blueprint (complete copy of original data)
     * @details Used during Export to preserve data fields that we did not parse or understand.
     * Implements Round-trip Fidelity.
     * Save original JSON during Import, and export by writing back ECS modifications to this blueprint.
     */
    nlohmann::json simdroid_blueprint;

    /**
     * @brief Clears all entities, components, and context data from the registry
     */
    void clear() {
        registry.clear();
        analysis_entity = entt::null;
        output_entity = entt::null;
        cli_output_path.clear();
        simdroid_blueprint.clear();
    }
};

