// GenericLineParser.h
/**
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) 2025 hyperFEM. All rights reserved.
 * Author: Xiaotong Wang (or hyperFEM Team)
 */
#pragma once

#include <string>
#include <vector>
#include <variant>
#include <charconv> // C++17, for safe, non-throwing numeric conversion
#include <sstream>
#include <stdexcept>
#include "spdlog/spdlog.h"
#include "string_utils.h" // 引入 trim

namespace Parser {

// 代表一个解析后的字段，可以是多种类型
using Field = std::variant<int, double, std::string, std::vector<int>>;

/**
 * @brief 辅助函数，用于判断字符串是否可以被完整地解析为整数
 */
inline bool is_strictly_integer(const std::string& s) {
    if (s.empty()) return false;
    int result;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    // 只有当没有错误且整个字符串都被消耗掉时，才认为是严格的整数
    return ec == std::errc() && ptr == s.data() + s.size();
}

/**
 * @brief 辅助函数，用于判断字符串是否可以被完整地解析为浮点数
 */
inline bool is_strictly_double(const std::string& s) {
    if (s.empty()) return false;
    double result;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), result);
    return ec == std::errc() && ptr == s.data() + s.size();
}

/**
 * @brief 解析包含 "[]" 语法的行
 * @details 新语法规范:
 *   - 普通字段用逗号分隔: "101, 1.0, 2.0, 3.0"
 *   - 数组字段用方括号包围: "1, fix, [3, 4, 7, 8]"
 *   - 自动识别类型: int, double, string, vector<int>
 * @param line 要解析的字符串行
 * @return 解析后的字段列表
 */
inline std::vector<Field> parse_line_to_fields(const std::string& line) {
    std::vector<Field> fields;
    std::string current_segment;
    bool in_array = false;
    std::stringstream line_ss(line); // 使用 stringstream 更稳健地处理逗号

    while (std::getline(line_ss, current_segment, ',')) {
        // 检查并处理数组逻辑
        trim(current_segment);

        if (current_segment.find('[') != std::string::npos) {
            in_array = true;
            std::string array_content = current_segment.substr(1); // 移除'['

            while (in_array && current_segment.find(']') == std::string::npos) {
                if (!std::getline(line_ss, current_segment, ',')) break; // 文件或行结束
                array_content += "," + current_segment;
            }

            if (auto end_pos = array_content.find(']'); end_pos != std::string::npos) {
                array_content.resize(end_pos); // 移除']'及其之后的内容
            }

            in_array = false;

            // 解析数组内容
            std::vector<int> int_vector;
            std::stringstream array_ss(array_content);
            std::string item;
            while (std::getline(array_ss, item, ',')) {
                trim(item);
                if (!item.empty()) {
                    int_vector.push_back(std::stoi(item));
                }
            }
            fields.push_back(int_vector);
        } else { // 非数组字段
            trim(current_segment);
            if (current_segment.empty()) continue;

            // --- 使用新的严格检查 ---
            if (is_strictly_integer(current_segment)) {
                fields.push_back(std::stoi(current_segment));
            } else if (is_strictly_double(current_segment)) {
                fields.push_back(std::stod(current_segment));
            } else {
                fields.push_back(current_segment); // 否则，就是字符串
            }
        }
    }

    return fields;
}

} // namespace Parser

