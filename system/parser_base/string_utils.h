#pragma once

#include <string>
#include <algorithm>

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
