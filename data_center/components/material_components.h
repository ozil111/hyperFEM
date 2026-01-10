// data_center/components/material_component.h
#pragma once

#include <vector>
#include <string>
#include <Eigen/Dense>

/**
 * @namespace Component
 * @brief 包含所有ECS组件 - 材料部分
 */
namespace Component {

    /**
     * @brief [通用] 标识这是一个材料实体
     * @details 附加到所有材料实体上，存储其用户定义的ID (mid)。
     * 这是连接单元和材料的"主键"。
     */
    struct MaterialID {
        int value;
    };

    /**
     * @brief [Type 1] 各向同性线弹性参数
     * @details 对应 typeid = 1
     */
    struct LinearElasticParams {
        double rho; // 密度
        double E;   // 弹性模量
        double nu;  // 泊松比
    };

    /**
     * @brief [派生数据] 线性弹性本构矩阵 (D Matrix)
     * @details 这是一个运行时生成的组件，挂载在 Material 实体上。
     * 对于 3D 各向同性材料，这是一个 6x6 矩阵。
     * 使用 Abaqus/Fortran 顺序: xx, yy, zz, xy, yz, xz
     */
    struct LinearElasticMatrix {
        // 使用 Eigen 存储 6x6 矩阵 (Voigt notation: xx, yy, zz, xy, yz, xz - Abaqus/Fortran 顺序)
        Eigen::Matrix<double, 6, 6> D;
        
        // 标记是否已初始化，防止重复计算
        bool is_initialized = false;
    };

    /**
     * @brief [通用-超弹性] 超弹性材料的通用数据
     * @details 存储 mode=0 (直接输入) 或 mode=1 (曲线拟合)
     * 这个组件可以附加到所有超弹性材料实体上。
     */
    struct HyperelasticMode {
        int order;
        bool fit_from_data; // 对应 mode 0 (false) 或 1 (true)
        
        // 仅在 fit_from_data == true 时有意义
        std::vector<int> uniaxial_funcs;
        std::vector<int> biaxial_funcs;
        std::vector<int> planar_funcs;
        std::vector<int> volumetric_funcs;
        double nu; // 泊松比，仅用于曲线拟合
    };

    /**
     * @brief [Type 101] Polynomial (N=order)
     * @details 仅在 HyperelasticMode::fit_from_data == false 时使用
     */
    struct PolynomialParams {
        // Cij, 存储顺序 [C10, C01, C20, C02, ..., CN0, C0N]
        std::vector<double> c_ij; 
        // Di, 存储顺序 [D1, D2, ..., DN]
        std::vector<double> d_i;  
    };

    /**
     * @brief [Type 102] Reduced Polynomial (N=order)
     * @details 仅在 HyperelasticMode::fit_from_data == false 时使用
     */
    struct ReducedPolynomialParams {
        // Ci0, 存储顺序 [C10, C20, ..., CN0]
        std::vector<double> c_i0; 
        // Di, 存储顺序 [D1, D2, ..., DN]
        std::vector<double> d_i;  
    };

    /**
     * @brief [Type 103] Ogden (N=order)
     * @details 仅在 HyperelasticMode::fit_from_data == false 时使用
     */
    struct OgdenParams {
        // 存储顺序 [mu1, mu2, ..., muN]
        std::vector<double> mu_i;
        // 存储顺序 [alpha1, alpha2, ..., alphaN]
        std::vector<double> alpha_i;
        // 存储顺序 [D1, D2, ..., DN]
        std::vector<double> d_i;
    };

    // ... 未来可以继续添加 Type 2xx (粘弹), 3xx (弹塑性) 的参数组件 ...
    // struct ViscoelasticParams { ... };
    // struct PlasticParams { ... };

} // namespace Component