# 声明式解析器架构说明

## 架构概览

hyperFEM v1.0 引入了全新的**声明式解析器架构**，将解析器从硬编码的过程式系统转变为配置驱动的声明式框架。

### 核心理念

> "告诉解析器数据如何映射，而不是如何解析"

传统解析器需要为每种数据类型编写专门的循环、类型转换和错误处理代码。声明式解析器将这些通用逻辑提取到核心框架中，用户只需**声明**字段到组件的映射关系。

---

## 架构对比

### 旧架构：硬编码解析

```cpp
// NodeParser.cpp (~100行)
void parse(std::ifstream& file, ...) {
    while (get_line(file, line)) {
        std::stringstream ss(line);
        std::getline(ss, segment, ','); 
        int id = std::stoi(segment);
        std::getline(ss, segment, ','); 
        double x = std::stod(segment);
        // ... 重复的解析逻辑
        
        auto entity = registry.create();
        registry.emplace<Component::OriginalID>(entity, id);
        registry.emplace<Component::Position>(entity, x, y, z);
    }
}

// ElementParser.cpp (~100行，类似逻辑)
// SetParser.cpp (~150行，类似逻辑)
```

**问题**：
- 为每种块类型重复实现解析逻辑
- 添加新组件需要修改多个文件
- 错误处理分散在各处

### 新架构：声明式配置

```cpp
// parserBase.cpp 中的 configure_handlers() - 集中配置
void configure_handlers(...) {
    // Node 处理器
    auto node_handler = std::make_unique<Parser::BlockHandler>(registry);
    node_handler->map<Component::OriginalID>(0, [](auto& c, auto& f) { 
        c.value = std::get<int>(f); 
    });
    node_handler->map<Component::Position>(1, [](auto& c, auto& f) { 
        c.x = std::get<double>(f); 
    });
    node_handler->map<Component::Position>(2, [](auto& c, auto& f) { 
        c.y = std::get<double>(f); 
    });
    node_handler->map<Component::Position>(3, [](auto& c, auto& f) { 
        c.z = std::get<double>(f); 
    });
    handlers["*node begin"] = std::move(node_handler);
    
    // Element, Set 等处理器配置...
}

// FemParser::parse - 极简调度
while (std::getline(file, line)) {
    auto it = handlers.find(line);
    if (it != handlers.end()) {
        it->second->process(file);  // 框架自动处理一切
    }
}
```

**优势**：
- 所有映射规则集中在一处
- 添加新组件只需添加一行配置
- 通用的解析和错误处理逻辑

---

## 三层架构

### 第一层：GenericLineParser（通用行解析器）

**职责**：将文本行解析为类型化的字段

```cpp
// 输入
"1, 308, [5, 6, 8, 7, 1, 2, 4, 3]"

// 输出
std::vector<Field>{
    int(1),
    int(308),
    std::vector<int>{5, 6, 8, 7, 1, 2, 4, 3}
}
```

**类型识别规则**：
- 包含小数点 → `double`
- 纯数字 → `int`
- 在 `[]` 中 → `vector<int>`
- 其他 → `string`

**核心代码**：
```cpp
// system/parser_base/GenericLineParser.h
std::vector<Field> parse_line_to_fields(const std::string& line);
```

### 第二层：BlockHandler（块处理器）

**职责**：管理字段到组件的声明式映射

```cpp
class BlockHandler {
    // 声明映射
    template<typename ComponentType>
    void map(int field_index, 
             std::function<void(ComponentType&, const Field&)> setter);
    
    // 自动处理整个块
    void process(std::ifstream& file) override {
        while (get_logical_line(file, line)) {
            auto fields = parse_line_to_fields(line);
            auto entity = registry.create();
            
            // 应用所有已声明的映射
            for (auto& [index, mapper] : field_mappers) {
                mapper(registry, entity, fields[index]);
            }
        }
    }
};
```

**工作流程**：
1. 读取一行 → 解析为 `vector<Field>`
2. 创建实体
3. 遍历所有已配置的映射，逐个应用

### 第三层：FemParser（调度器）

**职责**：配置处理器并分派任务

```cpp
bool FemParser::parse(...) {
    // 1. 配置所有处理器
    std::unordered_map<string, unique_ptr<IBlockHandler>> handlers;
    configure_handlers(handlers, registry, node_id_map, element_id_map);
    
    // 2. 极简调度循环
    while (std::getline(file, line)) {
        auto it = handlers.find(line);
        if (it != handlers.end()) {
            it->second->process(file);
        }
    }
}
```

---

## 新的文件语法：[] 数组表示

### 语法规范

使用 `[]` 包围可变长度的数组字段：

```
*element begin
# element_id, element_type, [node_ids]
1, 308, [5, 6, 8, 7, 1, 2, 4, 3]
2, 304, [1, 2, 3, 4]
*element end

*nodeset begin
# set_id, set_name, [member_node_ids]
0, fix, [3, 4, 7, 8]
1, load, [1, 2, 5, 6]
*nodeset end
```

### 为什么需要 [] 语法？

**问题**：旧格式的字段数不固定
```
1, 308, 5, 6, 8, 7, 1, 2, 4, 3  # 10个字段
2, 304, 1, 2, 3, 4              # 6个字段
```
- 无法声明式地描述"第N个字段是XX类型"
- 解析器必须硬编码"前两个字段后，剩下的都是节点ID"

**解决**：固定字段结构
```
1, 308, [5, 6, 8, 7, 1, 2, 4, 3]  # 3个字段
2, 304, [1, 2, 3, 4]              # 3个字段
```
- 字段0：Element ID (int)
- 字段1：Element Type (int)
- 字段2：Node IDs (vector<int>)

现在每行的**结构固定**，可以声明式地处理。

---

## 扩展性演示：添加温度组件

假设要为节点添加温度属性。

### Step 1：定义组件
```cpp
// data_center/components/physics_components.h
namespace Component {
    struct Temperature { 
        double value; 
    };
}
```

### Step 2：更新文件格式
```
*node begin
# node_id, x, y, z, temperature
1, 1.0, 1.0, 1.0, 300.5
2, 1.0, 0.0, 1.0, 298.3
*node end
```

### Step 3：添加一行配置
```cpp
// system/parser_base/parserBase.cpp
void FemParser::configure_handlers(...) {
    auto node_handler = std::make_unique<Parser::BlockHandler>(registry);
    // ... 现有的映射 ...
    
    // 新增：温度组件映射
    node_handler->map<Component::Temperature>(4, [](auto& comp, const auto& f) {
        comp.value = std::get<double>(f);
    });
    
    handlers["*node begin"] = std::move(node_handler);
}
```

### 完成！

无需：
- ❌ 修改 NodeParser 的循环逻辑
- ❌ 添加新的 try-catch 块
- ❌ 更新多个文件

只需：
- ✅ 定义组件
- ✅ 添加一行配置

---

## 专用处理器

对于需要特殊处理的块（如单元、集合），提供专用处理器：

### ElementBlockHandler

处理单元块的特殊需求：
- 节点连接性需要从 NodeID 转换为 `entt::entity`
- 需要填充 `element_id_map` 供后续使用

```cpp
class ElementBlockHandler : public IBlockHandler {
public:
    ElementBlockHandler(
        entt::registry& reg,
        std::unordered_map<int, entt::entity>& node_map,
        std::unordered_map<int, entt::entity>& element_map
    );
    
    void process(std::ifstream& file) override;
};
```

### SetBlockHandler

处理集合块的特殊需求：
- 成员ID需要转换为 `entt::entity`
- 支持节点集和单元集

```cpp
class SetBlockHandler : public IBlockHandler {
public:
    SetBlockHandler(
        entt::registry& reg,
        std::unordered_map<int, entt::entity>& id_map,
        bool is_node_set
    );
    
    void process(std::ifstream& file) override;
};
```

---

## 性能特性

### 解析性能

| 方面 | 旧架构 | 新架构 |
|------|--------|--------|
| **字符串解析** | 手动 `std::getline` | 统一的 `parse_line_to_fields` |
| **类型转换** | 分散的 `stoi/stod` | 集中的 `std::variant` 处理 |
| **错误处理** | 重复的 try-catch | 统一的异常处理 |
| **内存分配** | 相似 | 相似（`vector<Field>` 临时对象）|

**结论**：性能基本相当，但代码更清晰、可维护。

### 编译时开销

新架构使用模板和 lambda，可能增加编译时间。实测影响很小（< 1秒）。

---

## 向后兼容性

### 旧格式文件

当前版本**不支持**旧格式（无 `[]` 的文件）。

如需迁移：
```bash
# 使用旧版本读取旧文件并导出
hyperFEM_app_v0.9 -i old_format.xfem -o new_format.xfem
```

### 未来计划

可以添加"遗留模式"处理器支持旧格式，但不建议这样做，因为会增加维护负担。

---

## 代码结构

```
system/parser_base/
├── GenericLineParser.h     # 通用行解析器
├── BlockHandler.h          # 声明式块处理器
├── parserBase.h            # 调度器接口
└── parserBase.cpp          # 调度器实现 + 配置中心

system/mesh/
├── element_exporter.cpp    # 单元导出（新格式）
├── set_exporter.cpp        # 集合导出（新格式）
└── ... (其他导出器)
```

---

## 测试与验证

### Roundtrip 测试

完整的读取-导出-再读取循环：

```bash
# 读取新格式
hyperFEM_app -i test_new_syntax.xfem -o roundtrip_v2.xfem

# 再次读取导出的文件
hyperFEM_app -i roundtrip_v2.xfem -o roundtrip_v3.xfem

# 比较两次导出结果
diff roundtrip_v2.xfem roundtrip_v3.xfem  # 应该完全一致
```

### 测试结果

✅ **通过** - v2 和 v3 文件完全一致，数据完整性验证成功

---

## 架构优势总结

### 1. 极高的灵活性
添加新组件从"修改多个文件"变为"添加一行配置"

### 2. 卓越的可维护性
所有解析规则集中在 `configure_handlers()` 一处，一目了然

### 3. 统一的错误处理
不再需要在每个解析器中重复实现 try-catch 逻辑

### 4. 类型安全
`std::variant` + lambda 类型检查，编译期发现错误

### 5. 可扩展性
新的块类型只需实现 `IBlockHandler` 接口

### 6. 符合 EnTT 哲学
数据驱动、组件化，与 ECS 架构完美契合

---

## 设计模式

本架构运用的设计模式：

1. **策略模式** (Strategy)
   - `IBlockHandler` 接口
   - 不同的处理器策略

2. **工厂模式** (Factory)
   - `configure_handlers()` 创建处理器实例

3. **命令模式** (Command)
   - 字段映射 lambda 封装操作

4. **外观模式** (Facade)
   - `FemParser` 对外提供简单接口

5. **模板方法模式** (Template Method)
   - `BlockHandler::process()` 定义算法骨架

---

## 未来展望

### 可能的扩展

1. **验证器链** (Validation Chain)
   ```cpp
   node_handler->map<Component::Position>(1, ...)
                ->validate([](auto& c){ return c.x >= 0; })
                ->on_error("X coordinate must be non-negative");
   ```

2. **默认值支持**
   ```cpp
   node_handler->map<Component::Temperature>(4, ...)
                ->default_value(298.15);
   ```

3. **单位转换**
   ```cpp
   node_handler->map<Component::Position>(1, ...)
                ->convert_units(Unit::MM, Unit::M);
   ```

4. **条件映射**
   ```cpp
   node_handler->map_if<Component::Velocity>(...)
                ->when([](auto& fields){ return fields.size() > 4; });
   ```

---

**文档版本**: v1.0  
**最后更新**: 2025-11-03  
**架构负责人**: xiaotong wang

