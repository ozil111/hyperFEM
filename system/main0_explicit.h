// main0_explicit.h
// Header for explicit solver logic

#pragma once

// Forward declaration
struct DataContext;

/**
 * @brief Run explicit dynamics solver
 * @param data_context The data context containing the mesh and analysis configuration
 */
void run_explicit_solver(DataContext& data_context);
