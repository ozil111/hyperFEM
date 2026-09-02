# NovaFEA JSON 格式参考

## 概述

NovaFEA 支持使用 JSON 格式定义有限元模型。JSON 格式比传统的 `.xfem` 文本格式更易读、更易解析，并且支持注释（使用 `.jsonc` 扩展名）。

JSON 解析器（`system/parser_json/JsonParser.cpp`）基于 nlohmann::json 实现，采用 N-Step 解析策略，严格按照实体依赖顺序解析，并以 ECS（EnTT）Plan B 引用模式建立实体间关系。

## 文件结构

完整的 JSON 文件结构如下（所有顶层键均为可选，按需提供）：

```jsonc
{
    "material": [ /* 材料定义 */ ],
    "property": [ /* 属性（截面）定义 */ ],
    "mesh": {
        "nodes":    [ /* 节点定义 */ ],
        "elements": [ /* 单元定义 */ ]
    },
    "nodeset":  [ /* 节点集定义 */ ],
    "eleset":   [ /* 单元集定义 */ ],
    "curve":    [ /* 时间曲线定义 */ ],
    "load":     [ /* 载荷定义 */ ],
    "boundary": [ /* 边界条件定义 */ ],
    "analysis": [ /* 分析设置（数组，取第一个元素生效）*/ ],
    "output":   { /* 输出请求（单个全局对象）*/ }
}
```

## 解析顺序与依赖

解析器按以下依赖顺序解析各实体（摘自 `JsonParser.h`）：

1. **Material**（无依赖）
2. **Property**（依赖 Material）
3. **Node**（无依赖）
4. **Element**（依赖 Node、Property）
5. **NodeSet**（依赖 Node）
6. **EleSet**（依赖 Element）
7. **Curve**（无依赖，须先于 Load）
8. **Load**（依赖 Curve）
9. **Boundary**（无依赖）
10. **Apply Load**（依赖 Load、NodeSet）
11. **Apply Boundary**（依赖 Boundary、NodeSet）
12. **Analysis / Output**（最后解析）

## 详细说明

### 1. Material（材料）

材料是独立的实体，通过 `mid` (Material ID) 标识。

#### 1.1 线弹性材料 (typeid: 1)

```jsonc
{
    "mid": 1,           // Material ID（必填）
    "typeid": 1,        // 类型：1 = 各向同性线弹性
    "rho": 1000.0,      // 密度（必填）
    "E": 2e9,           // 弹性模量（必填）
    "nu": 0.3           // 泊松比（必填）
}
```

> **注意**：当前解析器只实现 `typeid: 1`。未知 typeid 会告警并跳过参数。

#### 未来扩展

组件层（`material_components.h`）已预留以下类型定义，但 JSON 解析尚未接入：

```jsonc
// 超弹性材料
{"mid": 2, "typeid": 101, "order": 2, "c_ij": [...], "d_i": [...]}   // Polynomial
{"mid": 3, "typeid": 102, "order": 1, "c_i0": [...], "d_i": [...]}   // ReducedPolynomial
{"mid": 4, "typeid": 103, "order": 2, "mu_i": [...], "alpha_i": [...], "d_i": [...]}  // Ogden

// 塑性材料（预留）
{"mid": 5, "typeid": 301, ...}   // IsotropicPlasticJC (LAW2)
{"mid": 6, "typeid": 302, ...}   // RateDependentPlastic (LAW36)
```

### 2. Property（属性 / 截面）

属性定义单元的截面与积分特性，并通过 `mid` 引用材料。材料绑定实际通过 `SimdroidPart` 完成（解析器在 Step 4.5 自动为每个 Property 建立 Part）。

#### 2.1 固体单元属性 (typeid: 1)

```jsonc
{
    "pid": 1,                       // Property ID（必填）
    "typeid": 1,                    // 类型：1 = 固体单元
    "mid": 1,                       // 引用的 Material ID（必填）
    "integration_network": 2,       // 积分点数，默认 1（可选）
    "hourglass_control": "eas"      // 沙漏控制，默认 ""（可选）
}
```

**字段说明**：
- `integration_network`：积分点数（如 Hex8 全积分 8、缩减积分 1）
- `hourglass_control` 可取值：`eas`、`rph`、`enhanced`、`stiffness`、`relax_stiffness`、`standard`、`viscous`、`""`（无）

**说明**：
- 一个 Material 可以被多个 Property 引用
- 每个 Property 自动生成名为 `Part_pid_<pid>` 的 SimdroidPart 和对应单元集
- 未来可扩展 Shell Property、Beam Property 等

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
    "etype": 308,                       // 单元类型，308 = Hexa8
    "pid": 1,                           // 引用的 Property ID
    "nids": [5, 6, 8, 7, 1, 2, 4, 3]   // 节点 ID 列表
}
```

**支持的单元类型 (etype)**：
- `102`: Line2（2 节点线单元）
- `103`: B31（梁单元，导出用）
- `203`: Triangle3（3 节点三角形）
- `204`: Quad4（4 节点四边形）
- `304`: Tetra4（4 节点四面体）
- `306`: Wedge6（6 节点楔形）
- `308`: Hexa8（8 节点六面体）
- `310`: Tetra10（10 节点四面体）
- `320`: Hexa20（20 节点六面体）

> **注意**：当前求解器（线性静力 / 显式）主要实现 `308`（C3D8R）和 `304`（Tet4）的刚度/质量组装，其他类型仅保证解析与导出。

### 4. NodeSet（节点集）

节点集用于定义一组节点，通常用于施加边界条件或载荷。

```jsonc
{
    "nsid": 1,              // NodeSet ID
    "name": "fix",          // 节点集名称，默认 "nodeset_<nsid>"（可选）
    "nids": [3, 4, 7, 8]    // 包含的节点 ID 列表
}
```

### 5. EleSet（单元集）

单元集用于定义一组单元，用于分析或后处理。

```jsonc
{
    "esid": 1,      // EleSet ID
    "name": "all",  // 单元集名称，默认 "eleset_<esid>"（可选）
    "eids": [1]     // 包含的单元 ID 列表
}
```

### 6. Curve（时间曲线）

时间曲线用于随时间缩放载荷值。每条曲线通过 `cid` 标识。

```jsonc
{
    "cid": 1,               // Curve ID（必填）
    "type": "linear",       // 曲线类型（必填），如 "linear"
    "x": [0.0, 1.0],        // 自变量数组（通常是时间）
    "y": [0.0, 1.0]         // 因变量数组（缩放因子）
}
```

**校验规则**：
- `x` 与 `y` 数组长度必须一致，且不能为空，否则该曲线被跳过
- Load 未指定 `curve` 时，自动使用默认曲线 `cid: 0`（`type: "linear"`，`x: [0,1]`、`y: [1,1]`，即常值 1.0 不缩放）

### 7. Load（载荷）

载荷是抽象的定义，通过 NodeSet 应用到节点上。

#### 7.1 节点载荷 (typeid: 1)

```jsonc
{
    "lid": 1,       // Load ID（必填）
    "typeid": 1,    // 类型：1 = 节点载荷
    "nsid": 2,      // 应用到的 NodeSet ID（必填）
    "dof": "y",     // 载荷方向（必填）
    "value": 100.0, // 载荷值 N（必填）
    "curve": 1      // 时间曲线 ID（可选，默认使用 Curve 0）
}
```

**dof 取值**：
- 显式求解器 / 载荷装配支持：`"all"`（或 `"xyz"`，三方向同时施加）、`"x"`、`"y"`、`"z"`
- 线性静力求解器额外支持组合：`"xy"`、`"xz"`、`"yz"`（及逆序写法）

**说明**：
- 载荷按曲线缩放：实际载荷 = `value × curve(t)`
- 一个节点可同时被多个载荷引用（1-to-Many）

#### 未来扩展

```jsonc
// 压力载荷等面载荷类型尚未在 JSON 解析器中实现
```

### 8. Boundary（边界条件）

边界条件是抽象的定义，通过 NodeSet 应用到节点上。

#### 8.1 单点约束 (typeid: 1)

```jsonc
{
    "bid": 1,       // Boundary ID（必填）
    "typeid": 1,    // 类型：1 = SPC (Single Point Constraint)
    "nsid": 1,      // 应用到的 NodeSet ID（必填）
    "dof": "all",   // 约束自由度（必填），取值同 Load 的 dof
    "value": 0.0    // 指定位移（必填）
}
```

**说明**：
- `value` 是该自由度上的**指定位移**：`0.0` 表示固定，非零值即位移载荷（线性静力分析支持位移控制加载）
- `dof` 取值同第 7 节（`"all"` / `"xyz"` / `"x"` / `"y"` / `"z"` / `"xy"` / `"xz"` / `"yz"`）
- Boundary 通过 `nsid` 引用 NodeSet，应用到该集合中的所有节点

### 9. Analysis（分析设置）

`analysis` 是**数组**，每个元素通过 `aid` 标识。**数组中第一个元素**被同步为 DataContext 的活动分析实体；文件中没有 `analysis` 字段时默认按 `static` 处理。

```jsonc
"analysis": [
    {
        "aid": 1,                    // Analysis ID（必填）
        "analysis_type": "static",   // "static"（线性静力）或 "explicit"（显式动力学），默认 "static"
        "endtime": 1.0,              // 终止时间（可选）
        "fixed_time_step": 1.0       // 固定时间步长（可选）
    }
]
```

### 10. Output（输出请求）

`output` 是**单个对象**（非数组），当前只支持一个全局输出实体。

```jsonc
"output": {
    "node_output":    ["displacement"],   // 节点输出变量列表（可选）
    "element_output": ["stress"],         // 单元输出变量列表（可选）
    "interval_time": 0.01                 // 输出时间间隔（可选）
}
```

## 完整示例

以下是一个完整的单单元立方体模型（线性静力，节点力加载）：

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
        {"nsid":1, "name":"fix",  "nids":[3, 4, 7, 8]},
        {"nsid":2, "name":"load", "nids":[1, 2, 5, 6]}
    ],
    "eleset": [
        {"esid":1, "name":"all", "eids":[1]}
    ],
    "curve": [
        {"cid":1, "type":"linear", "x":[0.0, 1.0], "y":[0.0, 1.0]}
    ],
    "boundary": [
        {"bid":1, "typeid":1, "nsid":1, "dof":"all", "value":0.0}
    ],
    "load": [
        {"lid":1, "typeid":1, "nsid":2, "dof":"y", "value":100.0, "curve":1}
    ],
    "analysis": [
        {"aid":1, "analysis_type":"static", "endtime":1.0, "fixed_time_step":1.0}
    ],
    "output": {
        "node_output": ["displacement"],
        "interval_time": 1.0
    }
}
```

实际测试用例可参考 `test_case/hex8_mat1_im/hex8_mat1_im.jsonc`（最小模型）与 `test_case/implicit_c3d8r/T01/T01_im.jsonc`（完整模型）。

## 使用方法

### 命令行（批处理模式）

```bash
# 解析并求解，输出 VTK 结果
NovaFEA_app -i model.jsonc --output result.vtk

# 解析并求解，输出 CSV 结果（Abaqus 格式，<prefix>_disp.csv / <prefix>_elements.csv）
NovaFEA_app -i model.jsonc --output-csv result

# 仅导入并导出预处理网格
NovaFEA_app -i model.jsonc -e output.xfem
```

主要选项（`NovaFEA_app --help`）：
- `--input-file, -i <file>`：输入文件（`.xfem` / `.json` / `.jsonc`，按扩展名自动选择解析器）
- `--export, -e <file>`：导出预处理网格（`.xfem` / `.jsonc`）
- `--output, -o <file>`：输出结果文件（`.vtk`）
- `--output-csv <prefix>`：输出 CSV 结果前缀
- `--script, -s <file>`：脚本模式（每行一条命令，`#` 为注释）
- `--log-level, -l` / `--log-directory, -d`：日志设置

### 交互 / 脚本模式

```text
> import model.jsonc        # 导入模型（清空后解析，支持 .json/.jsonc/.xfem/.inp）
> json_apply patch.jsonc    # 将 JSON 片段合并进当前模型（不清空、可引用已有实体）
> build_topology            # 构建拓扑
> list_bodies               # 列出 Part
> save output.xfem          # 保存
> quit
```

`json_apply` 与完整导入使用相同的顶层键和实体结构，适合分步建模（如先导入网格，再施加边界与载荷）。

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
  ├─ Component::ElementID (eid)
  ├─ Component::ElementType (etype)
  ├─ Component::Connectivity (nids[])
  └─ Component::PropertyRef ─────→ Property 实体（截面/积分）
                                   ├─ Component::PropertyID (pid)
                                   └─ Component::SolidProperty

SimdroidPart（绑定器）: element_set + section(Property) + material
TopologyData.element_uid_to_part_map[eid] ───→ Part 实体 ───→ Material 实体
                                                             ├─ Component::MaterialID (mid)
                                                             └─ Component::LinearElasticParams

Node 实体
  ├─ Component::NodeID (nid)
  ├─ Component::Position (x, y, z)
  ├─ Component::AppliedLoadRef     ──→ [Load 实体（含 NodalLoad + Curve 引用）]
  └─ Component::AppliedBoundaryRef ──→ [Boundary 实体（含 BoundarySPC）]
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
6. **显式声明分析设置**：通过 `analysis` 指定 `analysis_type`、`endtime`、`fixed_time_step`，避免依赖默认值
7. **载荷时间历程用 Curve 控制**：静载荷可不指定 `curve`（默认常值），随时间变化的载荷显式定义曲线

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
