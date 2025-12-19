// DataContext.h
#pragma once

#include "entt/entt.hpp"
#include <memory>

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
     * @brief Clears all entities, components, and context data from the registry
     */
    void clear() {
        registry.clear();
    }
};

