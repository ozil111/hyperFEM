// analysis_component.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <vector>
#include <string>
#include "entt/entt.hpp"

// Type aliases for clarity and consistency
using AnalysisID = int;

/**
 * @namespace Component
 * @brief Contains all ECS components for analysis representation
 * @details Components are organized by domain:
 *   - Analysis components: AnalysisType, EndTime, FixedTimeStep   
 */
namespace Component {

    // ===================================================================
    // Analysis Components
    // ===================================================================

    /**
     * @brief Analysis type component
     * @details Attached to entities representing analysis type
     */
    struct AnalysisType {
        std::string value;
    };

    /**
     * @brief Stores the analysis ID from the input file
     * @details Attached to entities representing analysis ID
     */
    struct AnalysisID {
        int value;
    };
    /**
     * @brief End time component
     * @details Attached to entities representing end time
     */
    struct EndTime {
        double value;
    };

    /**
     * @brief Fixed time step component
     * @details Attached to entities representing fixed time step
     */
    struct FixedTimeStep {
        double value;
    };

    /**
     * @brief Node output component
     * @details Attached to entities representing output
     */
    struct NodeOutput {
        std::vector<std::string> node_output;
    };

    struct ElementOutput {
        std::vector<std::string> element_output;
    };

    struct OutputIntervalTime {
        double interval_time;
    };  

    // [新增] Output control (e.g. d3plot interval)
    struct OutputControl {
        double interval;
    };

} // namespace Component

