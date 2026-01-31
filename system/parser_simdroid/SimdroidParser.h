#pragma once
#include "DataContext.h"
#include "entt/entt.hpp"
#include "nlohmann/json.hpp"
#include <string>
#include <vector>

class SimdroidParser {
    public:
        static bool parse(const std::string& mesh_path, 
                          const std::string& control_path, 
                          DataContext& context);
    private:
        // 使用 Vector 作为 O(1) 查找表
        static std::vector<entt::entity> node_lookup;
        static std::vector<entt::entity> element_lookup;
        
        // 辅助函数
        static void parse_control_json(const std::string& path, DataContext& ctx);
        static void parse_mesh_dat(const std::string& path, DataContext& ctx);

        // New Parsing Helpers (Hierarchical Constraints + Loads)
        static void parse_boundary_conditions(const nlohmann::json& j, entt::registry& registry);
        static void parse_rigid_bodies(const nlohmann::json& j, entt::registry& registry);
        static void parse_loads(const nlohmann::json& j, entt::registry& registry);

        // [新增] Core parsing helpers
        static void parse_initial_conditions(const nlohmann::json& j, entt::registry& registry);
        static void parse_rigid_walls(const nlohmann::json& j, entt::registry& registry);
        static void parse_analysis_settings(const nlohmann::json& j, entt::registry& registry, DataContext& ctx);
        
        // Helper to find a set entity by name
        static entt::entity find_set_by_name(entt::registry& registry, const std::string& name);
    };