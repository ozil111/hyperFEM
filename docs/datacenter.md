# hyperFEM 数据中心架构（EnTT ECS）

## 概述

hyperFEM 采用 **EnTT Entity-Component-System (ECS)** 架构来管理所有网格数据，并使用**声明式解析器**进行文件I/O。这是一种现代化的、高性能的、可扩展的数据组织方式。

### 核心架构创新

1. **数据层**: EnTT ECS - 数据驱动的组件系统
2. **解析层**: 声明式解析器 - 从硬编码到配置驱动
3. **文件格式**: 支持 `[]` 语法的可变长度数组表示

### 核心概念

- **Entity（实体）**: 唯一的ID句柄，代表一个节点、单元或集合
- **Component（组件）**: 附加到实体上的纯数据结构
- **System（系统）**: 操作组件数据的无状态函数
- **Registry（注册表）**: 管理所有实体和组件的中央数据库

### 数据所有权

```cpp
DataContext data;                  // 应用层数据中心
entt::registry& registry = data.registry;  // 唯一的"事实之源"
```

所有网格数据存储在 `entt::registry` 中，通过组件化的方式组织。

### EnTT 配置

```cmake
# CMakeLists.txt 中配置64位实体ID
add_compile_definitions(ENTT_ID_TYPE=std::uint64_t)
```

- **默认32位ID**: 最多支持 ~100万个实体（2^20）
- **64位ID（已配置）**: 支持 ~43亿个实体（2^32），适合大规模FEM模型
- **内存开销**: 每个实体句柄从4字节增加到8字节，对大模型而言可忽略不计

### 架构对比总结

| 方面 | 旧架构（Mesh结构体） | 新架构（EnTT ECS） |
|------|---------------------|-------------------|
| **数据存储** | SoA数组 + 映射表 | 实体 + 组件 |
| **数据访问** | 通过索引或ID查找 | 直接实体句柄，O(1)访问 |
| **关系表达** | 存储ID，需要查找 | 直接存储entity，无查找 |
| **扩展性** | 修改结构体定义 | 添加新组件 |
| **内存效率** | 手动管理，易浪费 | 自动稀疏集，高效 |
| **并行化** | 需要手动管理 | 天然支持并行view |
| **类型安全** | 依赖注释和约定 | 类型系统强制 |

### ECS带来的优势

1. **直接引用**: `Connectivity` 直接存储 `entt::entity`，访问节点数据无需查找
2. **统一建模**: 节点、单元、集合都是实体，架构一致
3. **按需查询**: 使用 `registry.view<...>()` 按组件类型高效查询
4. **易于扩展**: 添加分析结果（应力、位移）只需定义新组件
5. **内存优化**: EnTT自动管理稀疏集，只为实际使用的组件分配内存

---

## Node 数据（节点）

### 旧架构（已废弃）
```cpp
// 旧的 Mesh 结构体方式
std::vector<double> node_coordinates;         // SoA格式
std::unordered_map<NodeID, size_t> node_id_to_index;
std::vector<NodeID> node_index_to_id;
```

### 新架构（ECS组件）

每个节点现在是一个 **Entity**，附加以下组件：

```cpp
// 几何位置组件
struct Component::Position {
    double x, y, z;
};

// 原始ID组件（用于导出和用户查询）
struct Component::OriginalID {
    int value;  // 输入文件中定义的节点ID
};
```

### 创建节点实体

```cpp
// 在解析器中创建节点
const entt::entity node_entity = registry.create();
registry.emplace<Component::Position>(node_entity, x, y, z);
registry.emplace<Component::OriginalID>(node_entity, node_id);

// 建立ID映射（临时，仅用于解析阶段）
node_id_map[node_id] = node_entity;
```

### 查询节点数据

```cpp
// 获取所有节点
auto view = registry.view<Component::Position, Component::OriginalID>();

// 遍历所有节点
for (auto entity : view) {
    const auto& pos = view.get<Component::Position>(entity);
    const auto& id = view.get<Component::OriginalID>(entity);
    
    // 使用 pos.x, pos.y, pos.z 和 id.value
}

// 统计节点数量
size_t node_count = view.size_hint();
```

### 性能特点

- **内存布局**: EnTT内部使用SoA（Structure of Arrays），保证缓存友好
- **访问速度**: O(1) 实体句柄查找，连续内存迭代
- **扩展性**: 可随时为节点添加新组件（如位移、速度等）

输入文件结构如下

```
*node begin
# node id, x, y, z
1, 1.0, 1.0, 1.0
2, 1.0, 0.0, 1.0
3, 1.0, 1.0, 0.0
4, 1.0, 0.0, 0.0
5, 0.0, 1.0, 1.0
6, 0.0, 0.0, 1.0
7, 0.0, 1.0, 0.0
8, 0.0, 0.0, 0.0
*node end
```

**注意**: 节点格式保持固定字段数，无需使用 `[]` 语法。

---

## Element 数据（单元）

### 旧架构（已废弃）
```cpp
// 旧的 Mesh 结构体方式
std::vector<int> element_types;
std::vector<int> element_connectivity;
std::vector<size_t> element_offsets;
std::unordered_map<ElementID, size_t> element_id_to_index;
std::vector<ElementID> element_index_to_id;
```

### 新架构（ECS组件）

每个单元现在是一个 **Entity**，附加以下组件：

```cpp
// 单元类型组件
struct Component::ElementType {
    int type_id;  // 例如: 308表示8节点六面体
};

// 节点连接性组件
struct Component::Connectivity {
    std::vector<entt::entity> nodes;  // 直接存储节点实体句柄！
};

// 原始ID组件
struct Component::OriginalID {
    int value;  // 输入文件中定义的单元ID
};
```

### 关键优势：直接实体引用

```cpp
// 连接性使用 entt::entity 而不是 NodeID
// 这意味着访问节点数据无需查找，直接O(1)访问！
const auto& connectivity = registry.get<Component::Connectivity>(element_entity);

for (entt::entity node_entity : connectivity.nodes) {
    // 直接获取节点坐标，无需ID查找
    const auto& pos = registry.get<Component::Position>(node_entity);
    // 使用 pos.x, pos.y, pos.z
}
```

### 创建单元实体

```cpp
// 在解析器中创建单元
const entt::entity element_entity = registry.create();
registry.emplace<Component::OriginalID>(element_entity, elem_id);
registry.emplace<Component::ElementType>(element_entity, type_id);

// 构建连接性（将NodeID转换为entity句柄）
auto& connectivity = registry.emplace<Component::Connectivity>(element_entity);
for (const auto& node_id : raw_node_ids) {
    connectivity.nodes.push_back(node_id_map.at(node_id));
}
```

### 查询单元数据

```cpp
// 获取所有单元
auto view = registry.view<Component::Connectivity, 
                          Component::ElementType, 
                          Component::OriginalID>();

// 遍历所有单元
for (auto entity : view) {
    const auto& conn = view.get<Component::Connectivity>(entity);
    const auto& type = view.get<Component::ElementType>(entity);
    const auto& id = view.get<Component::OriginalID>(entity);
}

// 统计单元数量
size_t element_count = view.size_hint();
```

### 单元类型定义

命名规则：采用三位数，首位是维度，第二三位是节点数，比如四节点壳就是204

| typeid | 单元类型       |
| ------ | -------------- |
| 102    | 普通梁单元     |
| 103    | 三节点梁单元   |
| 203    | 平面三角形单元 |
| 204    | 平面四边形单元 |
| 208    | 二阶平面四边形 |
| 304    | 四面体         |
| 306    | Penta          |
| 308    | 八节点六面体   |
| 310    | 二阶四面体     |
| 320    | 20节点六面体   |

### 新的声明式语法（v1.0+）

从 v1.0 开始，单元块采用 **`[]` 语法**表示可变长度的节点连接：

```
*element begin
# element id, element type, [node ids]
1, 308, [5, 6, 8, 7, 1, 2, 4, 3]
2, 304, [1, 2, 3, 4]
*element end
```

**关键优势**：
- 每行的字段结构固定且可预测
- 字段0: Element ID (int)
- 字段1: Element Type (int)
- 字段2: Node IDs (vector<int>)

**旧格式（已废弃）**：
```
*element begin
1, 308, 5, 6, 8, 7, 1, 2, 4, 3  # 字段数不固定，难以声明式处理
*element end
```

### ElementRegistry（单元属性查找）

```cpp
// 单例模式的单元注册表
const auto& props = ElementRegistry::getInstance().getProperties(type_id);
int num_nodes = props.numNodes;
int dimension = props.dimension;
std::string name = props.name;
```

### 解析器优化：缓存最后一次的查找结果

尽管如此，我们确实有一种几乎零成本的方法来解决每次读取单元都需要查找注册表的问题。这个方法利用了网格文件中一个非常常见的特性：**相同类型的单元通常会连续出现在文件中**。

例如，一个文件里可能有成千上万个`308`单元，然后是成千上万个`304`单元。

我们可以利用这个局部性原理，缓存上一次的查找结果。

**实现思路：** 在你的解析器类或函数中，维护两个静态或成员变量：

- `cachedTypeId`: 缓存的最后一个单元类型ID。
- `cachedProperties`: 指向该类型属性的指针或引用。


这种缓存策略的优势：

- **性能提升巨大**：假设文件中有100万个单元，但只有5种不同的类型，并且每种类型的单元都聚集在一起。那么，`elementRegistry.find()` 这个真正的哈希查找操作总共只会被执行**5次**，而不是100万次！后续的999,995次访问都只是一个简单的整数比较 (`typeId != cachedTypeId`)，这个开销可以忽略不计。
- **代码改动极小**：你只需要在原来的查找逻辑外加一个 `if` 判断和两个缓存变量，核心逻辑不变，保持了代码的清晰和可维护性。
- **完全兼容**：即使文件中的单元类型是完全随机、交错排列的，这个方法也不会出错，最坏情况下降级为每次都查找，和没有缓存时一样。

## **有限元单元库节点顺序规范**

### 1. 概述

本文档定义了自定义有限元单元库中各类单元的节点连接顺序。遵循统一的节点顺序对于正确构建单元面、保证拓扑关系查找的准确性以及后续计算（如计算单元法向）至关重要。

- **二维单元**：节点顺序通常遵循**逆时针**方向，以确保单元法线方向（根据右手定则）朝向屏幕外。
- **三维单元**：面的节点顺序同样遵循**逆时针**方向（从单元外部看），以确保面法线朝向单元外部。
- **二阶单元**：节点列表的顺序为：**先列出所有角点，再列出所有中点**。在进行拓扑分析（即查找共享面）时，仅使用角点来定义面。

------

### 2. 一维单元 (1D Elements)

一维单元没有“面”的概念，它们的连接性体现在共享节点上。

#### **单元类型 102: 2节点线性梁 (Linear Beam)**

```
1----------2
```

- **节点列表**: `[node_1, node_2]`

#### **单元类型 103: 3节点二阶梁 (Quadratic Beam)**

```
1-----3-----2
```

- **角点**: 1, 2
- **中点**: 3
- **节点列表**: `[node_1, node_2, node_3]`

------

### 3. 二维单元 (2D Elements)

二维单元的面即为其边。

#### **单元类型 203: 3节点线性三角形 (Linear Triangle)**

```
      3
     / \
    /   \
   1-----2
```

- **节点列表**: `[node_1, node_2, node_3]`
- **边 (Faces)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 1]`

#### **单元类型 204: 4节点线性四边形 (Linear Quadrilateral)**

```
   4-------3
   |       |
   |       |
   1-------2
```

- **节点列表**: `[node_1, node_2, node_3, node_4]`
- **边 (Faces)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 4]`
  - 边 4: `[4, 1]`

#### **单元类型 208: 8节点二阶四边形 (Quadratic Quadrilateral)**

```
   4---7---3
   |       |
   8       6
   |       |
   1---5---2
```

- **角点**: 1, 2, 3, 4
- **中点**: 5, 6, 7, 8
- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6, node_7, node_8]`
- **拓扑边 (Topological Faces - 仅使用角点)**:
  - 边 1: `[1, 2]`
  - 边 2: `[2, 3]`
  - 边 3: `[3, 4]`
  - 边 4: `[4, 1]`

------

### 4. 三维单元 (3D Elements)

#### **单元类型 304: 4节点线性四面体 (Linear Tetrahedron)**	

```
      4
     /|\
    / | \
   /  |  \
  1---|---3
   \  |  /
    \ | /
     \|/
      2
```

- **节点列表**: `[node_1, node_2, node_3, node_4]`
- **面 (Faces)**:
  - 面 1 (底面): `[1, 2, 3]`
  - 面 2: `[1, 4, 2]` (注意顺序，原代码为1,3,1，修正为1,4,2更合理)
  - 面 3: `[2, 4, 3]` (修正)
  - 面 4: `[3, 4, 1]` (修正)

*(注: `get_faces_from_element` 函数中四面体的面定义顺序经过调整以符合标准的右手定则，确保法线向外)*

#### **单元类型 306: 6节点线性楔形体 (Linear Wedge/Penta)**

```
      6
     / \
    /   \
   4-----5
   |  3  |
   | / \ |
   |/   \|
   1-----2
```

- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6]`
- **面 (Faces)**:
  - 面 1 (底面, 三角形): `[1, 2, 3]`
  - 面 2 (顶面, 三角形): `[4, 5, 6]`
  - 面 3 (侧面, 四边形): `[1, 2, 5, 4]`
  - 面 4 (侧面, 四边形): `[2, 3, 6, 5]`
  - 面 5 (侧面, 四边形): `[3, 1, 4, 6]`

#### **单元类型 308: 8节点线性六面体 (Linear Hexahedron/Brick)**

```
      8-------7
     /|      /|
    / |     / |
   5-------6  |
   |  4----|--3
   | /     | /
   |/      |/
   1-------2
```

- **节点列表**: `[node_1, node_2, node_3, node_4, node_5, node_6, node_7, node_8]`
- **面 (Faces)**:
  - 面 1 (底面, Z-): `[1, 2, 3, 4]`
  - 面 2 (顶面, Z+): `[5, 6, 7, 8]`
  - 面 3 (前面, Y-): `[1, 2, 6, 5]`
  - 面 4 (后面, Y+): `[4, 3, 7, 8]`
  - 面 5 (左面, X-): `[1, 4, 8, 5]`
  - 面 6 (右面, X+): `[2, 3, 7, 6]`

#### **单元类型 310: 10节点二阶四面体 (Quadratic Tetrahedron)**

- **角点**: 1, 2, 3, 4
- **中点**: 5, 6, 7, 8, 9, 10
- **节点列表**: `[node_1, ..., node_4, node_5, ..., node_10]`
- **拓扑面 (Topological Faces - 仅使用角点)**:
  - 面 1: `[1, 2, 3]`
  - 面 2: `[1, 4, 2]`
  - 面 3: `[2, 4, 3]`
  - 面 4: `[3, 4, 1]`

#### **单元类型 320: 20节点二阶六面体 (Quadratic Hexahedron/Brick)**

- **角点**: 1, 2, 3, 4, 5, 6, 7, 8
- **中点**: 9 到 20
- **节点列表**: `[node_1, ..., node_8, node_9, ..., node_20]`
- **拓扑面 (Topological Faces - 仅使用角点)**:
  - 面 1 (底面): `[1, 2, 3, 4]`
  - 面 2 (顶面): `[5, 6, 7, 8]`
  - 面 3 (前面): `[1, 2, 6, 5]`
  - 面 4 (后面): `[4, 3, 7, 8]`
  - 面 5 (左面): `[1, 4, 8, 5]`
  - 面 6 (右面): `[2, 3, 7, 6]`

---

## Topology 数据（拓扑）

### 设计理念

我们定义一个连续网格体为一个 **body**，采用洪水填充算法来寻找body。这样做的目的是为了让复杂模型一目了然。

拓扑数据作为**派生数据**，从EnTT registry中的基础组件计算生成，存储在 `registry.ctx()` 中供后续系统使用。

### 核心设计原则

- **性能优先**: 使用 `entt::entity` 句柄而非索引，保证O(1)访问
- **稳定性**: 面的定义使用外部NodeID（OriginalID），保证唯一性
- **按需计算**: 拓扑数据仅在需要时构建，不影响基础数据加载
- **上下文存储**: 存储在 `registry.ctx()` 中，全局可访问

### TopologyData 结构（ECS集成）

```cpp
struct TopologyData {
    // --- 核心拓扑实体 ---
    std::vector<FaceKey> faces;  // FaceKey = std::vector<NodeID>
    std::unordered_map<FaceKey, FaceID, VectorHasher> face_key_to_id;

    // --- 关系映射表（使用 entt::entity）---
    // 单元 -> 面
    std::unordered_map<entt::entity, std::vector<FaceID>> element_to_faces;
    // 面 -> 单元
    std::vector<std::vector<entt::entity>> face_to_elements;

    // --- 连续体数据 ---
    std::unordered_map<entt::entity, BodyID> element_to_body;
    std::unordered_map<BodyID, std::vector<entt::entity>> body_to_elements;
};
```

### 关键改进：使用 entt::entity

**旧架构**：使用内部索引（size_t），索引可能因实体删除而失效
```cpp
std::vector<std::vector<ElementIndex>> face_to_elements;  // 不稳定
```

**新架构**：使用实体句柄（entt::entity），永久有效
```cpp
std::vector<std::vector<entt::entity>> face_to_elements;  // 稳定
```

### 拓扑数据的使用

#### 1. 构建拓扑

```cpp
// 从registry中提取拓扑关系
TopologySystems::extract_topology(registry);

// 拓扑数据自动存储在context中
auto& topology = *registry.ctx().get<std::unique_ptr<TopologyData>>();
```

#### 2. 查找连续体

```cpp
// 执行洪水填充算法
TopologySystems::find_continuous_bodies(registry);

// 访问结果
auto& topology = *registry.ctx().get<std::unique_ptr<TopologyData>>();
for (const auto& [body_id, elements] : topology.body_to_elements) {
    spdlog::info("Body {}: {} elements", body_id, elements.size());
}
```

#### 3. 拓扑查询示例

```cpp
// 查找单元的所有面
entt::entity elem_entity = /* ... */;
auto& faces = topology.element_to_faces[elem_entity];

// 查找共享某个面的所有单元
FaceID face_id = /* ... */;
auto& sharing_elements = topology.face_to_elements[face_id];

// 判断面的类型
if (sharing_elements.size() == 1) {
    // 边界面
} else if (sharing_elements.size() == 2) {
    // 内部面，连接两个单元
    entt::entity elem1 = sharing_elements[0];
    entt::entity elem2 = sharing_elements[1];
}
```

### TopologySystems（系统函数）

```cpp
class TopologySystems {
public:
    // 从registry提取拓扑
    static void extract_topology(entt::registry& registry);
    
    // 查找连续体
    static void find_continuous_bodies(entt::registry& registry);
    
private:
    // 辅助函数：提取单元的面
    static std::vector<FaceKey> get_faces_from_element(
        const std::vector<NodeID>& element_nodes,
        int element_type
    );
};
```

### 面的定义（FaceKey）

- **`FaceKey`**: `std::vector<NodeID>`（排序后的角点ID列表）
- **唯一性**: 通过排序保证同一个面有相同的FaceKey
- **角点原则**: 仅使用角点，忽略中点（确保线性和二阶单元能正确匹配）

**示例**：
```cpp
// 六面体单元的底面
FaceKey bottom_face = {1, 2, 3, 4};  // 排序后
std::sort(bottom_face.begin(), bottom_face.end());
```

---

## Set 数据（集合）

### 旧架构（已废弃）
```cpp
// 旧的 Mesh 结构体方式
SetID next_set_id = 0;
std::unordered_map<std::string, SetID> set_name_to_id;
std::vector<std::string> set_id_to_name;
std::unordered_map<SetID, std::vector<NodeID>> node_sets;
std::unordered_map<SetID, std::vector<ElementID>> element_sets;
```

### 新架构（ECS组件）

**关键创新**: 每个集合本身也是一个 **Entity**！

```cpp
// 集合名称组件
struct Component::SetName {
    std::string value;
};

// 节点集成员组件
struct Component::NodeSetMembers {
    std::vector<entt::entity> members;  // 直接存储节点实体句柄
};

// 单元集成员组件
struct Component::ElementSetMembers {
    std::vector<entt::entity> members;  // 直接存储单元实体句柄
};
```

### 集合的优势

**1. 一致性**: 集合与节点、单元同等对待，都是实体
**2. 扩展性**: 可为集合添加任意属性（如颜色、可见性等）
**3. 直接引用**: 成员列表直接存储实体句柄，无需ID查找

### 创建集合实体

```cpp
// 创建节点集
entt::entity nodeset_entity = registry.create();
registry.emplace<Component::SetName>(nodeset_entity, "fix_boundary");
auto& members = registry.emplace<Component::NodeSetMembers>(nodeset_entity);

// 添加成员（使用实体句柄）
for (const auto& node_id : raw_node_ids) {
    members.members.push_back(node_id_map.at(node_id));
}
```

### 查询集合

```cpp
// 查找特定名称的集合
entt::entity find_set_by_name(entt::registry& registry, const std::string& name) {
    auto view = registry.view<const Component::SetName>();
    for (auto entity : view) {
        if (view.get<const Component::SetName>(entity).value == name) {
            return entity;
        }
    }
    return entt::null;
}

// 遍历所有节点集
auto view = registry.view<Component::SetName, Component::NodeSetMembers>();
for (auto set_entity : view) {
    const auto& name = view.get<Component::SetName>(set_entity);
    const auto& members = view.get<Component::NodeSetMembers>(set_entity);
    
    spdlog::info("Node set '{}' has {} members", name.value, members.members.size());
}
```

### 使用集合数据

```cpp
// 获取集合的所有成员节点坐标
entt::entity set_entity = find_set_by_name(registry, "load_surface");
const auto& members = registry.get<Component::NodeSetMembers>(set_entity);

for (entt::entity node_entity : members.members) {
    const auto& pos = registry.get<Component::Position>(node_entity);
    // 对 pos.x, pos.y, pos.z 进行操作
}
```

### 新的声明式语法（v1.0+）

集合块也采用 **`[]` 语法**表示可变长度的成员列表：

```
*nodeset begin
# set id, set name, [member node ids]
0, fix, [3, 4, 7, 8]
1, load, [1, 2, 5, 6]
*nodeset end

*eleset begin
# set id, set name, [member element ids]
0, all, [1, 2, 3, 4]
*eleset end
```

**字段结构**：
- 字段0: Set ID (int) - 可忽略，仅用于兼容
- 字段1: Set Name (string)
- 字段2: Member IDs (vector<int>)

**旧格式（已废弃）**：
```
*nodeset begin
1, fix, 3, 4, 7, 8  # 字段数不固定
*nodeset end
```

---

## 声明式解析器架构

### 架构演进：从硬编码到声明式

hyperFEM 的解析器经历了一次重大架构重构，从"硬编码解析"转变为"声明式映射"。

#### 旧架构的问题

```cpp
// 旧方式：为每种数据类型写专门的解析器
NodeParser::parse(file, registry, node_id_map);
ElementParser::parse(file, registry, node_id_map);
SetParser::parse_node_sets(file, registry, node_id_map);
```

**缺点**：
- 添加新组件需要修改多个文件
- 解析逻辑分散，难以维护
- 大量重复的循环和错误处理代码

#### 新架构：声明式配置

```cpp
// 新方式：在一个地方声明所有映射规则
void FemParser::configure_handlers(...) {
    auto node_handler = std::make_unique<Parser::BlockHandler>(registry);
    
    // 声明字段到组件的映射
    node_handler->map<Component::OriginalID>(0, [](auto& comp, const auto& f) {
        comp.value = std::get<int>(f);
    });
    node_handler->map<Component::Position>(1, [](auto& comp, const auto& f) {
        comp.x = std::get<double>(f);
    });
    // ... 更多映射
    
    handlers["*node begin"] = std::move(node_handler);
}
```

### 核心组件

#### 1. GenericLineParser（通用行解析器）

负责将文本行解析为结构化字段：

```cpp
// 输入: "1, 308, [5, 6, 8, 7, 1, 2, 4, 3]"
auto fields = parse_line_to_fields(line);
// 输出: [int(1), int(308), vector<int>{5,6,8,7,1,2,4,3}]
```

**支持的类型**：
- `int`: 整数字段
- `double`: 浮点数字段（包含小数点）
- `string`: 字符串字段
- `vector<int>`: 使用 `[]` 语法的整数数组（自动识别：如果数组内所有项都是整数）
- `vector<double>`: 使用 `[]` 语法的浮点数数组（自动识别：如果数组内包含浮点数）

#### 2. BlockHandler（块处理器）

声明式的块处理核心：

```cpp
class BlockHandler : public IBlockHandler {
public:
    // 声明字段映射
    template<typename ComponentType>
    void map(int field_index, 
             std::function<void(ComponentType&, const Field&)> setter);
    
    // 自动处理整个块
    void process(std::ifstream& file) override;
};
```

**工作流程**：
1. 读取一行文本（支持 `&` 续行符）
2. 检查是否为新的关键字（以 `*` 开头），如果是则回退并停止
3. 调用 `parse_line_to_fields()` 解析字段
4. 创建一个新实体
5. 对每个已配置的字段索引，调用对应的 setter 函数
6. 附加组件到实体
7. 调用后处理钩子（如果有）

**关键改进**：`BlockHandler::process()` 现在会在遇到以 `*` 开头的行时自动停止并回退文件流，这支持两层分派架构（如材料解析）。

#### 3. FemParser（一级调度器）

极简的主解析器：

```cpp
bool FemParser::parse(const std::string& filepath, DataContext& data_context) {
    // 1. 配置所有处理器
    configure_handlers(handlers, registry, node_id_map, element_id_map, material_id_map);
    
    // 2. 简洁的调度循环
    while (std::getline(file, line)) {
        auto it = handlers.find(line);
        if (it != handlers.end()) {
            it->second->process(file);  // 分派给对应处理器
        }
    }
}
```

#### 4. MaterialDispatcherHandler（二级分发器）

专门用于材料块的两层分派系统：

```cpp
class MaterialDispatcherHandler : public IBlockHandler {
public:
    // 注册子类型的处理器
    void register_material_type(const std::string& keyword, 
                                std::unique_ptr<IBlockHandler> handler);
    
    // 主分发循环
    void process(std::ifstream& file) override;
};
```

**工作流程**：
1. 识别 `*material begin` 块
2. 读取行，查找 `**typeid` 子关键字（如 `**1`, `**101`）
3. 将控制权分发给对应的 `BlockHandler`
4. 子处理器读取材料参数行，直到遇到下一个 `**typeid` 或 `*material end`
5. 返回控制权给分发器

**优势**：完全消除 switch 语句，新材料类型只需在配置函数中注册即可。

### 添加新组件的示例

假设要为节点添加温度组件：

**步骤1**: 定义组件
```cpp
// physics_components.h
namespace Component {
    struct Temperature { double value; };
}
```

**步骤2**: 更新文件格式
```
*node begin
101, 1.0, 2.0, 3.0, 300.5  # 第5个字段是温度
*node end
```

**步骤3**: 添加一行配置
```cpp
// parserBase.cpp 中的 configure_handlers()
node_handler->map<Component::Temperature>(4, [](auto& comp, const auto& f) {
    comp.value = std::get<double>(f);
});
```

**完成**！无需修改任何循环或解析逻辑。

### 架构优势总结

| 方面 | 旧架构 | 新架构（声明式）|
|------|--------|----------------|
| **添加组件** | 修改多个文件 | 添加一行配置 |
| **代码量** | ~500行解析器 | ~100行核心 + 配置 |
| **可维护性** | 分散在多处 | 集中在一处 |
| **错误处理** | 重复实现 | 统一处理 |
| **文件格式** | 字段数不固定 | 固定结构（`[]`） |
| **类型安全** | 手动转换 | `std::variant` |

### 文件语法规范（v1.0）

#### [] 语法规则

使用 `[]` 包围可变长度的数组：

```
# 正确
1, 308, [5, 6, 8, 7, 1, 2, 4, 3]

# 错误：缺少 []
1, 308, 5, 6, 8, 7, 1, 2, 4, 3
```

#### 字段分隔

字段之间用逗号分隔，空格会被自动忽略：

```
1,308,[1,2,3]        # 有效
1, 308, [1, 2, 3]    # 有效（推荐，更清晰）
```

#### 数据类型识别

解析器自动识别字段类型：

```
123              → int
123.456          → double
hello            → string
[1, 2, 3]        → vector<int>（所有项都是整数）
[1.0, 2.5, 3.14] → vector<double>（包含浮点数）
[1, 2.0, 3]      → vector<double>（混合类型，自动提升为double）
```

**数组类型推断规则**：
- 如果数组内所有项都可以解析为整数，则识别为 `vector<int>`
- 如果数组内包含任何浮点数，则识别为 `vector<double>`
- 空数组 `[]` 会根据上下文推断类型

---

# 换行语法

我们规定：如果一行（在去除注释和前后空格后）以 `&` 符号结尾，那么它表示下一行是当前逻辑行的延续。

**示例：**

一个拥有20个节点的单元，如果不换行会非常长：

```
# 原始长行
101, 320, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20
```

使用行连续符 `&`，可以写成这样，更清晰：

```
# 使用行连续符 & 的多行
101, 320, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, &  # <-- 注意这里的注释
           11, 12, 13, 14, 15, 16, &       #     会被正确处理
           17, 18, 19, 20
```

我们的解析器会将上面这段多行文本合并为等效的单个字符串进行处理：

```
"101, 320, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20"
```

# hyperFEM 交互式程序使用说明

hyperFEM 提供两种操作模式：**批处理模式**和**交互模式**，以适应不同的使用场景。

#### 2.1 模式选择

程序的行为由启动时是否提供 `--input-file` (或 `-i`) 参数决定。

- **批处理模式 (Batch Mode)**

  - **触发方式**: 启动程序时提供输入文件路径。

  - **用途**: 自动化脚本、文件格式转换、无人值守任务。

  - **示例**:

    ```Bash
    # 仅解析并导出一个新文件
    hyperFEM_app --input-file case/model.xfem --output-file case/output.xfem
    ```

- **交互模式 (Interactive Mode)**

  - **触发方式**: 启动程序时不提供任何输入文件路径。

  - **用途**: 模型探索、交互式调试、分步分析与简化。

  - **示例**:

    ```Bash
    # 直接启动进入交互式控制台
    hyperFEM_app
    ```

#### 2.2 交互模式命令参考

启动后，您将看到 `hyperFEM>` 提示符，可以输入以下命令：

**`import <file_path>`**

- **功能**: 加载并解析一个 `.xfem` 格式的网格文件到内存中。如果已有模型被加载，此命令会先清空旧数据。

- **示例**:

  ```Bash
  hyperFEM> import D:\cases\complex_assembly.xfem
  ```

**`build_topology`**

- **功能**: 基于当前加载的网格，执行完整的拓扑分析。此命令会：

  1. 提取所有唯一的面，并建立单元与面之间的双向查找关系。
  2. 执行洪水填充算法，识别并划分所有的连续网格体（Bodies）。

- **前置条件**: 必须先使用 `import` 命令加载一个网格。

- **示例**:

  ```Bash
  hyperFEM> build_topology
  ```

**`list_bodies`**

- **功能**: 显示当前模型中所有已识别的连续网格体及其包含的单元数量。

- **前置条件**: 必须先运行 `build_topology`。

- **示例**:

  ```Bash
  hyperFEM> list_bodies
  ```

**`info`**

- **功能**: 显示当前会话的状态摘要，包括：

  - 已加载网格的节点、单元和集合数量。
  - 已构建拓扑的唯一面和连续体数量。

- **示例**:

  ```Bash
  hyperFEM> info
  ```

**`save <file_path>`**

- **功能**: 将当前 registry 中的网格数据（节点、单元、集合）导出到一个新的 `.xfem` 文件。注意：此命令只保存原始网格数据，不保存拓扑分析结果。

- **前置条件**: 必须先使用 `import` 命令加载一个网格。

- **示例**:

  ```Bash
  hyperFEM> save D:\cases\simplified_model.xfem
  ```

**`help`**

- **功能**: 显示所有可用命令的列表。

- **示例**:

  ```Bash
  hyperFEM> help
  ```

**`quit` 或 `exit`**

- **功能**: 退出 hyperFEM 交互式会话。

- **示例**:

  ```bash
  hyperFEM> quit
  ```

#### 2.3 典型工作流程 (交互模式)

一个典型的模型调试与分析流程如下：

1. **启动程序**: `hyperFEM_app`
2. **加载模型**: `import path/to/your/model.xfem`
3. **构建拓扑**: `build_topology`
4. **检查模型**: `info` (快速查看摘要)
5. **分析连续体**: `list_bodies` (查看模型由几个独立的零件构成)
6. **(未来功能)**: 执行选择、简化等操作...
7. **保存结果**: `save path/to/your/output.xfem`
8. **退出程序**: `exit`

# 函数曲线



# 材料

## 材料类型命名规则

材料类型采用三位数命名，首位是大类，第二三位是小类：

| 第一位 | 大类   |
| ------ | ------ |
| 0      | 弹性   |
| 1      | 超弹   |
| 2      | 粘弹   |
| 3      | 弹塑性 |

### 已支持的材料类型

| typeid | 材料               |
| ------ | ------------------ |
| 1      | 各向同性线弹性材料 |
| 101    | Polynomial（超弹性） |
| 102    | Reduced Polynomial（超弹性） |
| 103    | Ogden（超弹性） |

未来持续添加更多材料类型。

## 材料块语法（两层分派架构）

从 v1.0 开始，材料块采用**两层分派系统**，完全消除了 switch 语句：

1. **一级关键字**: `*material begin` - 由 FemParser 处理
2. **二级关键字**: `**typeid`（如 `**1`, `**101`）- 由 MaterialDispatcherHandler 分发

### 语法结构

```
*material begin
**typeid
# 材料参数行（根据typeid不同而不同）
mid, param1, param2, ...
**typeid
mid, param1, param2, ...
*material end
```

**关键特性**：
- 每个材料定义以 `**typeid` 开头（子关键字）
- 材料参数行紧跟在 `**typeid` 之后
- 当遇到下一个 `**typeid` 或 `*material end` 时，当前材料定义结束
- 支持使用 `[]` 语法表示数组参数（自动识别为 `vector<int>` 或 `vector<double>`）

## 材料类型 1：各向同性线弹性材料

**参数**：密度（rho）、弹性模量（E）、泊松比（nu）

**输入格式**：
```
*material begin
**1
# mid, rho, E, nu
1, 1e5, 2e9, 0.3
2, 1e5, 2e9, 0.3
*material end
```

**参数约束**：
- $rho > 0$（密度必须为正）
- $-1 \leq nu \leq 0.5$（泊松比范围）
- $E > 0$（弹性模量必须为正）

**字段结构**：
- 字段 0: Material ID (int)
- 字段 1: 密度 rho (double)
- 字段 2: 弹性模量 E (double)
- 字段 3: 泊松比 nu (double)

## 材料类型 101：Polynomial（超弹性）

超弹性材料支持两种定义方式：
1. **直接输入参数**：手动指定材料常数
2. **实验曲线拟合**：通过外部 Python 脚本拟合材料参数

### 直接输入格式

```
*material begin
**101
# mid, order, mode, [c_ij...], [d_i...]
# mode=0 表示直接输入
1, 1, 0, [1.0, 2.0], [0.1]
2, 2, 0, [1.0, 2.0, 3.0, 4.0], [0.1, 0.2]
*material end
```

**字段结构**（mode=0）：
- 字段 0: Material ID (int)
- 字段 1: Order N (int) - 多项式阶数
- 字段 2: Mode (int) - 0 表示直接输入
- 字段 3: c_ij 数组 (vector<double>) - 多项式系数 Cij，按 [C10, C01, C20, C02, ..., CN0, C0N] 顺序
- 字段 4: d_i 数组 (vector<double>) - 体积系数 Di，按 [D1, D2, ..., DN] 顺序

### 曲线拟合格式

```
*material begin
**101
# mid, order, mode, [uniaxial_func_ids], [biaxial_func_ids], [planar_func_ids], [volumetric_func_ids], nu
# mode=1 表示曲线拟合
1, 1, 1, [1], [2], [3], [4], 0.49
*material end
```

**字段结构**（mode=1）：
- 字段 0: Material ID (int)
- 字段 1: Order N (int)
- 字段 2: Mode (int) - 1 表示曲线拟合
- 字段 3: 单轴拉伸函数ID列表 (vector<int>) - 可选，可留空
- 字段 4: 双轴拉伸函数ID列表 (vector<int>) - 可选，可留空
- 字段 5: 平面拉伸函数ID列表 (vector<int>) - 可选，可留空
- 字段 6: 体积函数ID列表 (vector<int>) - 可选，可留空
- 字段 7: 泊松比 nu (double) - 用于拟合

**注意**：曲线拟合功能通过外部 Python 脚本实现，需要预先定义函数曲线数据。

## 材料类型 102：Reduced Polynomial（超弹性）

Reduced Polynomial 是 Polynomial 的简化版本，只包含 Ci0 项（不含 C0i 项）。

### 直接输入格式

```
*material begin
**102
# mid, order, mode, [c_i0...], [d_i...]
# mode=0 表示直接输入
1, 1, 0, [1.0], [0.1]
2, 2, 0, [1.0, 2.0], [0.1, 0.2]
*material end
```

**字段结构**（mode=0）：
- 字段 0: Material ID (int)
- 字段 1: Order N (int)
- 字段 2: Mode (int) - 0 表示直接输入
- 字段 3: c_i0 数组 (vector<double>) - 多项式系数 Ci0，按 [C10, C20, ..., CN0] 顺序
- 字段 4: d_i 数组 (vector<double>) - 体积系数 Di，按 [D1, D2, ..., DN] 顺序

### 曲线拟合格式

```
*material begin
**102
# mid, order, mode, [uniaxial_func_ids], [biaxial_func_ids], [planar_func_ids], [volumetric_func_ids], nu
1, 1, 1, [1, 2], [], [3], [4], 0.49
*material end
```

## 材料类型 103：Ogden（超弹性）

Ogden 模型使用不同的参数化方式。

### 直接输入格式

```
*material begin
**103
# mid, order, mode, [alpha_i...], [mu_i...], [d_i...]
# mode=0 表示直接输入
1, 1, 0, [1.5], [1.0], [0.1]
2, 2, 0, [1.5, -2.0], [1.0, 0.5], [0.1, 0.2]
*material end
```

**字段结构**（mode=0）：
- 字段 0: Material ID (int)
- 字段 1: Order N (int)
- 字段 2: Mode (int) - 0 表示直接输入
- 字段 3: alpha_i 数组 (vector<double>) - 按 [alpha1, alpha2, ..., alphaN] 顺序
- 字段 4: mu_i 数组 (vector<double>) - 按 [mu1, mu2, ..., muN] 顺序
- 字段 5: d_i 数组 (vector<double>) - 体积系数，按 [D1, D2, ..., DN] 顺序

### 曲线拟合格式

```
*material begin
**103
# mid, order, mode, [uniaxial_func_ids], [biaxial_func_ids], [planar_func_ids], [volumetric_func_ids], nu
1, 1, 1, [1, 2], [], [3], [], 0.49
*material end
```

## 材料解析架构

材料解析采用**声明式两层分派系统**：

1. **FemParser（一级）**: 识别 `*material begin`，创建 MaterialDispatcherHandler
2. **MaterialDispatcherHandler（二级）**: 识别 `**typeid`，分发到对应的 BlockHandler
3. **BlockHandler（三级）**: 解析具体材料参数行

### 添加新材料类型

添加新材料类型非常简单，只需在 `FemParser::configure_handlers()` 中添加配置：

```cpp
// 创建新材料处理器
auto handler_new = std::make_unique<Parser::BlockHandler>(registry);

// 配置字段映射
handler_new->map<Component::MaterialID>(0, [](auto& comp, const auto& f) {
    comp.value = std::get<int>(f);
});
// ... 更多字段映射 ...

// 注册到分发器
mat_dispatcher->register_material_type("**新typeid", std::move(handler_new));
```

**无需修改任何解析循环或 switch 语句！**

---

# 快速参考：EnTT ECS 常用操作

## 实体创建与组件附加

```cpp
// 创建实体
entt::entity entity = registry.create();

// 附加单个组件
registry.emplace<Component::Position>(entity, x, y, z);
registry.emplace<Component::OriginalID>(entity, id);

// 获取组件（假设实体有该组件）
auto& pos = registry.get<Component::Position>(entity);

// 安全获取组件（可能不存在）
if (auto* pos = registry.try_get<Component::Position>(entity)) {
    // 使用 pos
}

// 检查是否有某组件
bool has_pos = registry.all_of<Component::Position>(entity);

// 移除组件
registry.remove<Component::Position>(entity);

// 销毁实体
registry.destroy(entity);
```

## 查询与遍历

```cpp
// 单组件视图
auto view = registry.view<Component::Position>();
for (auto entity : view) {
    auto& pos = view.get<Component::Position>(entity);
}

// 多组件视图（交集）
auto view = registry.view<Component::Position, Component::OriginalID>();
for (auto entity : view) {
    auto [pos, id] = view.get<Component::Position, Component::OriginalID>(entity);
    // 使用结构化绑定
}

// 统计实体数量
size_t count = view.size_hint();

// 带过滤的视图
auto view = registry.view<Component::Position>(entt::exclude<Component::Velocity>);
// 只遍历有Position但没有Velocity的实体
```

## 常见模式

```cpp
// 1. 从OriginalID查找实体（需要建立映射）
std::unordered_map<int, entt::entity> id_to_entity;
// 在解析时填充映射
id_to_entity[original_id] = entity;

// 2. 遍历单元的所有节点
const auto& conn = registry.get<Component::Connectivity>(elem_entity);
for (entt::entity node_entity : conn.nodes) {
    const auto& pos = registry.get<Component::Position>(node_entity);
    // 处理节点
}

// 3. 按名称查找集合
entt::entity find_set(entt::registry& registry, const std::string& name) {
    auto view = registry.view<Component::SetName>();
    for (auto entity : view) {
        if (view.get<Component::SetName>(entity).value == name) {
            return entity;
        }
    }
    return entt::null;
}

// 4. 访问上下文中的拓扑数据
if (registry.ctx().contains<std::unique_ptr<TopologyData>>()) {
    auto& topology = *registry.ctx().get<std::unique_ptr<TopologyData>>();
    // 使用 topology
}
```

## 性能提示

1. **使用 `view.get()` 而不是 `registry.get()`**：前者已经验证了组件存在
2. **尽量使用多组件view**：比多个单组件view更高效
3. **实体句柄是轻量级的**：可以自由拷贝和存储
4. **避免在循环中创建view**：在循环外创建view，循环内迭代
5. **使用 `size_hint()` 而不是 `size()`**：更快，对于大多数场景足够

## 调试技巧

```cpp
// 打印实体信息
spdlog::debug("Entity {} has Position: {}", 
              entt::to_integral(entity),  // 将entity转为整数
              registry.all_of<Component::Position>(entity));

// 统计组件使用情况
spdlog::info("Total nodes: {}", 
             registry.view<Component::Position>().size_hint());
spdlog::info("Total elements: {}", 
             registry.view<Component::Connectivity>().size_hint());
spdlog::info("Total sets: {}", 
             registry.view<Component::SetName>().size_hint());

// 验证数据完整性
auto view = registry.view<Component::Connectivity>();
for (auto elem_entity : view) {
    const auto& conn = view.get<Component::Connectivity>(elem_entity);
    for (entt::entity node_entity : conn.nodes) {
        if (!registry.valid(node_entity)) {
            spdlog::error("Element references invalid node!");
        }
    }
}
```

---

**文档更新日期**: 2025-11-03  
**架构版本**: EnTT ECS v1.0  
**实体ID类型**: 64-bit (`std::uint64_t`)
