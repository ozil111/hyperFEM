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
     * @brief Analysis type from input file (e.g., "static", "explicit")
     * @details Set by the parser based on the "analysis" field in JSON input
     */
    std::string analysis_type = "static";  // Default to static analysis

    /**
     * @brief Clears all entities, components, and context data from the registry
     */
    void clear() {
        registry.clear();
        analysis_type = "static";
    }
};

