// ElementRegistry.h
#pragma once

#include <string>
#include <unordered_map>
#include <stdexcept>

// 单元类型的固定属性
struct ElementProperties {
    int numNodes;
    int dimension;
    std::string name;
};

// 使用类和静态成员实现单例模式，确保注册表全局唯一
class ElementRegistry {
public:
    // 获取全局唯一的注册表实例
    static ElementRegistry& getInstance() {
        static ElementRegistry instance; // C++11保证线程安全初始化
        return instance;
    }

    // 根据类型ID获取单元属性
    const ElementProperties& getProperties(int typeId) const {
        auto it = propertiesMap.find(typeId);
        if (it == propertiesMap.end()) {
            throw std::runtime_error("Unknown element type ID: " + std::to_string(typeId));
        }
        return it->second;
    }

private:
    // 私有构造函数，防止外部创建实例
    ElementRegistry() {
        initialize();
    }

    // 初始化函数，填充所有支持的单元类型
    void initialize() {
        propertiesMap[102] = {2, 1, "Line2"};
        propertiesMap[103] = {3, 1, "Line3"};
        propertiesMap[203] = {3, 2, "Triangle3"};
        propertiesMap[204] = {4, 2, "Quad4"};
        propertiesMap[208] = {8, 2, "Quad8"};
        propertiesMap[304] = {4, 3, "Tetra4"};
        propertiesMap[306] = {6, 3, "Penta6"}; // 注意：306通常是三棱柱(Wedge/Penta)，不是金字塔
        propertiesMap[308] = {8, 3, "Hexa8"};
        propertiesMap[310] = {10, 3, "Tetra10"};
        propertiesMap[320] = {20, 3, "Hexa20"};
    }

    // 禁止拷贝和赋值
    ElementRegistry(const ElementRegistry&) = delete;
    ElementRegistry& operator=(const ElementRegistry&) = delete;

    std::unordered_map<int, ElementProperties> propertiesMap;
};
