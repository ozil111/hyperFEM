# hyperFEM JSON 格式参考

## 概述

hyperFEM 支持使用 JSON 格式定义有限元模型。JSON 格式比传统的 `.xfem` 文本格式更易读、更易解析，并且支持注释（使用 `.jsonc` 扩展名）。

## 文件结构

完整的 JSON 文件结构如下：

```jsonc
{
    "material": [ /* 材料定义 */ ],
    "property": [ /* 属性定义 */ ],
    "mesh": {
        "nodes": [ /* 节点定义 */ ],
        "elements": [ /* 单元定义 */ ]
    },
    "nodeset": [ /* 节点集定义 */ ],
    "eleset": [ /* 单元集定义 */ ],
    "boundary": [ /* 边界条件定义 */ ],
    "load": [ /* 载荷定义 */ ],
    "analysis": { /* 分析设置（未来实现）*/ }
}
```

## 详细说明

### 1. Material（材料）

材料是独立的实体，通过 `mid` (Material ID) 标识。

#### 1.1 线弹性材料 (typeid: 1)

```jsonc
{
    "mid": 1,           // Material ID
    "typeid": 1,        // 类型：1 = 线弹性
    "rho": 1000.0,      // 密度 (kg/m³)
    "E": 2e9,           // 弹性模量 (Pa)
    "nu": 0.3           // 泊松比
}
```

#### 未来扩展

```jsonc
// 超弹性材料 (typeid: 101 = Polynomial)
{
    "mid": 2,
    "typeid": 101,
    "order": 2,
    "c_ij": [1.0, 0.5, 0.2, 0.1],
    "d_i": [0.01, 0.001]
}
```

### 2. Property（属性）

属性定义单元的物理特性，并引用材料。

#### 2.1 固体单元属性 (typeid: 1)

```jsonc
{
    "pid": 1,                       // Property ID
    "typeid": 1,                    // 类型：1 = 固体单元
    "mid": 1,                       // 引用的 Material ID
    "integration_network": 2,       // 积分网络（如 2x2x2）
    "hourglass_control": "eas"      // 沙漏控制方法：eas, viscous, 等
}
```

**说明：**
- Property 通过 `mid` 引用 Material
- 一个 Material 可以被多个 Property 引用
- 未来可扩展 Shell Property (typeid: 2), Beam Property (typeid: 3) 等

### 3. Mesh（网格）

#### 3.1 Nodes（节点）

```jsonc
{
    "nid": 1,       // Node ID
    "x": 1.0,       // X 坐标
    "y": 1.0,       // Y 坐标
    "z": 1.0        // Z 坐标
}
```

#### 3.2 Elements（单元）

```jsonc
{
    "eid": 1,                           // Element ID
    "etype": 308,                       // 单元类型：308 = Hexa8
    "pid": 1,                           // 引用的 Property ID
    "nids": [5, 6, 8, 7, 1, 2, 4, 3]   // 节点 ID 列表
}
```

**支持的单元类型 (etype)：**
- `102`: Line2 (2节点线单元)
- `203`: Triangle3 (3节点三角形)
- `204`: Quad4 (4节点四边形)
- `304`: Tetra4 (4节点四面体)
- `308`: Hexa8 (8节点六面体)
- `310`: Tetra10 (10节点四面体)
- `320`: Hexa20 (20节点六面体)

### 4. NodeSet（节点集）

节点集用于定义一组节点，通常用于施加边界条件或载荷。

```jsonc
{
    "nsid": 1,              // NodeSet ID
    "name": "fix",          // 节点集名称
    "nids": [3, 4, 7, 8]    // 包含的节点 ID 列表
}
```

### 5. EleSet（单元集）

单元集用于定义一组单元，用于分析或后处理。

```jsonc
{
    "esid": 1,      // EleSet ID
    "name": "all",  // 单元集名称
    "eids": [1]     // 包含的单元 ID 列表
}
```

### 6. Boundary（边界条件）

边界条件是抽象的定义，通过 NodeSet 应用到节点上。

#### 6.1 单点约束 (typeid: 1)

```jsonc
{
    "bid": 1,       // Boundary ID
    "typeid": 1,    // 类型：1 = SPC (Single Point Constraint)
    "nsid": 1,      // 应用到的 NodeSet ID
    "dof": "all",   // 约束自由度："all", "x", "y", "z", "xy", "xz", "yz", "xyz"
    "value": 0.0    // 约束值（通常为 0）
}
```

**说明：**
- `dof` 指定约束的自由度
- `"all"` 表示约束所有平动自由度（x, y, z）
- Boundary 通过 `nsid` 引用 NodeSet，然后应用到该集合中的所有节点

### 7. Load（载荷）

载荷是抽象的定义，通过 NodeSet 应用到节点上。

#### 7.1 节点载荷 (typeid: 1)

```jsonc
{
    "lid": 1,       // Load ID
    "typeid": 1,    // 类型：1 = 节点载荷
    "nsid": 2,      // 应用到的 NodeSet ID
    "dof": "all",   // 载荷方向："all", "x", "y", "z"
    "value": 1000.0 // 载荷值（N）
}
```

**说明：**
- `dof` 指定载荷方向
- `"all"` 表示在所有方向上均匀分布载荷
- Load 通过 `nsid` 引用 NodeSet，然后应用到该集合中的所有节点

#### 未来扩展

```jsonc
// 压力载荷 (typeid: 2)
{
    "lid": 2,
    "typeid": 2,
    "esid": 1,      // 应用到 EleSet
    "face": "top",  // 施加压力的面
    "value": -1000.0
}
```

## 完整示例

以下是一个完整的单单元立方体模型：

```jsonc
{
    "material": [
        {"mid":1, "typeid":1, "rho":1000.0, "E":2e9, "nu":0.3}
    ],
    "property": [
        {"pid":1, "typeid":1, "mid":1, "integration_network":2, "hourglass_control":"eas"}
    ],
    "mesh": {
        "nodes": [
            {"nid":1, "x":1.0, "y":1.0, "z":1.0},
            {"nid":2, "x":1.0, "y":0.0, "z":1.0},
            {"nid":3, "x":1.0, "y":1.0, "z":0.0},
            {"nid":4, "x":1.0, "y":0.0, "z":0.0},
            {"nid":5, "x":0.0, "y":1.0, "z":1.0},
            {"nid":6, "x":0.0, "y":0.0, "z":1.0},
            {"nid":7, "x":0.0, "y":1.0, "z":0.0},
            {"nid":8, "x":0.0, "y":0.0, "z":0.0}
        ],
        "elements": [
            {"eid":1, "etype":308, "pid":1, "nids":[5, 6, 8, 7, 1, 2, 4, 3]}
        ]
    },
    "nodeset": [
        {"nsid":1, "name":"fix", "nids":[3, 4, 7, 8]},
        {"nsid":2, "name":"load", "nids":[1, 2, 5, 6]}
    ],
    "eleset": [
        {"esid":1, "name":"all", "eids":[1]}
    ],
    "boundary": [
        {"bid":1, "typeid":1, "nsid":1, "dof":"all", "value":0.0}
    ],
    "load": [
        {"lid":1, "typeid":1, "nsid":2, "dof":"all", "value":1000.0}
    ]
}
```

## 使用方法

### 命令行

```bash
# 批处理模式
hyperFEM_app -i model.jsonc -o output.xfem

# 交互模式
hyperFEM_app
> import model.jsonc
> build_topology
> list_bodies
> save output.xfem
> quit
```

### 注释支持

使用 `.jsonc` 扩展名可以添加注释：

```jsonc
{
    // 这是单行注释
    "material": [
        {"mid":1, "typeid":1, "rho":1000.0, "E":2e9, "nu":0.3}  // 线弹性材料
    ],
    /* 
     * 这是多行注释
     * 可以用来详细说明
     */
    "mesh": {
        "nodes": [ /* ... */ ]
    }
}
```

## ECS 架构说明

### Plan B 引用模式

在 ECS 内部，实体间通过 `entt::entity` 句柄建立引用关系：

```
Element 实体
  ├─ Component::OriginalID (eid)
  ├─ Component::ElementType (etype)
  ├─ Component::Connectivity (nids[])
  └─ Component::PropertyRef ───→ Property 实体
                                   ├─ Component::OriginalID (pid)
                                   ├─ Component::SolidProperty
                                   └─ Component::MaterialRef ───→ Material 实体
                                                                   ├─ Component::OriginalID (mid)
                                                                   └─ Component::LinearElasticParams
```

这种设计的优势：
- **内存效率高**：Material 和 Property 只存储一次
- **易于修改**：更新 Material 参数会影响所有引用它的 Element
- **灵活可扩展**：易于添加新的材料类型和属性类型
- **查询高效**：通过 entity 句柄直接访问，无需 ID 查找

## 最佳实践

1. **使用有意义的 ID**：虽然 ID 可以是任意整数，但使用连续的 ID (1, 2, 3, ...) 更易于调试
2. **使用描述性的名称**：NodeSet 和 EleSet 的 `name` 字段应该清晰地描述其用途
3. **合理组织节点集**：为不同的边界条件和载荷创建独立的 NodeSet
4. **复用材料和属性**：相同材料和属性应该共享同一个 Material/Property 实体
5. **添加注释**：使用 `.jsonc` 格式，为复杂的模型添加注释

## 与 .xfem 格式的对比

| 特性 | .xfem (旧) | .jsonc (新) |
|------|-----------|-------------|
| 可读性 | 一般 | 优秀 |
| 注释支持 | 是 (#) | 是 (//, /* */) |
| 解析速度 | 快 | 非常快 |
| 扩展性 | 较难 | 容易 |
| 类型安全 | 无 | 有 (JSON schema) |
| 工具支持 | 少 | 丰富 (编辑器高亮、验证) |
| 层级关系 | 难以表达 | 清晰 |

推荐在新项目中使用 JSON 格式，旧的 `.xfem` 文件仍然受支持以保证向后兼容。

