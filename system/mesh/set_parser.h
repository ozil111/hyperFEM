// system/mesh/set_parser.h
#pragma once
#include "entt/entt.hpp"
#include "components/mesh_components.h"
#include <fstream>
#include <unordered_map>

// Encapsulate the set parsers in their own namespace
namespace SetParser {

/**
 * @brief Parses the node set data block (*nodeset begin ... *nodeset end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param registry [out] EnTT registry，将创建节点集实体并附加组件
 * @param node_id_map [in] 从原始NodeID到entity的映射表
 */
void parse_node_sets(std::ifstream& file, entt::registry& registry, const std::unordered_map<NodeID, entt::entity>& node_id_map);

/**
 * @brief Parses the element set data block (*eleset begin ... *eleset end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param registry [out] EnTT registry，将创建单元集实体并附加组件
 */
void parse_element_sets(std::ifstream& file, entt::registry& registry);

} // namespace SetParser
