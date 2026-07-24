// system/output/CsvExporter.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 */
#pragma once

#include <string>
#include <entt/entt.hpp>

struct DataContext;

/**
 * @brief Abaqus-compatible CSV exporter
 * @details Writes displacement and element field CSVs in the same format
 *          as Abaqus standard output for direct comparison.
 *
 * Output files:
 *   <prefix>_disp.csv   - instance,node_label,u1,u2,u3,mag
 *   <prefix>_elements.csv - variable,component,location,instance,
 *                           element_label,node_label,integration_point,value
 */
class CsvExporter {
public:
    /**
     * @brief Export post-solve results to CSV files
     * @param prefix    Output file prefix (produces <prefix>_disp.csv and <prefix>_elements.csv)
     * @param data_context  DataContext with registry containing solved displacement
     * @return true on success, false on failure
     */
    static bool save(const std::string& prefix, const DataContext& data_context);
};
