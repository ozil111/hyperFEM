// AssemblySystem.h
#pragma once

#include "entt/entt.hpp"
#include <Eigen/Sparse>
#include <Eigen/Dense>

// -------------------------------------------------------------------
// **组装系统 (Assembly System)**
// 负责将单元刚度矩阵组装到全局刚度矩阵中。
// 包含分发器（Dispatcher）和组装循环（Assembly Loop）。
// -------------------------------------------------------------------

class AssemblySystem {
public:
    // 使用 Eigen::SparseMatrix 作为全局刚度矩阵类型
    using SparseMatrix = Eigen::SparseMatrix<double, Eigen::RowMajor>;
    using Triplet = Eigen::Triplet<double>;

    /**
     * @brief [Dispatcher] 根据单元类型分发到相应的刚度矩阵计算函数（高性能版本）
     * @param registry EnTT registry
     * @param element_entity 单元实体句柄
     * @param Ke_buffer 输出的单元刚度矩阵缓冲区（会被 resize 为相应大小）
     * @return true 如果成功计算，false 如果不支持的单元类型
     * @details 
     *   - 自动获取节点坐标和材料 D 矩阵
     *   - 根据单元类型 ID 调用相应的计算函数
     *   - 使用输出参数避免堆内存分配，提高性能
     */
    static bool compute_element_stiffness_dispatcher(
        entt::registry& registry,
        entt::entity element_entity,
        Eigen::MatrixXd& Ke_buffer
    );

    /**
     * @brief [Assembly] 组装全局刚度矩阵
     * @param registry EnTT registry
     * @param K_global 输出的全局刚度矩阵（稀疏矩阵）
     * @details 
     *   - 遍历所有单元，调用 dispatcher 获取单元刚度矩阵
     *   - 将单元刚度矩阵组装到全局矩阵中
     *   - 使用 registry.ctx<DofMap>() 中的映射（需要先运行 DofNumberingSystem）
     * 
     * @pre 必须事先调用 DofNumberingSystem::build_dof_map(registry)
     */
    static void assemble_stiffness(
        entt::registry& registry,
        SparseMatrix& K_global
    );
};

