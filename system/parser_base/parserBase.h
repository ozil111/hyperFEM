
// parserBase.h
#pragma once
#include "mesh.h"
#include <string>

/**
 * @class FemParser
 * @brief FEM输入文件的总解析器 (Facade)
 * * 提供一个单一的静态方法来解析整个文件。
 * 它负责打开/关闭文件，并根据关键字将解析任务分派给专门的子解析器。
 */
class FemParser {
public:
    /**
     * @brief 解析指定的输入文件并填充Mesh对象
     * @param filepath 要解析的文件的路径
     * @param mesh [out] 将被解析数据填充的Mesh对象
     * @return true 如果解析成功, false 如果文件无法打开或发生严重错误
     */
    static bool parse(const std::string& filepath, Mesh& mesh);
};