// DataContext.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include "entt/entt.hpp"
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
     * @brief Simdroid Blueprint (原始数据的完整副本)
     * @details 用于在 Export 时保留那些我们未解析/不理解的数据字段，
     * 实现 Round-trip Fidelity (往返保真度)。
     * Import 时保存原始 JSON，Export 时将 ECS 修改回写到此蓝图后导出。
     */
    nlohmann::json simdroid_blueprint;

    /**
     * @brief Clears all entities, components, and context data from the registry
     */
    void clear() {
        registry.clear();
        analysis_entity = entt::null;
        output_entity = entt::null;
        simdroid_blueprint.clear();
    }
};

