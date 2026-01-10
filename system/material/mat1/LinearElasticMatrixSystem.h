// LinearElasticMatrixSystem.h
#pragma once

#include "entt/entt.hpp"
#include "../../../data_center/components/material_components.h"

// -------------------------------------------------------------------
// **材料系统 (Material Systems)**
// 这个类是无状态的，所有函数都是静态的。
// 它操作EnTT registry，从中读取材料参数组件，
// 计算并生成派生的材料矩阵组件。
// -------------------------------------------------------------------
class LinearElasticMatrixSystem {
public:
    /**
     * @brief [System] 计算线性弹性材料的本构矩阵 (D Matrix)
     * @details 遍历所有具有 LinearElasticParams 的材料实体，
     * 根据 E（弹性模量）和 nu（泊松比）计算 3D 各向同性材料的 D 矩阵。
     * 计算结果存储在 LinearElasticMatrix 组件中。
     * @param registry EnTT registry，包含材料实体及其参数组件
     */
    static void compute_linear_elastic_matrix(entt::registry& registry);

private:
    /**
     * @brief 辅助函数：根据 E 和 nu 计算 Lamé 参数
     * @param E 弹性模量
     * @param nu 泊松比
     * @return std::pair<lambda, mu> Lamé 参数
     */
    static std::pair<double, double> compute_lame_parameters(double E, double nu);

    /**
     * @brief 辅助函数：构建 3D 各向同性材料的 D 矩阵
     * @param lambda 第一 Lamé 参数
     * @param mu 第二 Lamé 参数（剪切模量）
     * @return 6x6 本构矩阵 (Voigt notation: xx, yy, zz, xy, yz, xz - Abaqus/Fortran 顺序)
     */
    static Eigen::Matrix<double, 6, 6> build_d_matrix_3d_isotropic(double lambda, double mu);
};

