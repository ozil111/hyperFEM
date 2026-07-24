// C3D8RStiffnessMatrix.cpp
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 NovaFEA. All rights reserved.
 * Author: Xiaotong Wang (or NovaFEA Team)
 */
#include "C3D8RStiffnessMatrix.h"
#include "../../../data_center/TopologyData.h"
#include "../../../data_center/components/mesh_components.h"
#include "../../../data_center/components/simdroid_components.h"
#include "../../../data_center/components/material_components.h"
#include <spdlog/spdlog.h>
#include <cmath>

// ===================================================================
// **第一阶段：常量定�?(Constants Module)**
// ===================================================================

namespace {
    // 沙漏模式向量 H_VECTORS (8x4)
    // FORTRAN: H_VECTORS(8,4) = reshape([...], [8,4])
    // 注意：FORTRAN 是列主序，Eigen 默认也是列主�?
    static const Eigen::Matrix<double, 8, 4> H_VECTORS = (Eigen::Matrix<double, 8, 4>() <<
        // h1: (1, -1, 1, -1, 1, -1, 1, -1)
         1.0,  1.0,  1.0, -1.0,
        -1.0, -1.0,  1.0,  1.0,
         1.0, -1.0, -1.0, -1.0,
        -1.0,  1.0, -1.0,  1.0,
         1.0, -1.0, -1.0,  1.0,
        -1.0,  1.0, -1.0, -1.0,
         1.0,  1.0,  1.0,  1.0,
        -1.0, -1.0,  1.0, -1.0
    ).finished();

    // 调优参数（对�?FORTRAN 中的 SCALE_* 参数�?
    static constexpr double SCALE_HOURGLASS = 1.0;
    static constexpr double SCALE_K_MATRIX = 1.0;
    static constexpr double SCALE_GAMMA = 1.0;
    static constexpr double SCALE_C_TILDE = 1.0;

    static constexpr double one_over_eight = 1.0 / 8.0;
    static constexpr double WG = 8.0;

    // 是否使用多点雅可比路径（对应 FORTRAN USE_MULTIPOINT_JACOBIAN）
    // 多点路径使用 2×2×2 高斯点处的雅可比矩阵进行物理空间投影，
    // 对于翘曲/畸变单元可捕获雅可比在单元内部的变化
    static constexpr bool USE_MULTIPOINT_JACOBIAN = true;
}

// -------------------------------------------------------------------
// **辅助函数：计�?B-bar 向量的一个分�?*
// 参�?FORTRAN 代码中的 CALC_B_BAR 子程�?
// 注意：FORTRAN 索引�?1 开始，C++ �?0 开�?
// -------------------------------------------------------------------
static void calc_b_bar_component(
    const double* y,  // 8个节点的 y 坐标（索�?0-7�?
    const double* z,  // 8个节点的 z 坐标（索�?0-7�?
    double* BiI       // 输出�?个节点的 B-bar 值（索引 0-7�?
) {
    // FORTRAN 代码中的 CALC_B_BAR 逻辑（已转换�?C++ 索引�?
    // FORTRAN BiI(1) 对应 C++ BiI[0]，FORTRAN y(1) 对应 C++ y[0]
    BiI[0] = -(y[1]*(z[2]+z[3]-z[4]-z[5])+y[2]*(-z[1]+z[3])
             +y[3]*(-z[1]-z[2]+z[4]+z[7])+y[4]*(z[1]-z[3]+z[5]-z[7])
             +y[5]*(z[1]-z[4])+y[7]*(-z[3]+z[4]))/12.0;
    
    BiI[1] = (y[0]*(z[2]+z[3]-z[4]-z[5])+y[2]*(-z[0]-z[3]+z[5]+z[6])
             +y[3]*(-z[0]+z[2])+y[4]*(z[0]-z[5])
             +y[5]*(z[0]-z[2]+z[4]-z[6])+y[6]*(-z[2]+z[5]))/12.0;
    
    BiI[2] = -(y[0]*(z[1]-z[3])+y[1]*(-z[0]-z[3]+z[5]+z[6])
             +y[3]*(z[0]+z[1]-z[6]-z[7])+y[5]*(-z[1]+z[6])
             +y[6]*(-z[1]+z[3]-z[5]+z[7])+y[7]*(z[3]-z[6]))/12.0;
    
    BiI[3] = -(y[0]*(z[1]+z[2]-z[4]-z[7])+y[1]*(-z[0]+z[2])
             +y[2]*(-z[0]-z[1]+z[6]+z[7])+y[4]*(z[0]-z[7])
             +y[6]*(-z[2]+z[7])+y[7]*(z[0]-z[2]+z[4]-z[6]))/12.0;
    
    BiI[4] = (y[0]*(z[1]-z[3]+z[5]-z[7])+y[1]*(-z[0]+z[5])
             +y[3]*(z[0]-z[7])+y[5]*(-z[0]-z[1]+z[6]+z[7])
             +y[6]*(-z[5]+z[7])+y[7]*(z[0]+z[3]-z[5]-z[6]))/12.0;
    
    BiI[5] = (y[0]*(z[1]-z[4])+y[1]*(-z[0]+z[2]-z[4]+z[6])
             +y[2]*(-z[1]+z[6])+y[4]*(z[0]+z[1]-z[6]-z[7])
             +y[6]*(-z[1]-z[2]+z[4]+z[7])+y[7]*(z[4]-z[6]))/12.0;
    
    BiI[6] = (y[1]*(z[2]-z[5])+y[2]*(-z[1]+z[3]-z[5]+z[7])
             +y[3]*(-z[2]+z[7])+y[4]*(z[5]-z[7])
             +y[5]*(z[1]+z[2]-z[4]-z[7])+y[7]*(-z[2]-z[3]+z[4]+z[5]))/12.0;
    
    BiI[7] = -(y[0]*(z[3]-z[4])+y[2]*(-z[3]+z[6])
             +y[3]*(-z[0]+z[2]-z[4]+z[6])+y[4]*(z[0]+z[3]-z[5]-z[6])
             +y[5]*(z[4]-z[6])+y[6]*(-z[2]-z[3]+z[4]+z[5]))/12.0;
}

// -------------------------------------------------------------------
// **辅助函数：计算单元中心处的雅可比矩阵**
// 参�?FORTRAN 代码中的 JACOBIAN_CENTER 子程�?
// FORTRAN: JAC = matmul(transpose(XiI), COORD) * one_over_eight; JAC = transpose(JAC)
// -------------------------------------------------------------------
static Eigen::Matrix3d jacobian_center(const Eigen::Matrix<double, 8, 3>& coords) {
    // FORTRAN: XiI(8,3) at center (0,0,0), column-major reshape
    // 手动初始化避免 comma initializer 的跨平台不确定性
    static const double one_over_eight = 1.0 / 8.0;
    static Eigen::Matrix<double, 8, 3> XiI;
    static bool XiI_init = false;
    if (!XiI_init) {
        // Column 0 (xi derivatives):  [-1, 1, 1,-1,-1, 1, 1,-1]
        XiI(0,0)=-1; XiI(1,0)= 1; XiI(2,0)= 1; XiI(3,0)=-1;
        XiI(4,0)=-1; XiI(5,0)= 1; XiI(6,0)= 1; XiI(7,0)=-1;
        // Column 1 (eta derivatives): [-1,-1, 1, 1,-1,-1, 1, 1]
        XiI(0,1)=-1; XiI(1,1)=-1; XiI(2,1)= 1; XiI(3,1)= 1;
        XiI(4,1)=-1; XiI(5,1)=-1; XiI(6,1)= 1; XiI(7,1)= 1;
        // Column 2 (zeta derivatives):[-1,-1,-1,-1, 1, 1, 1, 1]
        XiI(0,2)=-1; XiI(1,2)=-1; XiI(2,2)=-1; XiI(3,2)=-1;
        XiI(4,2)= 1; XiI(5,2)= 1; XiI(6,2)= 1; XiI(7,2)= 1;
        XiI_init = true;
    }
    
    // FORTRAN: JAC = matmul(transpose(XiI), COORD) * one_over_eight; JAC = transpose(JAC)
    Eigen::Matrix3d JAC = (XiI.transpose() * coords) * one_over_eight;
    return JAC.transpose();
}

// -------------------------------------------------------------------
// **辅助函数：计算雅可比矩阵的行列式**
// -------------------------------------------------------------------
static double jacobian_determinant(const Eigen::Matrix3d& JAC) {
    return JAC(0,0)*(JAC(1,1)*JAC(2,2)-JAC(1,2)*JAC(2,1)) -
           JAC(0,1)*(JAC(1,0)*JAC(2,2)-JAC(1,2)*JAC(2,0)) +
           JAC(0,2)*(JAC(1,0)*JAC(2,1)-JAC(1,1)*JAC(2,0));
}

// -------------------------------------------------------------------
// **辅助函数：计算任意参数点处的雅可比矩阵和行列式**
// 参考 FORTRAN 代码中的 JACOBIAN_AT_POINT 子程序
// JAC(i,j) = ∂X_i / ∂ξ_j，与 jacobian_center 的约定一致
// -------------------------------------------------------------------
static void jacobian_at_point(
    double xi, double eta, double zeta,
    const Eigen::Matrix<double, 8, 3>& coords,
    Eigen::Matrix3d& JAC,
    double& DETJ
) {
    // Node parametric coordinates (对应 FORTRAN XiI)
    static const double xi_i[8]  = {-1.0, 1.0, 1.0,-1.0,-1.0, 1.0, 1.0,-1.0};
    static const double eta_i[8] = {-1.0,-1.0, 1.0, 1.0,-1.0,-1.0, 1.0, 1.0};
    static const double zeta_i[8]= {-1.0,-1.0,-1.0,-1.0, 1.0, 1.0, 1.0, 1.0};

    JAC.setZero();
    for (int A = 0; A < 8; ++A) {
        // ∂N_A/∂ξ = (1/8) * ξ_A * (1 + η·η_A) * (1 + ζ·ζ_A)
        double dNdxi   = one_over_eight * xi_i[A]   * (1.0 + eta  * eta_i[A])  * (1.0 + zeta * zeta_i[A]);
        // ∂N_A/∂η = (1/8) * η_A * (1 + ξ·ξ_A) * (1 + ζ·ζ_A)
        double dNdeta  = one_over_eight * eta_i[A]  * (1.0 + xi   * xi_i[A])   * (1.0 + zeta * zeta_i[A]);
        // ∂N_A/∂ζ = (1/8) * ζ_A * (1 + ξ·ξ_A) * (1 + η·η_A)
        double dNdzeta = one_over_eight * zeta_i[A] * (1.0 + xi   * xi_i[A])   * (1.0 + eta  * eta_i[A]);

        // JAC(i,j) = Σ_A ∂N_A/∂ξ_j · X_i^A
        JAC(0, 0) += coords(A, 0) * dNdxi;
        JAC(0, 1) += coords(A, 0) * dNdeta;
        JAC(0, 2) += coords(A, 0) * dNdzeta;
        JAC(1, 0) += coords(A, 1) * dNdxi;
        JAC(1, 1) += coords(A, 1) * dNdeta;
        JAC(1, 2) += coords(A, 1) * dNdzeta;
        JAC(2, 0) += coords(A, 2) * dNdxi;
        JAC(2, 1) += coords(A, 2) * dNdeta;
        JAC(2, 2) += coords(A, 2) * dNdzeta;
    }

    DETJ = jacobian_determinant(JAC);
}

// -------------------------------------------------------------------
// **辅助函数：计算单元体积（B-bar 方法�?*
// 参�?FORTRAN 代码中的 CALC_VOL_BBAR
// -------------------------------------------------------------------
static double calc_vol_bbar(const double* BiI, const double* X) {
    double V = 0.0;
    for (int i = 0; i < 8; ++i) {
        V += X[i] * BiI[i];
    }
    return V;
}

// -------------------------------------------------------------------
// **辅助函数：构�?B 矩阵�?x24�?*
// 参�?FORTRAN 代码中的 FORM_B_MATRIX
// 
// [UPDATED] 现在的顺序与 Abaqus/Fortran 保持一�?
// Index: 0->XX, 1->YY, 2->ZZ, 3->XY, 4->YZ, 5->XZ
// -------------------------------------------------------------------
static Eigen::Matrix<double, 6, 24> form_b_matrix(
    const Eigen::Matrix<double, 8, 3>& BiI  // 8x3 �?B-bar 矩阵
) {
    Eigen::Matrix<double, 6, 24> B = Eigen::Matrix<double, 6, 24>::Zero();
    
    for (int K = 0; K < 8; ++K) {
        // FORTRAN: B(1,3*K-2)=BiI(K,1) �?C++: B(0, 3*K+0) = BiI(K, 0) [XX]
        B(0, 3*K + 0) = BiI(K, 0);
        
        // FORTRAN: B(2,3*K-1)=BiI(K,2) �?C++: B(1, 3*K+1) = BiI(K, 1) [YY]
        B(1, 3*K + 1) = BiI(K, 1);
        
        // FORTRAN: B(3,3*K)=BiI(K,3) �?C++: B(2, 3*K+2) = BiI(K, 2) [ZZ]
        B(2, 3*K + 2) = BiI(K, 2);
        
        // -------------------------------------------------------------
        // UPDATE: 适配 Abaqus 顺序 (XY, YZ, XZ)
        // -------------------------------------------------------------

        // Row 3 -> XY (对应 Fortran B(4))
        // xy = ∂v/∂x + ∂u/∂y
        // FORTRAN B(4,3*K-2)=BiI(K,2), B(4,3*K-1)=BiI(K,1)
        B(3, 3*K + 0) = BiI(K, 1);  // u �?y 的导�?(BiI(:,1))
        B(3, 3*K + 1) = BiI(K, 0);  // v �?x 的导�?(BiI(:,0))

        // Row 4 -> YZ (对应 Fortran B(5))
        // yz = ∂w/∂y + ∂v/∂z
        // FORTRAN B(5,3*K-1)=BiI(K,3), B(5,3*K)=BiI(K,2)
        B(4, 3*K + 1) = BiI(K, 2);  // v �?z 的导�?(BiI(:,2))
        B(4, 3*K + 2) = BiI(K, 1);  // w �?y 的导�?(BiI(:,1))

        // Row 5 -> XZ (对应 Fortran B(6))
        // xz = ∂w/∂x + ∂u/∂z
        // FORTRAN B(6,3*K-2)=BiI(K,3), B(6,3*K)=BiI(K,1)
        B(5, 3*K + 0) = BiI(K, 2);  // u �?z 的导�?(BiI(:,2))
        B(5, 3*K + 2) = BiI(K, 0);  // w �?x 的导�?(BiI(:,0))
    }
    
    return B;
}

// ===================================================================
// **第一阶段：极分解与旋转工具 (Polar Decomposition & Rotation)**
// ===================================================================

/**
 * @brief 极分解：J0_T = R * U，计�?R �?U_diag_inv
 * 参�?FORTRAN: POLAR_DECOMP_FOR_J0HINV
 */
static void polar_decomp_for_j0hinv(
    const Eigen::Matrix3d& J0_T,
    Eigen::Matrix3d& R,
    Eigen::Matrix3d& U_diag_inv
) {
    // 提取行向�?j1, j2, j3（FORTRAN: J0_T(1,:), J0_T(2,:), J0_T(3,:)�?
    Eigen::Vector3d j1 = J0_T.row(0).transpose();
    Eigen::Vector3d j2 = J0_T.row(1).transpose();
    Eigen::Vector3d j3 = J0_T.row(2).transpose();
    
    // 计算范数
    double j1_norm = j1.norm();
    double j2_norm = j2.norm();
    double j3_norm = j3.norm();
    
    if (j1_norm < 1.0e-20 || j2_norm < 1.0e-20 || j3_norm < 1.0e-20) {
        throw std::runtime_error("Jacobian column norm is zero or too small in polar decomposition");
    }
    
    // U_diag_inv = diag(1/||j1||, 1/||j2||, 1/||j3||)
    U_diag_inv.setZero();
    U_diag_inv(0, 0) = 1.0 / j1_norm;
    U_diag_inv(1, 1) = 1.0 / j2_norm;
    U_diag_inv(2, 2) = 1.0 / j3_norm;

    // USE_DIAGONAL_J0_INV = TRUE（对应 FORTRAN uel_constants）:
    // Puso (2000) Section 5.2: 对于各向同性材料，R 可以忽略，
    // hat_J0_inv = U_diag_inv（纯对角），跳过 Gram-Schmidt。
    // 对于规则网格无影响；对于畸变网格消除了 R 引入的离对角耦合。
    R.setZero();
    R(0, 0) = 1.0;
    R(1, 1) = 1.0;
    R(2, 2) = 1.0;
}

/**
 * @brief 旋转材料矩阵：D_rotated = J^T * D * J
 * 参�?FORTRAN: ROT_DMTX
 */
static void rot_dmtx(
    const Eigen::Matrix<double, 6, 6>& D,
    const Eigen::Matrix3d& J0Inv,
    Eigen::Matrix<double, 6, 6>& D_rotated
) {
    // 提取 J0Inv 的分量（与 FORTRAN 一致：行优先）
    // jXY = J0Inv 的第 X �?Y �?
    double j11 = J0Inv(0, 0), j12 = J0Inv(0, 1), j13 = J0Inv(0, 2);
    double j21 = J0Inv(1, 0), j22 = J0Inv(1, 1), j23 = J0Inv(1, 2);
    double j31 = J0Inv(2, 0), j32 = J0Inv(2, 1), j33 = J0Inv(2, 2);
    
    // 构建 6x6 变换矩阵 J_transform
    // Voigt �?XX, YY, ZZ, XY, YZ, XZ
    // FORTRAN: 每行使用 J0Inv 的对应行（而非列）
    Eigen::Matrix<double, 6, 6> J_transform;
    J_transform.setZero();
    
    // Row 0 (XX) — 使用 J0Inv �?0 �?
    J_transform(0, 0) = j11 * j11;
    J_transform(0, 1) = j12 * j12;
    J_transform(0, 2) = j13 * j13;
    J_transform(0, 3) = 2.0 * j11 * j12;
    J_transform(0, 4) = 2.0 * j12 * j13;
    J_transform(0, 5) = 2.0 * j11 * j13;
    
    // Row 1 (YY) — 使用 J0Inv �?1 �?
    J_transform(1, 0) = j21 * j21;
    J_transform(1, 1) = j22 * j22;
    J_transform(1, 2) = j23 * j23;
    J_transform(1, 3) = 2.0 * j21 * j22;
    J_transform(1, 4) = 2.0 * j22 * j23;
    J_transform(1, 5) = 2.0 * j21 * j23;
    
    // Row 2 (ZZ) — 使用 J0Inv �?2 �?
    J_transform(2, 0) = j31 * j31;
    J_transform(2, 1) = j32 * j32;
    J_transform(2, 2) = j33 * j33;
    J_transform(2, 3) = 2.0 * j31 * j32;
    J_transform(2, 4) = 2.0 * j32 * j33;
    J_transform(2, 5) = 2.0 * j31 * j33;
    
    // Row 3 (XY) — 使用 J0Inv �?0 �?1 �?
    J_transform(3, 0) = j11 * j21;
    J_transform(3, 1) = j12 * j22;
    J_transform(3, 2) = j13 * j23;
    J_transform(3, 3) = j11 * j22 + j12 * j21;
    J_transform(3, 4) = j12 * j23 + j13 * j22;
    J_transform(3, 5) = j11 * j23 + j13 * j21;
    
    // Row 4 (YZ) — 使用 J0Inv �?1 �?2 �?
    J_transform(4, 0) = j21 * j31;
    J_transform(4, 1) = j22 * j32;
    J_transform(4, 2) = j23 * j33;
    J_transform(4, 3) = j21 * j32 + j22 * j31;
    J_transform(4, 4) = j22 * j33 + j23 * j32;
    J_transform(4, 5) = j21 * j33 + j23 * j31;
    
    // Row 5 (XZ) — 使用 J0Inv �?0 �?2 �?
    J_transform(5, 0) = j11 * j31;
    J_transform(5, 1) = j12 * j32;
    J_transform(5, 2) = j13 * j33;
    J_transform(5, 3) = j11 * j32 + j12 * j31;
    J_transform(5, 4) = j12 * j33 + j13 * j32;
    J_transform(5, 5) = j11 * j33 + j13 * j31;
    
    // D_rotated = J_transform^T * D * J_transform
    D_rotated = J_transform.transpose() * D * J_transform;
}

// ===================================================================
// **第二阶段：几何参数计�?(Geometry & Shape)**
// ===================================================================

/**
 * @brief 计算沙漏形状向量 Gamma
 * 参�?FORTRAN: HOURGLASS_SHAPE_VECTORS
 * 公式：Γ_i = (1/8) * [h_i - Σ (h_i · x_a) * b_a]
 */
static void compute_hourglass_shape_vectors(
    const Eigen::Matrix<double, 8, 3>& BiI,
    const Eigen::Matrix<double, 8, 3>& coords,
    Eigen::Matrix<double, 8, 4>& gammas
) {
    gammas.setZero();
    
    for (int i = 0; i < 4; ++i) {
        // 计算 h_i · x_a 对于所�?个方�?
        Eigen::Vector3d h_dot_x = Eigen::Vector3d::Zero();
        for (int A = 0; A < 8; ++A) {
            h_dot_x(0) += H_VECTORS(A, i) * coords(A, 0);
            h_dot_x(1) += H_VECTORS(A, i) * coords(A, 1);
            h_dot_x(2) += H_VECTORS(A, i) * coords(A, 2);
        }
        
        // 计算 γ = (1/8) * [h - (h·x) * b]
        for (int A = 0; A < 8; ++A) {
            double h_dot_b = h_dot_x(0) * BiI(A, 0) + 
                            h_dot_x(1) * BiI(A, 1) + 
                            h_dot_x(2) * BiI(A, 2);
            gammas(A, i) = SCALE_GAMMA * one_over_eight * (H_VECTORS(A, i) - h_dot_b);
        }
    }
}

// ===================================================================
// **第三阶段：增强应变刚度核 (EAS Stiffness Kernel)**
// ===================================================================

/**
 * @brief 计算旋转后的材料矩阵 C_tilde
 * 参�?FORTRAN: GET_CMTXH
 * 对于线弹性小变形，rj = 1.0
 */
static void get_cmtxh(
    const Eigen::Matrix<double, 6, 6>& DMAT,
    const Eigen::Matrix3d& FJAC,
    double rj,
    Eigen::Matrix<double, 6, 6>& Cmtxh
) {
    // Step 1: 计算 J0^T
    Eigen::Matrix3d J0_T = FJAC.transpose();
    
    // Step 2: 极分解得�?R �?U_diag_inv
    Eigen::Matrix3d R, U_diag_inv;
    polar_decomp_for_j0hinv(J0_T, R, U_diag_inv);
    
    // Step 3: 计算 hat{J0^-1} = R * U_diag_inv
    Eigen::Matrix3d hat_J0_inv = R * U_diag_inv;
    
    // Step 4: 旋转材料矩阵
    rot_dmtx(DMAT, hat_J0_inv, Cmtxh);
    
    // Step 5: 应用 rj 和缩放因�?
    Cmtxh *= rj * SCALE_C_TILDE;
}

/**
 * @brief 计算 K 矩阵（K_uu, K_au, K_aa�?
 * 参�?FORTRAN: CALC_K_MATRICES
 */
static void calc_k_matrices(
    const Eigen::Matrix<double, 6, 6>& C_tilde,
    double vol,
    Eigen::Matrix<double, 3, 3> K_uu[4][4],
    Eigen::Matrix<double, 6, 3> K_au[4],
    Eigen::Matrix<double, 6, 6>& K_aa
) {
    // 初始�?
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            K_uu[i][j].setZero();
        }
        K_au[i].setZero();
    }
    K_aa.setZero();
    
    // 定义因子
    double factor_K123 = (8.0 / 3.0) * SCALE_K_MATRIX;
    double factor_K4 = (8.0 / 9.0) * SCALE_K_MATRIX;
    double factor_Kau = (8.0 / 3.0) * SCALE_K_MATRIX;
    
    // 提取 C 矩阵分量（简化符号）
    const Eigen::Matrix<double, 6, 6>& C = C_tilde;
    
    // ========== 对角�?==========
    
    // K^11
    K_uu[0][0](0, 0) = factor_K123 * C(0, 0);
    K_uu[0][0](0, 2) = factor_K123 * C(0, 5);
    K_uu[0][0](1, 1) = factor_K123 * C(1, 1);
    K_uu[0][0](1, 2) = factor_K123 * C(1, 4);
    K_uu[0][0](2, 0) = factor_K123 * C(5, 0);
    K_uu[0][0](2, 1) = factor_K123 * C(4, 1);
    K_uu[0][0](2, 2) = factor_K123 * (C(4, 4) + C(5, 5));
    
    // K^22
    K_uu[1][1](0, 0) = factor_K123 * C(0, 0);
    K_uu[1][1](0, 1) = factor_K123 * C(0, 3);
    K_uu[1][1](1, 0) = factor_K123 * C(3, 0);
    K_uu[1][1](1, 1) = factor_K123 * (C(4, 4) + C(3, 3));
    K_uu[1][1](1, 2) = factor_K123 * C(4, 2);
    K_uu[1][1](2, 1) = factor_K123 * C(2, 4);
    K_uu[1][1](2, 2) = factor_K123 * C(2, 2);
    
    // K^33
    K_uu[2][2](0, 0) = factor_K123 * (C(5, 5) + C(3, 3));
    K_uu[2][2](0, 1) = factor_K123 * C(3, 1);
    K_uu[2][2](0, 2) = factor_K123 * C(5, 2);
    K_uu[2][2](1, 0) = factor_K123 * C(1, 3);
    K_uu[2][2](1, 1) = factor_K123 * C(1, 1);
    K_uu[2][2](2, 0) = factor_K123 * C(2, 5);
    K_uu[2][2](2, 2) = factor_K123 * C(2, 2);
    
    // K^44
    K_uu[3][3](0, 0) = factor_K4 * C(0, 0);
    K_uu[3][3](1, 1) = factor_K4 * C(1, 1);
    K_uu[3][3](2, 2) = factor_K4 * C(2, 2);
    
    // ========== 交叉�?==========
    
    // K^12
    K_uu[0][1](1, 1) = factor_K123 * C(1, 4);
    K_uu[0][1](1, 2) = factor_K123 * C(1, 2);
    K_uu[0][1](2, 1) = factor_K123 * C(4, 4);
    K_uu[0][1](2, 2) = factor_K123 * C(4, 2);
    
    // K^13
    K_uu[0][2](0, 0) = factor_K123 * C(0, 5);
    K_uu[0][2](0, 2) = factor_K123 * C(0, 2);
    K_uu[0][2](2, 0) = factor_K123 * C(5, 5);
    K_uu[0][2](2, 2) = factor_K123 * C(5, 2);
    
    // K^21
    K_uu[1][0](1, 1) = factor_K123 * C(4, 1);
    K_uu[1][0](1, 2) = factor_K123 * C(4, 4);
    K_uu[1][0](2, 1) = factor_K123 * C(2, 1);
    K_uu[1][0](2, 2) = factor_K123 * C(2, 4);
    
    // K^23
    K_uu[1][2](0, 0) = factor_K123 * C(0, 3);
    K_uu[1][2](0, 1) = factor_K123 * C(0, 1);
    K_uu[1][2](1, 0) = factor_K123 * C(3, 3);
    K_uu[1][2](1, 1) = factor_K123 * C(3, 1);
    
    // K^31
    K_uu[2][0](0, 0) = factor_K123 * C(5, 0);
    K_uu[2][0](0, 2) = factor_K123 * C(5, 5);
    K_uu[2][0](2, 0) = factor_K123 * C(2, 0);
    K_uu[2][0](2, 2) = factor_K123 * C(2, 5);
    
    // K^32
    K_uu[2][1](0, 0) = factor_K123 * C(3, 0);
    K_uu[2][1](0, 1) = factor_K123 * C(3, 3);
    K_uu[2][1](1, 0) = factor_K123 * C(1, 0);
    K_uu[2][1](1, 1) = factor_K123 * C(1, 3);
    
    // ========== K_alpha_u ==========
    
    // K_alpha_u^1
    K_au[0](0, 1) = factor_Kau * C(0, 1);
    K_au[0](0, 2) = factor_Kau * C(0, 4);
    K_au[0](1, 0) = factor_Kau * C(1, 0);
    K_au[0](1, 2) = factor_Kau * C(1, 5);
    
    // K_alpha_u^2
    K_au[1](0, 1) = factor_Kau * C(0, 4);
    K_au[1](0, 2) = factor_Kau * C(0, 2);
    K_au[1](2, 0) = factor_Kau * C(2, 0);
    K_au[1](2, 1) = factor_Kau * C(2, 3);
    
    // K_alpha_u^3
    K_au[2](1, 0) = factor_Kau * C(1, 5);
    K_au[2](1, 2) = factor_Kau * C(1, 2);
    K_au[2](2, 0) = factor_Kau * C(2, 3);
    K_au[2](2, 1) = factor_Kau * C(2, 1);
    
    // K_alpha_u^4
    double H43 = C(0, 2) + C(1, 2) + C(2, 2);
    double H51 = C(0, 0) + C(1, 0) + C(2, 0);
    double H62 = C(0, 1) + C(1, 1) + C(2, 1);
    K_au[3](3, 2) = factor_K4 * H43;
    K_au[3](4, 0) = factor_K4 * H51;
    K_au[3](5, 1) = factor_K4 * H62;
    
    // ========== K_alpha_alpha ==========
    double H = C(0, 0) + C(1, 1) + C(2, 2) + 
               2.0 * (C(0, 1) + C(1, 2) + C(0, 2));
    
    K_aa(0, 0) = factor_Kau * C(0, 0);
    K_aa(1, 1) = factor_Kau * C(1, 1);
    K_aa(2, 2) = factor_Kau * C(2, 2);
    K_aa(3, 3) = factor_Kau * H / 3.0;
    K_aa(4, 4) = factor_Kau * H / 3.0;
    K_aa(5, 5) = factor_Kau * H / 3.0;
}

// ===================================================================
// **第四阶段：静力凝聚 (Static Condensation) - OPTIMIZED**
// ===================================================================

/**
 * @brief 计算沙漏刚度矩阵�?4x24�? 优化版本
 * 这是主要的沙漏控制函数，对应 VUEL 中的 Step 2 全部逻辑
 * 
 * 优化点：
 * - 使用 Block 操作代替逐元素循环（利用 SIMD�?
 * - 预计�?FJAC 转置，避免循环内重复计算
 * - 预计�?K_au^T * K_aa_inv，减少矩阵乘法次�?
 * - 使用 noalias() 避免临时矩阵分配
 * - 稀疏优化：跳过极小�?gamma �?
 */
static void compute_hourglass_stiffness(
    const Eigen::Matrix<double, 8, 3>& coords,
    const Eigen::Matrix<double, 8, 3>& BiI,
    const Eigen::Matrix3d& FJAC,
    const Eigen::Matrix<double, 6, 6>& D_mat,
    double vol,
    Eigen::Matrix<double, 24, 24>& Ke_hg_out
) {
    // 1. 计算 Gamma 向量 (8x4)
    Eigen::Matrix<double, 8, 4> gammas;
    compute_hourglass_shape_vectors(BiI, coords, gammas);
    
    // 2. 计算旋转后的材料矩阵 C_tilde (6x6)
    // 性能优化：直接在栈上分配，不初始化为0（后续会被覆盖）
    Eigen::Matrix<double, 6, 6> C_tilde;
    get_cmtxh(D_mat, FJAC, 1.0, C_tilde);
    
    // 3. 计算子刚度矩�?
    Eigen::Matrix<double, 3, 3> K_uu[4][4];
    Eigen::Matrix<double, 6, 3> K_au[4];
    Eigen::Matrix<double, 6, 6> K_aa;
    
    calc_k_matrices(C_tilde, vol, K_uu, K_au, K_aa);
    
    // 4. �?K_aa 求逆（利用对角特性）
    // 优化：预先计算逆矩阵的对角线元素，避免全矩阵乘�?
    Eigen::Matrix<double, 6, 6> K_aa_inv;
    K_aa_inv.setZero();
    // 使用 epsilon 避免除零，虽然理论上刚度不应�?
    constexpr double eps = 1.0e-20;
    for (int i = 0; i < 6; ++i) {
        double val = K_aa(i, i);
        if (std::abs(val) > eps) {
            K_aa_inv(i, i) = 1.0 / val;
        }
    }
    
    // 5. 静力凝聚并转换回物理空间 (24x24)
    Ke_hg_out.setZero();

    // 多点雅可比：预计算 2×2×2 高斯点处的 J_k 和 det(J_k)
    // 对应 FORTRAN: uel_implicit.for 中 GP_1D = ±1/√3
    Eigen::Matrix3d GP_JAC[8];
    double GP_DETJ[8];
    if (USE_MULTIPOINT_JACOBIAN) {
        const double G = 1.0 / std::sqrt(3.0);
        const double gp_pts[2] = {-G, G};
        int gp_idx = 0;
        for (int ig = 0; ig < 2; ++ig) {
            for (int jg = 0; jg < 2; ++jg) {
                for (int kg = 0; kg < 2; ++kg) {
                    jacobian_at_point(gp_pts[ig], gp_pts[jg], gp_pts[kg],
                                     coords, GP_JAC[gp_idx], GP_DETJ[gp_idx]);
                    ++gp_idx;
                }
            }
        }
    }

    // 预计�?FJAC 的转置，避免在循环中重复计算（单点路径使用）
    const Eigen::Matrix3d FJAC_T = FJAC.transpose();

    // 临时变量放在循环外（优化�?B：减少临时分配）
    Eigen::Matrix3d K_cond;
    Eigen::Matrix3d K_cond_transformed;

    // 循环 4x4 模式
    for (int i = 0; i < 4; ++i) {
        // 预计算部�?K_au 项：Temp = K_au_i^T * K_aa_inv
        // K_au[i] �?6x3，转置是 3x6。K_aa_inv �?6x6 对角�?
        // 因为 K_aa_inv 是对角的，这个乘法其实是对行进行缩放
        Eigen::Matrix<double, 3, 6> Kau_T_KaaInv;
        Kau_T_KaaInv.noalias() = K_au[i].transpose() * K_aa_inv;

        for (int j = 0; j < 4; ++j) {
            // A. 计算凝聚后的 3x3 刚度�?
            // K_cond = K_uu[i][j] - (Kau_T_KaaInv * K_au[j])
            K_cond.noalias() = K_uu[i][j] - Kau_T_KaaInv * K_au[j];

            // B. 坐标变换：
            // 多点路径: M_IJ = (1/8) * Σ_k det(J_k) * J_k * K_cond * J_k^T
            // 单点路径: M_IJ = J0 * K_cond * J0^T
            if (USE_MULTIPOINT_JACOBIAN) {
                K_cond_transformed.setZero();
                for (int gp = 0; gp < 8; ++gp) {
                    // K_cond_transformed += det(J_k) * J_k * K_cond * J_k^T
                    K_cond_transformed.noalias() += GP_DETJ[gp]
                        * (GP_JAC[gp] * K_cond * GP_JAC[gp].transpose());
                }
                K_cond_transformed *= one_over_eight;
            } else {
                K_cond_transformed.noalias() = FJAC * K_cond * FJAC_T;
            }

            // C. 组装�?24x24 矩阵 (Kronecker Product 优化 - 优化�?A)
            // 原理：Ke_block_AB += gamma(A,i)*gamma(B,j) * M_IJ
            // 利用 Block 操作代替逐元素循环，利用 SIMD 指令

            // 提取�?i 列和�?j �?gamma�?x1 向量�?
            auto gamma_i = gammas.col(i);
            auto gamma_j = gammas.col(j);

            for (int A = 0; A < 8; ++A) {
                double g_Ai = gamma_i(A);
                // 稀疏优化：如果 g_Ai 极小，跳过内层循�?
                if (std::abs(g_Ai) < 1e-15) continue;

                for (int B = 0; B < 8; ++B) {
                    double coef = g_Ai * gamma_j(B);

                    // 利用 Eigen �?block 操作直接加上 3x3 矩阵
                    // 这比逐元素加法快得多，因为利用了 SIMD
                    Ke_hg_out.block<3, 3>(3 * A, 3 * B).noalias() += coef * K_cond_transformed;
                }
            }
        }
    }

    // 应用缩放因子
    // 多点路径: M_IJ 已包含 det(J_k) 权重，只需 × SCALE_HOURGLASS
    // 单点路径: M_IJ = J0*Kc*J0^T 不含 detJ，需 × DETJ(=vol/8) × SCALE_HOURGLASS
    if (USE_MULTIPOINT_JACOBIAN) {
        Ke_hg_out *= SCALE_HOURGLASS;
    } else {
        Ke_hg_out *= (vol / 8.0) * SCALE_HOURGLASS;
    }
}

// -------------------------------------------------------------------
// **主函数：计算 C3D8R 单元刚度矩阵（高性能版本）**
// -------------------------------------------------------------------
void compute_c3d8r_stiffness_matrix(
    entt::registry& registry,
    entt::entity element_entity,
    const Eigen::Matrix<double, 6, 6>& D,
    Eigen::MatrixXd& Ke_output
) {
    // 1. 检查单元实体是否包含必要的组件
    if (!registry.all_of<Component::Connectivity>(element_entity)) {
        throw std::runtime_error("Element entity missing Connectivity component");
    }
    
    // 2. 获取连接�?
    const auto& connectivity = registry.get<Component::Connectivity>(element_entity);
    if (connectivity.nodes.size() != 8) {
        throw std::runtime_error("C3D8R element must have exactly 8 nodes");
    }
    
    // 3. 获取节点坐标
    Eigen::Matrix<double, 8, 3> coords;
    for (size_t i = 0; i < 8; ++i) {
        if (!registry.all_of<Component::Position>(connectivity.nodes[i])) {
            throw std::runtime_error("Node entity missing Position component");
        }
        const auto& pos = registry.get<Component::Position>(connectivity.nodes[i]);
        coords(i, 0) = pos.x;
        coords(i, 1) = pos.y;
        coords(i, 2) = pos.z;
    }
    
    // 4. 计算 B-bar 矩阵（D 矩阵已由调用者传入，无需查找�?
    Eigen::Matrix<double, 8, 3> BiI;
    
    // 提取坐标分量（注�?FORTRAN 代码中的顺序�?
    double y[8], z[8];
    double x[8];
    for (int i = 0; i < 8; ++i) {
        x[i] = coords(i, 0);
        y[i] = coords(i, 1);
        z[i] = coords(i, 2);
    }
    
    // 计算三个分量�?B-bar
    calc_b_bar_component(y, z, BiI.data() + 0*8);  // x 分量（对�?BiI(:,1)�?
    calc_b_bar_component(z, x, BiI.data() + 1*8);  // y 分量（对�?BiI(:,2)�?
    calc_b_bar_component(x, y, BiI.data() + 2*8);  // z 分量（对�?BiI(:,3)�?
    
    // 6. 计算单元体积
    double VOL = calc_vol_bbar(BiI.data() + 0*8, x);  // 使用第一个分量
    
    // 7. 归一化 B-bar 矩阵（除以体积）
    if (std::abs(VOL) < 1.0e-20) {
        throw std::runtime_error("Element volume is zero or too small");
    }
    BiI /= VOL;
    
    // 8. 计算雅可比矩阵和行列式（单元中心）
    Eigen::Matrix3d JAC = jacobian_center(coords);
    double DETJ = jacobian_determinant(JAC);
    
    if (std::abs(DETJ) < 1.0e-20) {
        throw std::runtime_error("Jacobian determinant is zero or too small");
    }
    
    // 9. 构建 B 矩阵 (6x24)
    Eigen::Matrix<double, 6, 24> B = form_b_matrix(BiI);
    
    // 10. 计算体积刚度矩阵（优化点 D：使用 noalias 和优化乘法顺序）
    // K_vol = B^T * D * B * detJ * WG
    // 优化：先计算 D * B (6x24)，再计算 B^T * (D*B) (24x24)
    // 这样比 B^T * D (24x6) * B (6x24) 要快，因为中间矩阵更小且更利于缓存
    double scale_vol = DETJ * WG;
    Eigen::Matrix<double, 24, 24> K_total;
    
    // Step 1: DB = D * B (6x24)
    Eigen::Matrix<double, 6, 24> DB;
    DB.noalias() = D * B;
    
    // Step 2: K_vol = B^T * DB * scale_vol
    K_total.noalias() = B.transpose() * DB * scale_vol;
    
    // 11. 计算沙漏刚度矩阵（Puso EAS 方法）
    // 直接累加到 K_total 上，避免创建额外的 K_hg 矩阵（内存优化）
    Eigen::Matrix<double, 24, 24> K_hg;
    compute_hourglass_stiffness(coords, BiI, JAC, D, DETJ * WG, K_hg);
    
    // 12. 总刚度矩�?= 体积刚度 + 沙漏刚度
    K_total += K_hg;
    
    // 输出到缓冲区
    Ke_output = K_total;
}

// -------------------------------------------------------------------
// Post-process: compute element stress and strain from displacement
// -------------------------------------------------------------------
bool compute_c3d8r_stress_strain(
    const entt::registry& registry,
    entt::entity element_entity,
    const Eigen::Matrix<double, 6, 6>& D,
    Eigen::Vector<double, 6>& stress,
    Eigen::Vector<double, 6>& strain
) {
    if (!registry.all_of<Component::Connectivity>(element_entity))
        return false;
    const auto& connectivity = registry.get<Component::Connectivity>(element_entity);
    if (connectivity.nodes.size() != 8)
        return false;

    Eigen::Matrix<double, 8, 3> coords;
    for (size_t i = 0; i < 8; ++i) {
        if (!registry.all_of<Component::Position>(connectivity.nodes[i]))
            return false;
        const auto& pos = registry.get<Component::Position>(connectivity.nodes[i]);
        coords(i, 0) = pos.x;
        coords(i, 1) = pos.y;
        coords(i, 2) = pos.z;
    }

    Eigen::Matrix<double, 8, 3> BiI;
    double x[8], y[8], z[8];
    for (int i = 0; i < 8; ++i) {
        x[i] = coords(i, 0);
        y[i] = coords(i, 1);
        z[i] = coords(i, 2);
    }
    calc_b_bar_component(y, z, BiI.data() + 0 * 8);
    calc_b_bar_component(z, x, BiI.data() + 1 * 8);
    calc_b_bar_component(x, y, BiI.data() + 2 * 8);

    double VOL = calc_vol_bbar(BiI.data() + 0 * 8, x);
    if (std::abs(VOL) < 1.0e-20)
        return false;
    BiI /= VOL;

    Eigen::Matrix<double, 6, 24> B = form_b_matrix(BiI);

    Eigen::Vector<double, 24> u_elem;
    for (size_t i = 0; i < 8; ++i) {
        entt::entity node = connectivity.nodes[i];
        double dx = 0.0, dy = 0.0, dz = 0.0;
        if (registry.all_of<Component::Displacement>(node)) {
            const auto& disp = registry.get<Component::Displacement>(node);
            dx = disp.dx;
            dy = disp.dy;
            dz = disp.dz;
        }
        u_elem(3 * i + 0) = dx;
        u_elem(3 * i + 1) = dy;
        u_elem(3 * i + 2) = dz;
    }

    strain = B * u_elem;
    stress = D * strain;

    return true;
}

// -------------------------------------------------------------------
// **向后兼容版本：计�?C3D8R 单元刚度矩阵（旧接口�?*
// -------------------------------------------------------------------
Eigen::Matrix<double, 24, 24> compute_c3d8r_stiffness_matrix(
    entt::registry& registry,
    entt::entity element_entity
) {
    // 通过 Part 获取材料 D 矩阵（element -> TopologyData -> Part -> material�?
    if (!registry.all_of<Component::ElementID>(element_entity)) {
        throw std::runtime_error("Element entity missing ElementID component");
    }
    if (!registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
        throw std::runtime_error("TopologyData not found. Run topology extraction and ensure SimdroidPart are built.");
    }
    auto& topology = *registry.ctx().get<std::unique_ptr<TopologyData>>();
    int eid = registry.get<Component::ElementID>(element_entity).value;
    if (eid < 0 || static_cast<size_t>(eid) >= topology.element_uid_to_part_map.size()) {
        throw std::runtime_error("Element ID out of range for element_uid_to_part_map");
    }
    entt::entity part_entity = topology.element_uid_to_part_map[static_cast<size_t>(eid)];
    if (part_entity == entt::null || !registry.all_of<Component::SimdroidPart>(part_entity)) {
        throw std::runtime_error("No Part found for element");
    }
    entt::entity material_entity = registry.get<Component::SimdroidPart>(part_entity).material;

    if (!registry.all_of<Component::LinearElasticMatrix>(material_entity)) {
        throw std::runtime_error("Material entity missing LinearElasticMatrix component. "
                                "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
    }
    const auto& material_matrix = registry.get<Component::LinearElasticMatrix>(material_entity);
    if (!material_matrix.is_initialized) {
        throw std::runtime_error("Material D matrix not initialized. "
                                "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
    }
    Eigen::Map<Eigen::Matrix<double, 6, 6, Eigen::RowMajor>> D(const_cast<double*>(material_matrix.D));

    Eigen::MatrixXd Ke_buffer;
    compute_c3d8r_stiffness_matrix(registry, element_entity, D, Ke_buffer);
    return Eigen::Matrix<double, 24, 24>(Ke_buffer);
}

