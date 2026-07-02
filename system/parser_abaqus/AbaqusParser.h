// AbaqusParser.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#pragma once

#include "DataContext.h"
#include <string>

/**
 * @class AbaqusParser
 * @brief Abaqus .inp format FEM input file parser
 * @details Parses Abaqus input decks into the same ECS-based DataContext used by
 *          JsonParser and SimdroidParser. Reuses existing components without
 *          introducing new ones.
 *
 * Supported keywords (T01 case scope):
 *   *NODE, *ELEMENT, *NSET, *ELSET, *MATERIAL (+*DENSITY, *ELASTIC),
 *   *SOLID SECTION, *AMPLITUDE, *STEP (+*DYNAMIC, *BOUNDARY, *CLOAD),
 *   *END STEP
 *
 * Architecture:
 *   - Two-phase: pre-scan file into keyword blocks, then process by dependency order
 *   - Reuses all existing Component definitions (NodeID, Position, ElementID, etc.)
 *   - Abaqus element type strings (C3D8R, ...) are mapped to NovaFEA numeric type IDs
 */
class AbaqusParser {
public:
    /**
     * @brief Parse an Abaqus .inp file and populate the DataContext registry
     * @param filepath Path to the .inp file
     * @param data_context [out] DataContext to populate (cleared before parsing)
     * @return true on success, false if the file cannot be opened
     */
    static bool parse(const std::string& filepath, DataContext& data_context);
};
