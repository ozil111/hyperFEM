// GaussIntegration.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <vector>
#include <Eigen/Dense>

/**
 * @namespace GaussIntegration
 * @brief Gaussian integration utilities for finite element analysis
 * @details Provides Gaussian quadrature points and weights for numerical integration
 */
namespace GaussIntegration {

    /**
     * @brief Get Gaussian quadrature points and weights for 1D integration
     * @param n_points Number of integration points (1, 2, 3, etc.)
     * @param points Output vector of integration point coordinates in [-1, 1]
     * @param weights Output vector of integration weights
     * @return true if successful, false if n_points is not supported
     * @details
     *   - 1 point: Standard midpoint rule
     *   - 2 points: 2-point Gauss quadrature
     *   - 3 points: 3-point Gauss quadrature
     *   - Higher orders can be added as needed
     */
    bool get_1d_gauss_points(int n_points, std::vector<double>& points, std::vector<double>& weights);

    /**
     * @brief Get Gaussian quadrature points and weights for 3D hexahedron integration
     * @param n_points_per_dim Number of integration points per dimension (1, 2, 3, etc.)
     * @param points Output matrix (n_points^3 x 3) of integration point coordinates in [-1, 1]^3
     * @param weights Output vector of integration weights
     * @return true if successful, false if n_points_per_dim is not supported
     * @details
     *   - For n_points_per_dim = 1: Single point at (0, 0, 0) with weight 8.0
     *   - For n_points_per_dim = 2: 8 points (2x2x2) with appropriate weights
     *   - For n_points_per_dim = 3: 27 points (3x3x3) with appropriate weights
     */
    bool get_3d_hex_gauss_points(int n_points_per_dim, 
                                  Eigen::MatrixXd& points, 
                                  std::vector<double>& weights);

}
