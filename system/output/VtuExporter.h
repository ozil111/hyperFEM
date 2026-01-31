// system/output/VtuExporter.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <string>
#include "entt/entt.hpp"

struct DataContext;

/**
 * @brief VTU (VTK UnstructuredGrid) 输出辅助类
 * @details 按 docs/vtu_format.md 规格写出 .vtu 文件，供 HyperView 等后处理使用。
 * 几何与拓扑来自 mesh_components（Position、Connectivity、ElementType），
 * 节点/单元数据按 output_entity 上的 NodeOutput、ElementOutput 指定字段输出。
 */
class VtuExporter {
public:
    /**
     * @brief 将网格与当前结果写出为 VTU 文件
     * @param filepath 输出 .vtu 路径
     * @param data_context 含 registry 的 DataContext
     * @param output_entity 若有效，则从该实体读取 NodeOutput/ElementOutput，仅写出指定字段；否则写出默认字段（Displacement）
     * @return 成功返回 true，否则 false
     */
    static bool save(const std::string& filepath, const DataContext& data_context, entt::entity output_entity = entt::null);
};
