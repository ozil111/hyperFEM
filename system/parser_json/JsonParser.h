// system/parser_json/JsonParser.h
#pragma once

#include "DataContext.h"
#include "nlohmann/json_fwd.hpp"
#include <string>
#include <unordered_map>

/**
 * @class JsonParser
 * @brief JSON 格式的 FEM 输入文件解析器
 * @details 采用 N-Step 解析策略，严格按照实体依赖顺序解析：
 * 
 * 解析顺序（关键！）：
 *   1. Material  (无依赖)
 *   2. Property  (依赖 Material)
 *   3. Node      (无依赖)
 *   4. Element   (依赖 Node, Property)
 *   5. NodeSet   (依赖 Node)
 *   6. EleSet    (依赖 Element)
 *   7. Load      (无依赖，但需要在 NodeSet 之后)
 *   8. Boundary  (无依赖，但需要在 NodeSet 之后)
 *   9. Apply Load/Boundary (依赖 Load, Boundary, NodeSet)
 * 
 * 架构特点：
 *   - 使用 nlohmann::json 库解析
 *   - 采用 Plan B 引用模式：实体间通过 entt::entity 句柄关联
 *   - 通过多个 std::unordered_map<int, entt::entity> 暂存用户ID到实体的映射
 *   - 每个解析步骤独立封装为私有方法，易于维护和测试
 */
class JsonParser {
public:
    /**
     * @brief 解析 JSON 格式的输入文件并填充 DataContext
     * @param filepath JSON 文件路径（推荐扩展名：.jsonc 支持注释）
     * @param data_context [out] 将被填充的 DataContext 对象
     * @return true 如果解析成功，false 如果文件无法打开或发生错误
     */
    static bool parse(const std::string& filepath, DataContext& data_context);

private:
    // ====================================================================
    // N-Step 解析方法（按依赖顺序调用）
    // ====================================================================

    /**
     * @brief 步骤 1: 解析 Material 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param material_id_map [out] mid -> entity 映射表
     */
    static void parse_materials(
        const nlohmann::json& j,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& material_id_map
    );

    /**
     * @brief 步骤 2: 解析 Property 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param material_id_map [in] mid -> entity 映射表
     * @param property_id_map [out] pid -> entity 映射表
     */
    static void parse_properties(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& material_id_map,
        std::unordered_map<int, entt::entity>& property_id_map
    );

    /**
     * @brief 步骤 3: 解析 Node 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param node_id_map [out] nid -> entity 映射表
     */
    static void parse_nodes(
        const nlohmann::json& j,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& node_id_map
    );

    /**
     * @brief 步骤 4: 解析 Element 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param node_id_map [in] nid -> entity 映射表
     * @param property_id_map [in] pid -> entity 映射表
     * @param element_id_map [out] eid -> entity 映射表
     */
    static void parse_elements(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& node_id_map,
        const std::unordered_map<int, entt::entity>& property_id_map,
        std::unordered_map<int, entt::entity>& element_id_map
    );

    /**
     * @brief 步骤 5: 解析 NodeSet 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param node_id_map [in] nid -> entity 映射表
     * @param nodeset_id_map [out] nsid -> entity 映射表
     */
    static void parse_nodesets(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& node_id_map,
        std::unordered_map<int, entt::entity>& nodeset_id_map
    );

    /**
     * @brief 步骤 6: 解析 EleSet 实体
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param element_id_map [in] eid -> entity 映射表
     * @param eleset_id_map [out] esid -> entity 映射表
     */
    static void parse_elesets(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& element_id_map,
        std::unordered_map<int, entt::entity>& eleset_id_map
    );

    /**
     * @brief 步骤 6.5: 解析 Curve 实体（曲线定义）
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param curve_id_map [out] cid -> entity 映射表
     */
    static void parse_curves(
        const nlohmann::json& j,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& curve_id_map
    );

    /**
     * @brief 步骤 7: 解析 Load 实体（抽象定义）
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param load_id_map [out] lid -> entity 映射表
     * @param curve_id_map [inout] cid -> entity 映射表（可能被修改以添加默认curve）
     */
    static void parse_loads(
        const nlohmann::json& j,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& load_id_map,
        std::unordered_map<int, entt::entity>& curve_id_map
    );

    /**
     * @brief 步骤 8: 解析 Boundary 实体（抽象定义）
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param boundary_id_map [out] bid -> entity 映射表
     */
    static void parse_boundaries(
        const nlohmann::json& j,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& boundary_id_map
    );

    /**
     * @brief 步骤 9: "应用" Load 到 Node（建立引用关系）
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param load_id_map [in] lid -> entity 映射表
     * @param nodeset_id_map [in] nsid -> entity 映射表
     */
    static void apply_loads(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& load_id_map,
        const std::unordered_map<int, entt::entity>& nodeset_id_map
    );

    /**
     * @brief 步骤 10: "应用" Boundary 到 Node（建立引用关系）
     * @param j JSON 根对象
     * @param registry EnTT registry
     * @param boundary_id_map [in] bid -> entity 映射表
     * @param nodeset_id_map [in] nsid -> entity 映射表
     */
    static void apply_boundaries(
        const nlohmann::json& j,
        entt::registry& registry,
        const std::unordered_map<int, entt::entity>& boundary_id_map,
        const std::unordered_map<int, entt::entity>& nodeset_id_map
    );
};

