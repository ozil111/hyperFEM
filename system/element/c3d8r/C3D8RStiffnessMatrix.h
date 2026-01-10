// C3D8RStiffnessMatrix.h
#pragma once

#include "entt/entt.hpp"
#include <Eigen/Dense>

// -------------------------------------------------------------------
// **C3D8R 单元刚度矩阵计算**
// 这是一个纯计算函数，用于计算 C3D8R（8节点六面体）单元的刚度矩阵。
// 使用 B-bar 方法和单点积分（简化版本，暂不考虑沙漏控制）。
// -------------------------------------------------------------------

/**
 * @brief 计算 C3D8R 单元的刚度矩阵（高性能版本，避免内存分配）
 * @param registry EnTT registry，包含单元和节点数据
 * @param element_entity 单元实体句柄
 * @param D 材料的本构矩阵 (6x6)，由调用者传入（避免重复查找）
 * @param Ke_output 输出的刚度矩阵缓冲区（会被 resize 为 24x24）
 * @details 
 *   - 输入：单元实体（必须包含 Connectivity 组件）和 D 矩阵
 *   - 输出：24x24 刚度矩阵写入 Ke_output
 *   - 算法：使用 B-bar 方法，单点积分（参考 FORTRAN 代码）
 *   - 性能：使用输出参数避免堆内存分配
 * 
 * @throws std::runtime_error 如果单元缺少必要的组件
 */
void compute_c3d8r_stiffness_matrix(
    entt::registry& registry,
    entt::entity element_entity,
    const Eigen::Matrix<double, 6, 6>& D,
    Eigen::MatrixXd& Ke_output
);

/**
 * @brief 计算 C3D8R 单元的刚度矩阵（旧版接口，向后兼容）
 * @deprecated 使用输出参数版本以获得更好性能
 */
Eigen::Matrix<double, 24, 24> compute_c3d8r_stiffness_matrix(
    entt::registry& registry,
    entt::entity element_entity
);

