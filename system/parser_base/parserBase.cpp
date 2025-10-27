// parserBase.cpp
#include "parser_base/parserBase.h"
#include "mesh/node_parser.h" // 引入节点解析器的头文件
// #include "element_parser.h" // 当您添加单元解析器时，在这里包含它
#include "parser_base/string_utils.h" // 使用统一的trim函数
#include "spdlog/spdlog.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

bool FemParser::parse(const std::string& filepath, Mesh& mesh) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        spdlog::error("FemParser could not open file: {}", filepath);
        return false;
    }

    spdlog::info("FemParser started for file: {}", filepath);

    // 确保从一个干净的状态开始
    mesh.clear();

    std::string line;
    try {
        while (std::getline(file, line)) {
            trim(line);
            if (line.empty() || line[0] == '#') {
                continue;
            }

            // --- 总指挥/调度逻辑 ---
            if (line.find("*node begin") != std::string::npos) {
                // 任务分发给 NodeParser
                NodeParser::parse(file, mesh);
            } 
            // else if (line.find("*element begin") != std::string::npos) {
            //     // 未来：任务分发给 ElementParser
            //     ElementParser::parse(file, mesh);
            // }
            else {
                // 可以选择忽略未知关键字或发出警告
                // spdlog::debug("FemParser skipping unrecognized keyword line: {}", line);
            }
        }
    } catch (const std::runtime_error& e) {
        spdlog::error("A critical parsing error occurred: {}", e.what());
        return false; // 报告严重错误
    }

    spdlog::info("FemParser finished. Total nodes loaded: {}", mesh.get_node_count());
    return true;
}