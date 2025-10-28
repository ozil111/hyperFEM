#pragma once

#include <string>
#include <algorithm>
#include <cctype> // for std::isspace
#include <fstream> // for std::ifstream

/**
 * @brief 移除字符串两端的空白符
 * @param s 要处理的字符串引用
 */
inline void trim(std::string& s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), s.end());
}

/**
 * @brief [新增] 统一的行预处理函数
 * * 1. 移除从 '#' 开始的注释
 * * 2. 移除处理后字符串两端的空白
 * @param line 要处理的字符串引用
 */
inline void preprocess_line(std::string& line) {
    // 1. 查找注释字符 '#'
    size_t comment_pos = line.find('#');

    // 2. 如果找到，则删除从该位置到字符串末尾的部分
    if (comment_pos != std::string::npos) {
        line.erase(comment_pos);
    }

    // 3. 对结果进行 trim 操作
    trim(line);
}

/**
 * @brief 从文件流中读取一个完整的"逻辑行".
 * 通过查找行尾的逗号来处理行连续。
 * @param file [in, out] 输入文件流.
 * @param logical_line [out] 存储最终合并后的逻辑行.
 * @return bool 如果成功读取到内容则返回 true, 否则返回 false (文件结束).
 */
inline bool get_logical_line(std::ifstream& file, std::string& logical_line) {
    logical_line.clear();
    std::string physical_line;
    bool is_first_line = true;

    while (std::getline(file, physical_line)) {
        preprocess_line(physical_line); // 统一预处理

        if (physical_line.empty()) {
            if (is_first_line) {
                // 如果逻辑行的第一行就是空的，继续找下一行
                continue;
            } else {
                // 如果是在拼接过程中遇到空行，可以视为连续符错误或直接忽略
                // 这里我们选择忽略它，继续找下一行
                continue;
            }
        }
        
        is_first_line = false;

        if (physical_line.back() == ',') {
            // 发现行连续符
            physical_line.pop_back(); // 移除末尾的逗号
            trim(physical_line);      // 再次 trim 以防 "1, 2, 3 , " 这种情况
            logical_line += physical_line + ","; // 拼接并保留一个逗号作为分隔
        } else {
            // 没有行连续符，这是逻辑行的最后一部分
            logical_line += physical_line;
            return true; // 成功读取一个完整的逻辑行
        }
    }

    // 文件结束，但 logical_line 中可能还有内容
    return !logical_line.empty();
}
