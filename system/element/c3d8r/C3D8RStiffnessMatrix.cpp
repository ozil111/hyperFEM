// C3D8RStiffnessMatrix.cpp
#include "C3D8RStiffnessMatrix.h"
#include "../../../data_center/components/mesh_components.h"
#include "../../../data_center/components/property_components.h"
#include "../../../data_center/components/material_components.h"
#include "spdlog/spdlog.h"
#include <cmath>

// ===================================================================
// **第一阶段：常量定义 (Constants Module)**
// ===================================================================

namespace {
    // 沙漏模式向量 H_VECTORS (8x4)
    // FORTRAN: H_VECTORS(8,4) = reshape([...], [8,4])
    // 注意：FORTRAN 是列主序，Eigen 默认也是列主序
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

    // 调优参数（对应 FORTRAN 中的 SCALE_* 参数）
    static constexpr double SCALE_HOURGLASS = 1.0;
    static constexpr double SCALE_K_MATRIX = 1.0;
    static constexpr double SCALE_GAMMA = 1.0;
    static constexpr double SCALE_C_TILDE = 1.0;

    static constexpr double one_over_eight = 1.0 / 8.0;
    static constexpr double WG = 8.0;
}

// -------------------------------------------------------------------
// **辅助函数：计算 B-bar 向量的一个分量**
// 参考 FORTRAN 代码中的 CALC_B_BAR 子程序
// 注意：FORTRAN 索引从 1 开始，C++ 从 0 开始
// -------------------------------------------------------------------
static void calc_b_bar_component(
    const double* y,  // 8个节点的 y 坐标（索引 0-7）
    const double* z,  // 8个节点的 z 坐标（索引 0-7）
    double* BiI       // 输出：8个节点的 B-bar 值（索引 0-7）
) {
    // FORTRAN 代码中的 CALC_B_BAR 逻辑（已转换为 C++ 索引）
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
// 参考 FORTRAN 代码中的 JACOBIAN_CENTER 子程序
// FORTRAN: JAC = matmul(transpose(XiI), COORD) * one_over_eight; JAC = transpose(JAC)
// -------------------------------------------------------------------
static Eigen::Matrix3d jacobian_center(const Eigen::Matrix<double, 8, 3>& coords) {
    // FORTRAN 代码中的 XiI 矩阵（单元中心处的等参坐标导数）
    // FORTRAN reshape 按列填充（列主序），对应单元中心 (0, 0, 0)
    // XiI(8,3) = reshape([第一列8个值, 第二列8个值, 第三列8个值], [8,3])
    static const double one_over_eight = 1.0 / 8.0;
    static const Eigen::Matrix<double, 8, 3> XiI = (Eigen::Matrix<double, 8, 3>() <<
        // 第一列 (xi 导数)
        -1.0,  1.0,  1.0, -1.0, -1.0,  1.0,  1.0, -1.0,
        // 第二列 (eta 导数)
        -1.0, -1.0,  1.0,  1.0, -1.0, -1.0,  1.0,  1.0,
        // 第三列 (zeta 导数)
        -1.0, -1.0, -1.0, -1.0,  1.0,  1.0,  1.0,  1.0
    ).finished();
    
    // FORTRAN: JAC = matmul(transpose(XiI), COORD) * one_over_eight
    // XiI 是 8x3，COORD 是 8x3
    // transpose(XiI) 是 3x8
    // matmul(transpose(XiI), COORD) 是 3x8 * 8x3 = 3x3
    Eigen::Matrix3d JAC = (XiI.transpose() * coords) * one_over_eight;
    
    // FORTRAN: JAC = transpose(JAC)
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
// **辅助函数：计算单元体积（B-bar 方法）**
// 参考 FORTRAN 代码中的 CALC_VOL_BBAR
// -------------------------------------------------------------------
static double calc_vol_bbar(const double* BiI, const double* X) {
    double V = 0.0;
    for (int i = 0; i < 8; ++i) {
        V += X[i] * BiI[i];
    }
    return V;
}

// -------------------------------------------------------------------
// **辅助函数：构建 B 矩阵（6x24）**
// 参考 FORTRAN 代码中的 FORM_B_MATRIX
// 
// [UPDATED] 现在的顺序与 Abaqus/Fortran 保持一致:
// Index: 0->XX, 1->YY, 2->ZZ, 3->XY, 4->YZ, 5->XZ
// -------------------------------------------------------------------
static Eigen::Matrix<double, 6, 24> form_b_matrix(
    const Eigen::Matrix<double, 8, 3>& BiI  // 8x3 的 B-bar 矩阵
) {
    Eigen::Matrix<double, 6, 24> B = Eigen::Matrix<double, 6, 24>::Zero();
    
    for (int K = 0; K < 8; ++K) {
        // FORTRAN: B(1,3*K-2)=BiI(K,1) → C++: B(0, 3*K+0) = BiI(K, 0) [XX]
        B(0, 3*K + 0) = BiI(K, 0);
        
        // FORTRAN: B(2,3*K-1)=BiI(K,2) → C++: B(1, 3*K+1) = BiI(K, 1) [YY]
        B(1, 3*K + 1) = BiI(K, 1);
        
        // FORTRAN: B(3,3*K)=BiI(K,3) → C++: B(2, 3*K+2) = BiI(K, 2) [ZZ]
        B(2, 3*K + 2) = BiI(K, 2);
        
        // -------------------------------------------------------------
        // UPDATE: 适配 Abaqus 顺序 (XY, YZ, XZ)
        // -------------------------------------------------------------

        // Row 3 -> XY (对应 Fortran B(4))
        // xy = ∂v/∂x + ∂u/∂y
        // FORTRAN B(4,3*K-2)=BiI(K,2), B(4,3*K-1)=BiI(K,1)
        B(3, 3*K + 0) = BiI(K, 1);  // u 对 y 的导数 (BiI(:,1))
        B(3, 3*K + 1) = BiI(K, 0);  // v 对 x 的导数 (BiI(:,0))

        // Row 4 -> YZ (对应 Fortran B(5))
        // yz = ∂w/∂y + ∂v/∂z
        // FORTRAN B(5,3*K-1)=BiI(K,3), B(5,3*K)=BiI(K,2)
        B(4, 3*K + 1) = BiI(K, 2);  // v 对 z 的导数 (BiI(:,2))
        B(4, 3*K + 2) = BiI(K, 1);  // w 对 y 的导数 (BiI(:,1))

        // Row 5 -> XZ (对应 Fortran B(6))
        // xz = ∂w/∂x + ∂u/∂z
        // FORTRAN B(6,3*K-2)=BiI(K,3), B(6,3*K)=BiI(K,1)
        B(5, 3*K + 0) = BiI(K, 2);  // u 对 z 的导数 (BiI(:,2))
        B(5, 3*K + 2) = BiI(K, 0);  // w 对 x 的导数 (BiI(:,0))
    }
    
    return B;
}

// ===================================================================
// **第一阶段：极分解与旋转工具 (Polar Decomposition & Rotation)**
// ===================================================================

/**
 * @brief 极分解：J0_T = R * U，计算 R 和 U_diag_inv
 * 参考 FORTRAN: POLAR_DECOMP_FOR_J0HINV
 */
static void polar_decomp_for_j0hinv(
    const Eigen::Matrix3d& J0_T,
    Eigen::Matrix3d& R,
    Eigen::Matrix3d& U_diag_inv
) {
    // 提取列向量 j1, j2, j3
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
    
    // Gram-Schmidt 正交化得到 R
    Eigen::Vector3d q1 = j1 / j1_norm;
    
    Eigen::Vector3d q2 = j2 - j2.dot(q1) * q1;
    q2.normalize();
    
    Eigen::Vector3d q3 = j3 - j3.dot(q1) * q1 - j3.dot(q2) * q2;
    q3.normalize();
    
    // R 矩阵（正交）
    R.row(0) = q1.transpose();
    R.row(1) = q2.transpose();
    R.row(2) = q3.transpose();
    
    // U_diag_inv = diag(1/||j1||, 1/||j2||, 1/||j3||)
    U_diag_inv.setZero();
    U_diag_inv(0, 0) = 1.0 / j1_norm;
    U_diag_inv(1, 1) = 1.0 / j2_norm;
    U_diag_inv(2, 2) = 1.0 / j3_norm;
}

/**
 * @brief 旋转材料矩阵：D_rotated = J^T * D * J
 * 参考 FORTRAN: ROT_DMTX
 */
static void rot_dmtx(
    const Eigen::Matrix<double, 6, 6>& D,
    const Eigen::Matrix3d& J0Inv,
    Eigen::Matrix<double, 6, 6>& D_rotated
) {
    // 提取 J0Inv 的分量
    double j11 = J0Inv(0, 0), j12 = J0Inv(0, 1), j13 = J0Inv(0, 2);
    double j21 = J0Inv(1, 0), j22 = J0Inv(1, 1), j23 = J0Inv(1, 2);
    double j31 = J0Inv(2, 0), j32 = J0Inv(2, 1), j33 = J0Inv(2, 2);
    
    // 构建 6x6 变换矩阵 J_transform
    Eigen::Matrix<double, 6, 6> J_transform;
    J_transform.setZero();
    
    // Row 1
    J_transform(0, 0) = j11 * j11;
    J_transform(0, 1) = j21 * j21;
    J_transform(0, 2) = j31 * j31;
    J_transform(0, 3) = j11 * j21;
    J_transform(0, 4) = j21 * j31;
    J_transform(0, 5) = j11 * j31;
    
    // Row 2
    J_transform(1, 0) = j12 * j12;
    J_transform(1, 1) = j22 * j22;
    J_transform(1, 2) = j32 * j32;
    J_transform(1, 3) = j12 * j22;
    J_transform(1, 4) = j22 * j32;
    J_transform(1, 5) = j12 * j32;
    
    // Row 3
    J_transform(2, 0) = j13 * j13;
    J_transform(2, 1) = j23 * j23;
    J_transform(2, 2) = j33 * j33;
    J_transform(2, 3) = j13 * j23;
    J_transform(2, 4) = j23 * j33;
    J_transform(2, 5) = j13 * j33;
    
    // Row 4
    J_transform(3, 0) = 2.0 * j11 * j12;
    J_transform(3, 1) = 2.0 * j21 * j22;
    J_transform(3, 2) = 2.0 * j31 * j32;
    J_transform(3, 3) = j11 * j22 + j21 * j12;
    J_transform(3, 4) = j21 * j32 + j31 * j22;
    J_transform(3, 5) = j11 * j32 + j31 * j12;
    
    // Row 5
    J_transform(4, 0) = 2.0 * j12 * j13;
    J_transform(4, 1) = 2.0 * j22 * j23;
    J_transform(4, 2) = 2.0 * j32 * j33;
    J_transform(4, 3) = j12 * j23 + j22 * j13;
    J_transform(4, 4) = j22 * j33 + j32 * j23;
    J_transform(4, 5) = j12 * j33 + j32 * j13;
    
    // Row 6
    J_transform(5, 0) = 2.0 * j13 * j11;
    J_transform(5, 1) = 2.0 * j23 * j21;
    J_transform(5, 2) = 2.0 * j33 * j31;
    J_transform(5, 3) = j13 * j21 + j23 * j11;
    J_transform(5, 4) = j23 * j31 + j33 * j21;
    J_transform(5, 5) = j13 * j31 + j33 * j11;
    
    // D_rotated = J_transform^T * D * J_transform
    D_rotated = J_transform.transpose() * D * J_transform;
}

// ===================================================================
// **第二阶段：几何参数计算 (Geometry & Shape)**
// ===================================================================

/**
 * @brief 计算沙漏形状向量 Gamma
 * 参考 FORTRAN: HOURGLASS_SHAPE_VECTORS
 * 公式：Γ_i = (1/8) * [h_i - Σ (h_i · x_a) * b_a]
 */
static void compute_hourglass_shape_vectors(
    const Eigen::Matrix<double, 8, 3>& BiI,
    const Eigen::Matrix<double, 8, 3>& coords,
    Eigen::Matrix<double, 8, 4>& gammas
) {
    gammas.setZero();
    
    for (int i = 0; i < 4; ++i) {
        // 计算 h_i · x_a 对于所有3个方向
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
 * 参考 FORTRAN: GET_CMTXH
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
    
    // Step 2: 极分解得到 R 和 U_diag_inv
    Eigen::Matrix3d R, U_diag_inv;
    polar_decomp_for_j0hinv(J0_T, R, U_diag_inv);
    
    // Step 3: 计算 hat{J0^-1} = R * U_diag_inv
    Eigen::Matrix3d hat_J0_inv = R * U_diag_inv;
    
    // Step 4: 旋转材料矩阵
    rot_dmtx(DMAT, hat_J0_inv, Cmtxh);
    
    // Step 5: 应用 rj 和缩放因子
    Cmtxh *= rj * SCALE_C_TILDE;
}

/**
 * @brief 计算 K 矩阵（K_uu, K_au, K_aa）
 * 参考 FORTRAN: CALC_K_MATRICES
 */
static void calc_k_matrices(
    const Eigen::Matrix<double, 6, 6>& C_tilde,
    double vol,
    Eigen::Matrix<double, 3, 3> K_uu[4][4],
    Eigen::Matrix<double, 6, 3> K_au[4],
    Eigen::Matrix<double, 6, 6>& K_aa
) {
    // 初始化
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
    
    // ========== 对角项 ==========
    
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
    
    // ========== 交叉项 ==========
    
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
 * @brief 计算沙漏刚度矩阵（24x24）- 优化版本
 * 这是主要的沙漏控制函数，对应 VUEL 中的 Step 2 全部逻辑
 * 
 * 优化点：
 * - 使用 Block 操作代替逐元素循环（利用 SIMD）
 * - 预计算 FJAC 转置，避免循环内重复计算
 * - 预计算 K_au^T * K_aa_inv，减少矩阵乘法次数
 * - 使用 noalias() 避免临时矩阵分配
 * - 稀疏优化：跳过极小的 gamma 值
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
    
    // 3. 计算子刚度矩阵
    Eigen::Matrix<double, 3, 3> K_uu[4][4];
    Eigen::Matrix<double, 6, 3> K_au[4];
    Eigen::Matrix<double, 6, 6> K_aa;
    
    calc_k_matrices(C_tilde, vol, K_uu, K_au, K_aa);
    
    // 4. 对 K_aa 求逆（利用对角特性）
    // 优化：预先计算逆矩阵的对角线元素，避免全矩阵乘法
    Eigen::Matrix<double, 6, 6> K_aa_inv;
    K_aa_inv.setZero();
    // 使用 epsilon 避免除零，虽然理论上刚度不应为0
    constexpr double eps = 1.0e-20;
    for (int i = 0; i < 6; ++i) {
        double val = K_aa(i, i);
        if (std::abs(val) > eps) {
            K_aa_inv(i, i) = 1.0 / val;
        }
    }
    
    // 5. 静力凝聚并转换回物理空间 (24x24)
    Ke_hg_out.setZero();
    
    // 预计算 FJAC 的转置，避免在循环中重复计算
    const Eigen::Matrix3d FJAC_T = FJAC.transpose();
    
    // 临时变量放在循环外（优化点 B：减少临时分配）
    Eigen::Matrix3d K_cond;
    Eigen::Matrix3d K_cond_transformed;
    
    // 循环 4x4 模式
    for (int i = 0; i < 4; ++i) {
        // 预计算部分 K_au 项：Temp = K_au_i^T * K_aa_inv
        // K_au[i] 是 6x3，转置是 3x6。K_aa_inv 是 6x6 对角。
        // 因为 K_aa_inv 是对角的，这个乘法其实是对行进行缩放
        Eigen::Matrix<double, 3, 6> Kau_T_KaaInv;
        Kau_T_KaaInv.noalias() = K_au[i].transpose() * K_aa_inv;

        for (int j = 0; j < 4; ++j) {
            // A. 计算凝聚后的 3x3 刚度核
            // K_cond = K_uu[i][j] - (Kau_T_KaaInv * K_au[j])
            K_cond.noalias() = K_uu[i][j] - Kau_T_KaaInv * K_au[j];
            
            // B. 坐标变换：J * K_cond * J^T 
            // CRITICAL FIX: 使用 FJAC (J) 而不是 J0_T (J^T)
            K_cond_transformed.noalias() = FJAC * K_cond * FJAC_T;
            
            // C. 组装到 24x24 矩阵 (Kronecker Product 优化 - 优化点 A)
            // 原理：Ke_block_AB += gamma(A,i)*gamma(B,j) * K_transformed
            // 利用 Block 操作代替逐元素循环，利用 SIMD 指令
            
            // 提取第 i 列和第 j 列 gamma（8x1 向量）
            auto gamma_i = gammas.col(i); 
            auto gamma_j = gammas.col(j);
            
            for (int A = 0; A < 8; ++A) {
                double g_Ai = gamma_i(A);
                // 稀疏优化：如果 g_Ai 极小，跳过内层循环
                if (std::abs(g_Ai) < 1e-15) continue; 

                for (int B = 0; B < 8; ++B) {
                    double coef = g_Ai * gamma_j(B);
                    
                    // 利用 Eigen 的 block 操作直接加上 3x3 矩阵
                    // 这比逐元素加法快得多，因为利用了 SIMD
                    Ke_hg_out.block<3, 3>(3 * A, 3 * B).noalias() += coef * K_cond_transformed;
                }
            }
        }
    }
    
    // 应用体积因子和缩放
    Ke_hg_out *= (vol / 8.0) * SCALE_HOURGLASS;
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
    
    // 2. 获取连接性
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
    
    // 4. 计算 B-bar 矩阵（D 矩阵已由调用者传入，无需查找）
    Eigen::Matrix<double, 8, 3> BiI;
    
    // 提取坐标分量（注意 FORTRAN 代码中的顺序）
    double y[8], z[8];
    double x[8];
    for (int i = 0; i < 8; ++i) {
        x[i] = coords(i, 0);
        y[i] = coords(i, 1);
        z[i] = coords(i, 2);
    }
    
    // 计算三个分量的 B-bar
    calc_b_bar_component(y, z, BiI.data() + 0*8);  // x 分量（对应 BiI(:,1)）
    calc_b_bar_component(z, x, BiI.data() + 1*8);  // y 分量（对应 BiI(:,2)）
    calc_b_bar_component(x, y, BiI.data() + 2*8);  // z 分量（对应 BiI(:,3)）
    
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
    
    // 9. 构建 B 矩阵（6x24）
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
    
    // 12. 总刚度矩阵 = 体积刚度 + 沙漏刚度
    K_total += K_hg;
    
    // 输出到缓冲区
    Ke_output = K_total;
}

// -------------------------------------------------------------------
// **向后兼容版本：计算 C3D8R 单元刚度矩阵（旧接口）**
// -------------------------------------------------------------------
Eigen::Matrix<double, 24, 24> compute_c3d8r_stiffness_matrix(
    entt::registry& registry,
    entt::entity element_entity
) {
    // 获取材料 D 矩阵
    if (!registry.all_of<Component::PropertyRef>(element_entity)) {
        throw std::runtime_error("Element entity missing PropertyRef component");
    }
    
    const auto& property_ref = registry.get<Component::PropertyRef>(element_entity);
    entt::entity property_entity = property_ref.property_entity;
    
    if (!registry.all_of<Component::MaterialRef>(property_entity)) {
        throw std::runtime_error("Property entity missing MaterialRef component");
    }
    
    const auto& material_ref = registry.get<Component::MaterialRef>(property_entity);
    entt::entity material_entity = material_ref.material_entity;
    
    if (!registry.all_of<Component::LinearElasticMatrix>(material_entity)) {
        throw std::runtime_error("Material entity missing LinearElasticMatrix component. "
                                "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
    }
    
    const auto& material_matrix = registry.get<Component::LinearElasticMatrix>(material_entity);
    if (!material_matrix.is_initialized) {
        throw std::runtime_error("Material D matrix not initialized. "
                                "Please call LinearElasticMatrixSystem::compute_linear_elastic_matrix() first.");
    }
    
    const Eigen::Matrix<double, 6, 6>& D = material_matrix.D;
    
    // 调用新版本函数
    Eigen::MatrixXd Ke_buffer;
    compute_c3d8r_stiffness_matrix(registry, element_entity, D, Ke_buffer);
    
    // 返回固定大小矩阵
    return Eigen::Matrix<double, 24, 24>(Ke_buffer);
}

