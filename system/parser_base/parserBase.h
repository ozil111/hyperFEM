/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
// parserBase.h
#pragma once
#include "DataContext.h"
#include "BlockHandler.h" // 引入新的声明式处理器
#include <string>
#include <memory>
#include <unordered_map>

/**
 * @class FemParser
 * @brief FEM输入文件的总解析器 - 声明式架构 (Facade & Dispatcher)
 * @details 从硬编码解析器重构为声明式调度器：
 *   1. 不再包含具体的解析逻辑
 *   2. 负责配置所有BlockHandler
 *   3. 根据关键字将文件流分派给对应的处理器
 * 
 * 优势：
 *   - 添加新组件只需在configure_handlers中声明映射
 *   - 解析逻辑高度集中，易于维护
 *   - 符合EnTT数据驱动的设计哲学
 */
class FemParser {
public:
    /**
     * @brief 解析指定的输入文件并填充DataContext的registry
     * @param filepath 要解析的文件的路径
     * @param data_context [out] 将被解析数据填充的DataContext对象
     * @return true 如果解析成功, false 如果文件无法打开或发生严重错误
     */
    static bool parse(const std::string& filepath, DataContext& data_context);

private:
    /**
     * @brief 配置所有支持的块处理器 - 声明式架构的核心
     * @details 在这里"声明"所有解析规则，无需编写循环和解析逻辑
     * @param handlers [out] 存储关键字到处理器的映射
     * @param registry [in] EnTT registry
     * @param node_id_map [in,out] NodeID到entity的映射，供后续处理器使用
     * @param element_id_map [in,out] ElementID到entity的映射，供后续处理器使用
     */
    static void configure_handlers(
        std::unordered_map<std::string, std::unique_ptr<Parser::IBlockHandler>>& handlers,
        entt::registry& registry,
        std::unordered_map<int, entt::entity>& node_id_map,
        std::unordered_map<int, entt::entity>& element_id_map
    );
};