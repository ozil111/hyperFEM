// parserBase.cpp
#include "parser_base/parserBase.h"
#include "mesh/node_parser.h" // 引入节点解析器的头文件
// #include "element_parser.h" // 当您添加单元解析器时，在这里包含它
#include "spdlog/spdlog.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <cctype>

// 辅助函数：移除字符串两端的空白符 (也可以放在一个公共的utils文件中)
static void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

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