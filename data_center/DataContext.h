// DataContext.h
#pragma once

#include "entt/entt.hpp"
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

    /**
     * @brief Clears all entities, components, and context data from the registry
     */
    void clear() {
        registry.clear();
        analysis_entity = entt::null;
    }
};

