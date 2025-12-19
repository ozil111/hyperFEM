// element_parser.h
#pragma once
#include "entt/entt.hpp"
#include "components/mesh_components.h"
#include <fstream>
#include <unordered_map>

// Encapsulate the element parser in its own namespace for clarity
namespace ElementParser {

/**
 * @brief Parses the element data block (*element begin ... *element end) from a file stream.
 * @param file [in, out] The input file stream, positioned at the start of the block.
 * @param registry [out] EnTT registry，将创建单元实体并附加组件
 * @param node_id_map [in] 从原始NodeID到entity的映射表，用于构建Connectivity
 */
void parse(std::ifstream& file, entt::registry& registry, const std::unordered_map<NodeID, entt::entity>& node_id_map);

} // namespace ElementParser
