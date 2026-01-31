// GaussIntegration.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#include "GaussIntegration.h"
#include <cmath>
#include <stdexcept>

namespace GaussIntegration {

bool get_1d_gauss_points(int n_points, std::vector<double>& points, std::vector<double>& weights) {
    points.clear();
    weights.clear();
    
    switch (n_points) {
        case 1: {
            // 1-point Gauss quadrature (midpoint rule)
            points.push_back(0.0);
            weights.push_back(2.0);
            return true;
        }
        
        case 2: {
            // 2-point Gauss quadrature
            double sqrt3_inv = 1.0 / std::sqrt(3.0);
            points.push_back(-sqrt3_inv);
            points.push_back(sqrt3_inv);
            weights.push_back(1.0);
            weights.push_back(1.0);
            return true;
        }
        
        case 3: {
            // 3-point Gauss quadrature
            double sqrt15 = std::sqrt(15.0);
            points.push_back(-std::sqrt(3.0/5.0));
            points.push_back(0.0);
            points.push_back(std::sqrt(3.0/5.0));
            weights.push_back(5.0/9.0);
            weights.push_back(8.0/9.0);
            weights.push_back(5.0/9.0);
            return true;
        }
        
        default:
            // TODO: Implement higher-order Gauss quadrature (4, 5, etc.)
            return false;
    }
}

bool get_3d_hex_gauss_points(int n_points_per_dim, 
                              Eigen::MatrixXd& points, 
                              std::vector<double>& weights) {
    // Get 1D points and weights
    std::vector<double> xi_points, xi_weights;
    if (!get_1d_gauss_points(n_points_per_dim, xi_points, xi_weights)) {
        return false;
    }
    
    // Generate 3D tensor product
    int total_points = n_points_per_dim * n_points_per_dim * n_points_per_dim;
    points.resize(total_points, 3);
    weights.clear();
    weights.reserve(total_points);
    
    int idx = 0;
    for (int i = 0; i < n_points_per_dim; ++i) {
        for (int j = 0; j < n_points_per_dim; ++j) {
            for (int k = 0; k < n_points_per_dim; ++k) {
                points(idx, 0) = xi_points[i];
                points(idx, 1) = xi_points[j];
                points(idx, 2) = xi_points[k];
                weights.push_back(xi_weights[i] * xi_weights[j] * xi_weights[k]);
                idx++;
            }
        }
    }
    
    return true;
}

} // namespace GaussIntegration
