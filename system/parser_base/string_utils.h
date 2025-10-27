#pragma once

#include <string>
#include <algorithm>
#include <cctype> // for std::isspace

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
