// LinearElasticMatrixSystem.cpp
#include "LinearElasticMatrixSystem.h"
#include "spdlog/spdlog.h"

// -------------------------------------------------------------------
// **System: 计算线性弹性材料的本构矩阵**
// -------------------------------------------------------------------
void LinearElasticMatrixSystem::compute_linear_elastic_matrix(entt::registry& registry) {
    spdlog::info("LinearElasticMatrixSystem: Computing D matrices for linear elastic materials...");
    
    // 获取所有具有 LinearElasticParams 的材料实体
    auto material_view = registry.view<const Component::LinearElasticParams>();
    
    size_t material_count = 0;
    for (auto material_entity : material_view) {
        const auto& params = material_view.get<const Component::LinearElasticParams>(material_entity);
        
        // 检查参数有效性
        if (params.E <= 0.0) {
            spdlog::warn("Material entity {} has invalid Young's modulus E = {}", 
                        static_cast<std::uint64_t>(material_entity), params.E);
            continue;
        }
        
        if (params.nu < -1.0 || params.nu >= 0.5) {
            spdlog::warn("Material entity {} has invalid Poisson's ratio nu = {} (should be in [-1, 0.5))", 
                        static_cast<std::uint64_t>(material_entity), params.nu);
            continue;
        }
        
        // 计算 Lamé 参数
        auto [lambda, mu] = compute_lame_parameters(params.E, params.nu);
        
        // 构建 D 矩阵
        Eigen::Matrix<double, 6, 6> D = build_d_matrix_3d_isotropic(lambda, mu);
        
        // 获取或创建 LinearElasticMatrix 组件
        auto& matrix_comp = registry.get_or_emplace<Component::LinearElasticMatrix>(material_entity);
        matrix_comp.D = D;
        matrix_comp.is_initialized = true;
        
        material_count++;
    }
    
    spdlog::info("LinearElasticMatrixSystem: Computed D matrices for {} material(s).", material_count);
}

// -------------------------------------------------------------------
// **辅助函数: 计算 Lamé 参数**
// -------------------------------------------------------------------
std::pair<double, double> LinearElasticMatrixSystem::compute_lame_parameters(double E, double nu) {
    // lambda = E * nu / ((1 + nu) * (1 - 2*nu))
    double lambda = E * nu / ((1.0 + nu) * (1.0 - 2.0 * nu));
    
    // mu = G = E / (2 * (1 + nu))  (剪切模量)
    double mu = E / (2.0 * (1.0 + nu));
    
    return {lambda, mu};
}

// -------------------------------------------------------------------
// **辅助函数: 构建 3D 各向同性材料的 D 矩阵**
// -------------------------------------------------------------------
Eigen::Matrix<double, 6, 6> LinearElasticMatrixSystem::build_d_matrix_3d_isotropic(double lambda, double mu) {
    // 3D 各向同性材料的本构矩阵 (Voigt notation)
    // 应变顺序: [xx, yy, zz, xy, yz, xz] (Abaqus/Fortran 顺序)
    // 应力顺序: [xx, yy, zz, xy, yz, xz] (Abaqus/Fortran 顺序)
    
    Eigen::Matrix<double, 6, 6> D = Eigen::Matrix<double, 6, 6>::Zero();
    
    // 填充主对角块 (3x3)
    double diag_value = lambda + 2.0 * mu;  // 对角元素
    double off_diag_value = lambda;         // 非对角元素
    
    D(0, 0) = diag_value;      // xx-xx
    D(1, 1) = diag_value;      // yy-yy
    D(2, 2) = diag_value;      // zz-zz
    
    D(0, 1) = off_diag_value;  // xx-yy
    D(0, 2) = off_diag_value;  // xx-zz
    D(1, 0) = off_diag_value;  // yy-xx
    D(1, 2) = off_diag_value;  // yy-zz
    D(2, 0) = off_diag_value;  // zz-xx
    D(2, 1) = off_diag_value;  // zz-yy
    
    // 填充剪切项 (3x3 块)
    D(3, 3) = mu;  // xy-xy
    D(4, 4) = mu;  // yz-yz
    D(5, 5) = mu;  // xz-xz
    
    return D;
}

