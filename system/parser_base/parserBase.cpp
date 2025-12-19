// parserBase.cpp - 声明式架构实现
#include "parser_base/parserBase.h"
#include "parser_base/string_utils.h"
#include "components/mesh_components.h"
#include "spdlog/spdlog.h"
#include <fstream>
#include <stdexcept>

// ============================================================================
// FemParser::parse - 调度器实现（极简版）
// ============================================================================
bool FemParser::parse(const std::string& filepath, DataContext& data_context) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("FemParser could not open file: {}", filepath);
        return false;
    }

    spdlog::info("FemParser started for file: {}", filepath);

    // 确保从一个干净的状态开始
    data_context.clear();

    // ID映射表 - 用于处理实体间的引用关系
    std::unordered_map<int, entt::entity> node_id_map;
    std::unordered_map<int, entt::entity> element_id_map;

    // 1. 创建并配置所有块处理器
    std::unordered_map<std::string, std::unique_ptr<Parser::IBlockHandler>> handlers;
    configure_handlers(handlers, data_context.registry, node_id_map, element_id_map);

    spdlog::debug("Configured {} block handlers", handlers.size());

    // 2. 简洁的调度循环
    std::string line;
    try {
        while (std::getline(file, line)) {
            preprocess_line(line);
            if (line.empty()) continue;

            // 查找匹配的处理器
            auto it = handlers.find(line);
            if (it != handlers.end()) {
                // 找到了匹配的处理器，将文件流交给它处理
                spdlog::debug("Dispatching to handler for '{}'", line);
                it->second->process(file);
            } else {
                // 未知关键字，可以选择忽略或警告
                // spdlog::debug("FemParser skipping unrecognized keyword line: {}", line);
            }
        }
    } catch (const std::exception& e) {
        spdlog::error("A critical parsing error occurred: {}", e.what());
        return false;
    }

    // Count entities using views
    auto node_count = data_context.registry.view<Component::Position>().size();
    auto element_count = data_context.registry.view<Component::Connectivity>().size();
    auto set_count = data_context.registry.view<Component::SetName>().size();
    
    spdlog::info("FemParser finished. Total nodes: {}, elements: {}, sets: {}", 
                 node_count, element_count, set_count);
    return true;
}

// ============================================================================
// FemParser::configure_handlers - 声明式配置的核心
// ============================================================================
void FemParser::configure_handlers(
    std::unordered_map<std::string, std::unique_ptr<Parser::IBlockHandler>>& handlers,
    entt::registry& registry,
    std::unordered_map<int, entt::entity>& node_id_map,
    std::unordered_map<int, entt::entity>& element_id_map
) {
    // ========================================================================
    // 配置 Node 解析器
    // ========================================================================
    // 语法: 101, 1.0, 2.0, 3.0
    // 字段: int(ID), double(x), double(y), double(z)
    auto node_handler = std::make_unique<Parser::BlockHandler>(registry);
    
    // 字段0 -> OriginalID
    node_handler->map<Component::OriginalID>(0, [](auto& comp, const auto& f) {
        comp.value = std::get<int>(f);
    });
    
    // 字段1 -> Position.x
    node_handler->map<Component::Position>(1, [](auto& comp, const auto& f) {
        comp.x = std::get<double>(f);
    });
    
    // 字段2 -> Position.y
    node_handler->map<Component::Position>(2, [](auto& comp, const auto& f) {
        comp.y = std::get<double>(f);
    });
    
    // 字段3 -> Position.z
    node_handler->map<Component::Position>(3, [](auto& comp, const auto& f) {
        comp.z = std::get<double>(f);
    });
    
    // 后处理钩子：建立 NodeID -> entity 映射表
    node_handler->set_post_entity_hook([&](entt::entity entity, const auto& fields) {
        int node_id = std::get<int>(fields[0]);
        node_id_map[node_id] = entity;
    });
    
    handlers["*node begin"] = std::move(node_handler);
    
    spdlog::debug("Configured Node handler with declarative field mappings");

    // ========================================================================
    // 配置 Element 解析器
    // ========================================================================
    // 语法: 201, 308, [101, 102, ...]
    // 字段: int(ID), int(Type), vector<int>(NodeIDs)
    // 注意: Element需要特殊处理器，因为它依赖node_id_map
    auto element_handler = std::make_unique<Parser::ElementBlockHandler>(
        registry, node_id_map, element_id_map
    );
    
    // element_id_map 会在 element_handler 处理过程中自动填充
    // 供后续的 element set 解析器使用
    
    handlers["*element begin"] = std::move(element_handler);
    
    spdlog::debug("Configured Element handler");

    // ========================================================================
    // 配置 NodeSet 解析器
    // ========================================================================
    // 语法: 1, fix, [3, 4, 7, 8]
    // 字段: int(ID), string(Name), vector<int>(MemberIDs)
    auto nodeset_handler = std::make_unique<Parser::SetBlockHandler>(
        registry, node_id_map, true  // true表示这是node set
    );
    
    handlers["*nodeset begin"] = std::move(nodeset_handler);
    
    spdlog::debug("Configured NodeSet handler");

    // ========================================================================
    // 配置 ElementSet 解析器
    // ========================================================================
    // 语法: 1, body1, [201, 202, ...]
    // 字段: int(ID), string(Name), vector<int>(MemberIDs)
    // 注意: element_id_map需要在element块处理后构建
    // 我们使用一个延迟构建的策略
    auto eleset_handler = std::make_unique<Parser::SetBlockHandler>(
        registry, element_id_map, false  // false表示这是element set
    );
    
    // 添加一个前置钩子来构建 element_id_map
    // 由于我们的架构没有前置钩子，我们在这里手动构建映射
    // 但这要求 *element begin 必须在 *eleset begin 之前
    
    handlers["*eleset begin"] = std::move(eleset_handler);
    
    spdlog::debug("Configured ElementSet handler");
    
    // ========================================================================
    // 未来扩展示例：如果要添加新的 Temperature 组件
    // ========================================================================
    // 只需在 node_handler 配置中添加一行：
    // node_handler->map<Component::Temperature>(4, [](auto& comp, const auto& f) {
    //     comp.value = std::get<double>(f);
    // });
    // 
    // 并确保文件语法更新为: 101, 1.0, 2.0, 3.0, 300.5
    // 
    // 就这么简单！无需修改任何循环或解析逻辑！
}