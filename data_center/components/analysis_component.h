// analysis_component.h
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

} // namespace Component

