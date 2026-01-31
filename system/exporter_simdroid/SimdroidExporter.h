// system/exporter_simdroid/SimdroidExporter.h
#pragma once
#include "DataContext.h"
#include <string>

class SimdroidExporter {
public:
    /**
     * @brief 导出完整的 Simdroid 工程
     * @param mesh_path 输出 mesh.dat 路径
     * @param control_path 输出 control.json 路径
     * @param ctx 数据上下文 (包含 ECS Registry 和 蓝图 JSON)
     */
    static bool save(const std::string& mesh_path, 
                     const std::string& control_path, 
                     DataContext& ctx);

private:
    static void save_mesh_dat(const std::string& path, entt::registry& registry);
    static void save_control_json(const std::string& path, DataContext& ctx);
};