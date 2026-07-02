// AbaqusExporter.h
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
 * @class AbaqusExporter
 * @brief Abaqus .inp format exporter (inverse of AbaqusParser)
 * @details Exports an ECS-based DataContext to an Abaqus input deck.
 *          The output is compatible with `abaqus job=... int` execution.
 *
 * Export order (matches Abaqus conventions):
 *   *HEADING
 *   *NODE
 *   *ELEMENT,TYPE=...
 *   *ELSET (section-related)
 *   *SOLID SECTION
 *   *NSET / *ELSET (analysis-related)
 *   *MATERIAL (+*DENSITY, *ELASTIC)
 *   *AMPLITUDE
 *   *STEP (+*DYNAMIC, *BOUNDARY, *CLOAD, *OUTPUT, *END STEP)
 */
class AbaqusExporter {
public:
    /**
     * @brief Save the model in DataContext to an Abaqus .inp file
     * @param filepath Output .inp file path
     * @param data_context [in] DataContext with registry populated by a parser
     * @return true on success, false if the file cannot be opened
     */
    static bool save(const std::string& filepath, const DataContext& data_context);
};
