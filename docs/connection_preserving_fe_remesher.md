# 连接保持有限元重网格器

连接保持有限元重网格器（Connection-Preserving FE Remesher）是一个面向调试的模型缩减工作流。其目标是生成一个规模远小于原模型的有限元模型，同时保持与求解器相关的连接约定。

输出**仅**用于 bug 复现、求解器调试、回归测试和开发验证。它不适用于工程分析、设计决策、位移精度、应力精度或频率精度。

## 保持的约定

重网格器将部件间连接关系作为首要不变量：

- 部件（Part）标识保持不变。
- 每个部件的单元类型集合保持不变。
- 每个部件的属性类型和材料类型保持不变。
- 接触（Contact）、绑定（Tie）、多点约束（MPC）和共享节点（SharedNode）接口标识保持不变。
- 当 ECS 模型中存在接触和约束的格式元数据时，应保持其不变。

改变以上任何约定都视为重网格失败，即使缩减后的网格能够成功求解。

## 当前实现

当前实现包含两个层次：

1. 规划与验证层。
2. 针对结构化 Hex8 部件的多部件网格生成层。

### 规划与验证

- `ConnectionPreservingRemesher::build_plan(...)`
  从当前 ECS 注册表和 `SimdroidInspector` 构建可机器读取的缩减计划。
- `ConnectionPreservingRemesher::validate_preservation(before, after)`
  比较两个 `RemeshPlan` 对象，检查部件签名和接口签名是否得到保持。
- `ConnectionPreservingRemesher::validate_preservation_detailed(registry, blueprint, after)`
  深入 ECS 注册表，执行细粒度保持性检查：
  - 所有 `NodeSetMembers`、`ElementSetMembers` 非空性检查（`SurfaceSetMembers` 为 warning 级别）
  - 蓝图 Load 和 Boundary 所引用 NodeSet 的存在性与非空性检查
  - 蓝图 PartProperty 所引用 EleSet 的存在性与非空性检查
  - `AppliedLoadRef` / `AppliedBoundaryRef` 覆盖性检查（原模型有载荷/约束时，重网格后不得丢失）
  - Registry 实际 `ElementID` 数量与 plan 一致性校验
- `ConnectionPreservingRemesher::write_plan_json(...)`
  将计划写入 JSON 文件。
- `remesh_plan [output.json] [ratio]`
  交互式命令，将计划输出为 JSON。

### 结构化 Hex8 生成

- `ConnectionPreservingRemesher::remesh_structured_hex8(...)`
  针对一个或多个结构化 Hex8 部件执行受限的物理网格替换。
- `remesh_generate [output_dir] [ratio]`
  交互式命令，输出重网格后的 Simdroid 项目和验证产物。

此生成器刻意保持功能精简，目前支持：

- 一个或多个 `SimdroidPart`，每个部件独立结构化粗化；
- 每个部件恰好一种单元类型 Hex8（`ElementType=308`）；
- 每个部件的节点坐标构成完整的三维结构化网格；
- 通过最近粗网格节点重建节点集（跨部件全局匹配）；
- 通过分配每部件新的粗单元来重建单元集；
- 从每部件新的外部粗面重建表面集，并通过质心最近邻匹配将表面分配到原有的表面集（保持 Contact/Tie 的 master 面集与 slave 节点集引用）；
- 通过原始 Simdroid 蓝图集名称重新附加载荷和边界条件。

对于不支持的模型，它会拒绝处理，而非静默生成具有误导性的网格。

## CLI 用法

仅生成计划：

```text
import_simdroid path/to/control.json
remesh_plan remesh_plan.json 100
```

生成的 JSON 包含：

- 请求的压缩选项
- 原始和目标单元数量
- 部件级别的目标单元数量
- 单元类型分布
- 材料/属性类型签名
- 接口签名

生成重网格后的调试项目：

```text
import_simdroid path/to/control.json
remesh_generate output/remeshed_case 100
```

输出目录包含：

- `mesh.dat`
- `control.json`
- `remesh_before.json`
- `remesh_after.json`
- `remesh_validation.json`
- `remesh_result.json`

`remesh_generate` 仅在保持性验证通过后才导出 Simdroid 项目。

## 悬臂梁示例

首个支持的生成案例为：

```text
case\cantilever beam\cantilever_beam_inp\control.json
```

使用方法：

```text
import_simdroid "case\cantilever beam\cantilever_beam_inp\control.json"
remesh_generate "result\cantilever_remesh_100x" 100
```

源模型为单部件结构化 Hex8 悬臂梁：

- `24321` 个节点
- `20000` 个 Hex8 单元
- 一个部件：`Component_1_Set-1`
- 材料：`IsotropicElastic`
- 属性类型：`SolidAdvancedProperty`
- 节点力集：`load`
- 边界节点集：`NodeValueSet_1` 到 `NodeValueSet_6`
- 外表面集：`Model_Outside_Surface`

当 `ratio=100` 时，当前结构化生成器产生：

- 约 `459` 个节点
- `200` 个 Hex8 单元
- 非空的重建节点集、单元集和表面集
- 保持不变的部件/材料/属性/类型签名
- 保持不变的载荷/约束部件分类
- 空接口列表，因为此案例没有部件间的 Contact、Tie、MPC 或 SharedNode 接口

注意：在 `remesh_after.json` 中，`summary.original_element_count` 是生成后的粗网格单元数量，`target_element_count` 是如果对已重网格的模型以相同比例再次压缩时计算得到的目标值。

## 双部件 Tie 接触案例

首个多部件生成案例为：

```text
case\double_part_hex8\doublepart_hex8_inp\control.json
```

源模型为双部件结构化 Hex8 梁，通过 Tie 接触连接：

- `48642` 个节点
- `40000` 个 Hex8 单元
- 两个部件（各 `20000` 个单元），节点不重叠
- 一个 `NodeToSurfaceTie` 接触接口（master 面 `M_SURF-1` / slave 节点 `S_SURF-1_Node`）
- 材料：`IsotropicElastic`
- 节点力集：`LOAD`（Part2 顶部）
- 边界节点集：`FIX`（Part1 底部）

当 `ratio=100` 时，当前结构化生成器产生：

- 约 `918` 个节点
- `400` 个 Hex8 单元（每部件 `200` 个）
- 保持不变的 Tie 接触接口标识
- 非空的重建节点集、单元集和表面集（含 `M_SURF-1` / `S_SURF-1` / `Model_Outside_Surface`）
- 保持不变的部件/材料/属性/类型签名
- 保持不变的载荷/约束部件分类

## 测试

专项测试位于：

```text
test\test_connection_preserving_remesher.cpp
```

测试涵盖：

- 基于合成双部件模型的共享节点接口规划；
- 基于合成双部件模型的每部件受保护实体提取（共享节点、载荷节点、约束节点），并校验计划中的 `protected_*_count` 计数；
- 双部件 Tie 接触案例的计划生成与受保护实体提取（接触节点、载荷节点、约束节点）；
- 悬臂梁案例的计划生成；
- 悬臂梁从 `20000` 个单元到 `200` 个单元的结构化 Hex8 重网格（包含详细验证）；
- 双部件 Tie 接触案例从 `40000` 个单元到 `400` 个单元的结构化 Hex8 重网格（含 Tie 接口保持与详细验证）；
- 详细验证的正常态通过测试（含非空 NodeSet、ElementSet、SurfaceSet、AppliedLoadRef、AppliedBoundaryRef 及匹配的蓝图）；
- 详细验证的空集合检测测试；
- 详细验证的蓝图缺失集合检测测试；
- 详细验证的单元数量不一致检测测试；
- 详细验证的丢失 AppliedLoadRef 检测测试；
- 详细验证的丢失 AppliedBoundaryRef 检测测试；

在 Windows MinGW/Clang 测试构建中，测试 CMake 文件现在将运行时 DLL（包括 GTest DLL）复制到测试输出目录。构建后可直接运行专项测试可执行文件：

```text
.\bin\Debug\tests\test_connection_preserving_remesher.exe
```

## 非结构化 Tet4 粗化策略

针对计划项 2（添加结构化 Hex8 之外的部件局部替换网格策略），采用**包围盒替换**——以部件包围盒生成结构化 Hex8 网格，再拆分为 Tet4，最大化复用现有 Hex8 流水线。

### 策略选择

非结构化 Tet4（`ElementType=304`）无法直接使用当前按坐标轴抽取节点的结构化粗化策略（`choose_target_cells` / `choose_axis_indices`）。经评估三个候选方案：

| 方案 | 描述 | 等效荷载 | 接口/载荷/边界重施加 | 新代码量 | 几何保真 |
|------|------|---------|---------------------|---------|---------|
| 单元采样删除 | 按比例删除内部单元 | 不需要 | 集合重建可能匹配到已删除节点 | 中 | 差（出现孔洞） |
| 包围盒替换 | 部件包围盒生成规则网格替换 | 不需要 | 复用现有最近邻匹配链路 | 小 | 低（包围盒近似） |
| 八叉树合并 | 空间八叉树细分并合并叶节点 | 不需要 | 需额外处理共享节点与 protected 节点 | 大 | 高 |

当前流水线不涉及等效荷载——载荷/约束按 NodeSet 名称重新挂载，力值不重新分配（见 `reapply_loads_and_boundaries_from_blueprint`，第 246-266 行）。因此三个方案均不需要等效荷载处理。

选择**包围盒替换**方案，理由：

- 复用现有结构化 Hex8 的节点生成、表面生成、NodeSet 最近邻匹配、SurfaceSet 质心匹配、Load/Boundary 按名重挂等全部后续链路；
- 新增代码仅"Hex8 → Tet4 拆分"一步；
- 对 debug-only 工具，几何保真非首要目标。

### 实现方案

流程：

```text
Tet4 部件包围盒
      ↓
生成结构化 Hex8 网格（复用 choose_target_cells / choose_axis_indices）
      ↓
每个 Hex8 拆分为 6 个 Tet4（新增代码仅此一步）
      ↓
后续流水线完全复用：NodeSet 最近邻 / SurfaceSet 质心匹配 / Load/Boundary 按名重挂
```

Hex8 → 6 Tet4 标准拆分表（8 个角点编号 n0..n7，与现有 Hex8 连接关系一致）：

```cpp
static const int tet_table[6][4] = {
    {0, 1, 2, 6}, {0, 2, 3, 6}, {0, 3, 7, 6},
    {0, 7, 4, 6}, {0, 4, 5, 6}, {0, 5, 1, 6}
};
```

- 节点生成逻辑不变（仍为结构化网格节点）；
- 单元类型从 308 变为 304（符合 Tet4 部件要求）；
- 表面仍用网格四边形面（对连接保持性验证足够）；
- NodeSet / SurfaceSet / Load / Boundary 重建零修改。

### 已知局限

1. **非盒状几何**：L 形、圆柱形等复杂形状的部件，包围盒包含大量"空气"，最近邻匹配可能不准。回归案例应刻意选择盒状几何以规避此问题。
2. **表面几何不精确**：四边形面与实际 Tet4 三角形面不对应。对连接保持性验证无影响（验证只检查集合非空和接口标识不变）；如未来需要精确表面，可后续增加三角形表面生成。

### 回归案例规划

按以下顺序建模三个案例，逐步覆盖计划项 2/4 需求：

**案例 A：单部件非结构化 Tet4（基础回归）**

- 几何：悬臂梁，1×1×5（与 cantilever beam 一致便于对比）
- 网格：自由四面体网格（非结构化 Tet4），约 5000~10000 单元
- 部件：1 个
- 载荷：自由端面节点力（z 方向）
- 约束：固定端面 6 自由度
- 表面集：外表面集
- 验证点：Tet4 单元数缩减、集合非空、载荷/约束覆盖、单元类型签名不变（304）

**案例 B：双部件非结构化 Tet4 + Tie 接触（接口回归）**

- 几何：两个长方体对接（各 1×1×2.5，共 1×1×5）
- 网格：每部件 Tet4 自由网格，各约 5000~10000 单元
- 部件：2 个，节点不重叠
- 接口：`NodeToSurfaceTie`（master 面 + slave 节点集），与 `double_part_hex8` 一致
- 载荷：Part2 顶部节点力
- 约束：Part1 底部固定
- 验证点：Tie 接口标识保持、两部件独立粗化、接口面集通过质心最近邻匹配重建

**案例 C（可选）：混合 Hex8 + Tet4 + Tie（高级回归）**

- 部件 1：结构化 Hex8（可复用 cantilever beam 网格）
- 部件 2：非结构化 Tet4
- 接口：Tie 接触
- 验证点：不同粗化策略的分部件调度

## 计划中的网格生成阶段

1. ~~为每个部件泛化受保护的边界/接口提取。~~（已完成，见 `extract_protected_entities` 与 `PartRemeshPlan` 中的 `protected_*_count` 字段）
2. 添加结构化 Hex8 之外的部件局部替换网格策略。（采用方案：包围盒替换，详见上文"非结构化 Tet4 粗化策略"小节）
3. ~~为 Contact、Tie、MPC 和 SharedNode 拓扑重建接口集。~~（已完成，`remesh_structured_hex8` 现支持多部件，通过表面集质心最近邻匹配保持 Tie 接口的 master 面集 / slave 节点集引用）
4. 通过解耦边界集为非结构化网格重新施加载荷和约束。
5. ~~扩展保持性验证以包含受保护集合的存在性/数量策略。~~（已完成，见 `validate_preservation_detailed`）
6. ~~添加包含 Contact/Tie/MPC/SharedNode 接口的多部件回归案例。~~（已完成，双部件 Tie 接触案例 `double_part_hex8`）

如果验证失败，实现应拒绝输出。
