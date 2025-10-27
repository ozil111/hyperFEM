// node_parser.h
#pragma once
#include "mesh.h"
#include <fstream>

// 我们将这个解析器封装在一个命名空间中，以避免函数名冲突
namespace NodeParser {

/**
 * @brief 解析文件流中的节点数据块 (*node begin ... *node end)
 * @param file [in, out] 文件输入流，函数将从当前位置读取
 * @param mesh [out] 要填充的Mesh数据对象
 */
void parse(std::ifstream& file, Mesh& mesh);

} // namespace NodeParser