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
2. 针对单部件调试场景的有限结构化 Hex8 网格生成层。

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
  针对一个结构化 Hex8 部件执行受限的物理网格替换。
- `remesh_generate [output_dir] [ratio]`
  交互式命令，输出重网格后的 Simdroid 项目和验证产物。

此生成器刻意保持功能精简，目前支持：

- 恰好一个 `SimdroidPart`；
- 恰好一种单元类型 Hex8（`ElementType=308`）；
- 节点坐标构成完整的三维结构化网格；
- 通过最近粗网格节点重建节点集；
- 通过分配新的粗单元来重建单元集；
- 从新的外部粗面重建表面集；
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

## 测试

专项测试位于：

```text
test\test_connection_preserving_remesher.cpp
```

测试涵盖：

- 基于合成双部件模型的共享节点接口规划；
- 基于合成双部件模型的每部件受保护实体提取（共享节点、载荷节点、约束节点），并校验计划中的 `protected_*_count` 计数；
- 悬臂梁案例的计划生成；
- 悬臂梁从 `20000` 个单元到 `200` 个单元的结构化 Hex8 重网格（包含详细验证）；
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

## 计划中的网格生成阶段

1. ~~为每个部件泛化受保护的边界/接口提取。~~（已完成，见 `extract_protected_entities` 与 `PartRemeshPlan` 中的 `protected_*_count` 字段）
2. 添加结构化 Hex8 之外的部件局部替换网格策略。
3. 为 Contact、Tie、MPC 和 SharedNode 拓扑重建接口集。
4. 通过解耦边界集为非结构化网格重新施加载荷和约束。
5. ~~扩展保持性验证以包含受保护集合的存在性/数量策略。~~（已完成，见 `validate_preservation_detailed`）
6. 添加包含 Contact/Tie/MPC/SharedNode 接口的多部件回归案例。

如果验证失败，实现应拒绝输出。
