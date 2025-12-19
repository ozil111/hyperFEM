// AppSession.h
#pragma once
#include "DataContext.h"
#include "TopologyData.h"

/**
 * @brief Application session state machine
 * @details Manages the lifecycle of the mesh data and topology analysis.
 * All mesh data is stored in the DataContext's registry using ECS components.
 */
struct AppSession {
    bool is_running = true;
    bool mesh_loaded = false;
    bool topology_built = false;

    DataContext data; // The single source of truth - EnTT registry

    AppSession() = default;

    /**
     * @brief Clears all mesh data and topology from the registry
     */
    void clear_data() {
        // Erase topology data from context if it exists
        if (data.registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
            data.registry.ctx().erase<std::unique_ptr<TopologyData>>();
        }
        
        // Clear all entities and components
        data.clear();
        
        mesh_loaded = false;
        topology_built = false;
    }
};