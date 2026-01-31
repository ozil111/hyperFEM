// main0_explicit.h
// Header for explicit solver logic
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */

#pragma once

// Forward declaration
struct DataContext;

/**
 * @brief Run explicit dynamics solver
 * @param data_context The data context containing the mesh and analysis configuration
 */
void run_explicit_solver(DataContext& data_context);
