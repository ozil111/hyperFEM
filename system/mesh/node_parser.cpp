// node_parser.cpp
#include "mesh/node_parser.h"
#include "spdlog/spdlog.h"
#include "parser_base/string_utils.h"
#include <iostream>
#include <sstream>
#include <string>


namespace NodeParser {

void parse(std::ifstream& file, Mesh& mesh) {
    // --- 优化点：添加预扫描和内存预留 ---
    std::streampos start_pos = file.tellg(); // 记录当前文件位置
    size_t line_count = 0;
    std::string line;

    while (std::getline(file, line)) {
        trim(line);
        if (line.find("*node end") != std::string::npos) break; // 找到结尾
        if (line.empty() || line[0] == '#') continue;
        line_count++;
    }

    // 预留空间
    mesh.node_coordinates.reserve(mesh.node_coordinates.size() + line_count * 3);
    mesh.node_index_to_id.reserve(mesh.node_index_to_id.size() + line_count);

    // 回到块的开始位置，进行真正的解析
    file.clear(); // 清除eof等状态位
    file.seekg(start_pos);
    // --- 优化结束 ---

    size_t current_index = mesh.get_node_count();
    spdlog::debug("--> Entering NodeParser... (pre-reserved for {} nodes)", line_count);

    while (std::getline(file, line)) {
        trim(line);

        if (line.find("*node end") != std::string::npos) {
            spdlog::debug("<-- Exiting NodeParser.");
            return;
        }
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::stringstream ss(line);
        std::string segment;
        NodeID id;
        double x, y, z;

        try {
            std::getline(ss, segment, ','); id = std::stoi(segment);
            std::getline(ss, segment, ','); x = std::stod(segment);
            std::getline(ss, segment, ','); y = std::stod(segment);
            std::getline(ss, segment, ','); z = std::stod(segment);
        } catch (const std::exception& e) {
            spdlog::warn("NodeParser skipping malformed line: {}", line);
            continue;
        }

        if (mesh.node_id_to_index.count(id)) {
            spdlog::warn("Duplicate node ID {}. Skipping.", id);
            continue;
        }

        mesh.node_coordinates.push_back(x);
        mesh.node_coordinates.push_back(y);
        mesh.node_coordinates.push_back(z);
        mesh.node_id_to_index[id] = current_index;
        mesh.node_index_to_id.push_back(id);
        current_index++;
    }
    
    throw std::runtime_error("Parser error: Reached end of file without finding *node end.");
}

} // namespace NodeParser