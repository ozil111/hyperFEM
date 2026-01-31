// AppSession.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once
#include "DataContext.h"
#include "TopologyData.h"
#include <simdroid/SimdroidInspector.h>

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
    SimdroidInspector inspector;

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
        inspector.clear(); // 清空缓存
        mesh_loaded = false;
        topology_built = false;
    }
};