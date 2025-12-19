// node_parser.h
#pragma once
#include "entt/entt.hpp"
#include "components/mesh_components.h"
#include <fstream>
#include <unordered_map>

// 我们将这个解析器封装在一个命名空间中，以避免函数名冲突
namespace NodeParser {

/**
 * @brief 解析文件流中的节点数据块 (*node begin ... *node end)
 * @param file [in, out] 文件输入流，函数将从当前位置读取
 * @param registry [out] EnTT registry，将创建节点实体并附加组件
 * @param node_id_map [out] 从原始NodeID到entity的映射表，供ElementParser使用
 */
void parse(std::ifstream& file, entt::registry& registry, std::unordered_map<NodeID, entt::entity>& node_id_map);

} // namespace NodeParser