#pragma once
#include "entt/entt.hpp"
#include <string>
#include <vector>

namespace Component {

    // 1. Simdroid 特有的 Part 定义
    // Python中的 Part 在这里是一个 Entity，包含 ElementSet 的引用
    struct SimdroidPart {
        std::string name;
        entt::entity element_set; // 指向包含该 Part 所有单元的 Set
        entt::entity material;    // 指向材料实体
        entt::entity section;     // 指向截面属性实体
    };

    // 2. 接触定义 (用于构建连接图)
    enum class ContactType { NodeToSurface, SurfaceToSurface, Unknown };

    struct ContactDefinition {
        std::string name;
        ContactType type;
        
        // 存储的是 Surface 或者 NodeSet 的 Entity Handle
        entt::entity master_entity; 
        entt::entity slave_entity;
        
        double friction;
    };

    // 3. 刚体/MPC 定义 (对传力路径至关重要)
    struct RigidBodyConstraint {
        entt::entity master_node_set; // 或者 master_node entity
        entt::entity slave_node_set;
    };
    
    // 4. 分析结果组件 (用于存储传力路径分析结果，避免重复计算)
    struct ForcePathNode {
        double weight;
        bool is_load_point;
        bool is_constraint_point;
    };

    // 5. [新增] 刚性墙定义
    struct RigidWall {
        int id;
        std::string type; // "Planar", "Cylindrical", "Spherical"
        std::vector<double> parameters; // 平面方程 ax+by+cz+d=0 或圆柱参数等
        entt::entity secondary_node_set; // 关联的从节点集（可选）
    };
}