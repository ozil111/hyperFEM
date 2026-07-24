// system/output/VtkExporter.h
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
 * @brief Legacy VTK format exporter (UnstructuredGrid, ASCII)
 * @details Writes .vtk files in legacy VTK format for ParaView/HyperView compatibility.
 *          Format: # vtk DataFile Version 3.0, ASCII, DATASET UNSTRUCTURED_GRID.
 */
class VtkExporter {
public:
    static bool save(const std::string& filepath, const DataContext& data_context, entt::entity output_entity = entt::null);
};
