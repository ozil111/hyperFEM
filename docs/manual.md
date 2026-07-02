# NovaFEA 用户手册

NovaFEA 是一个现代、高性能的有限元分析引擎。本手册介绍可执行程序 `NovaFEA_app` 的命令行用法和三种运行模式。

---

## 1. 概述

`NovaFEA_app` 支持三种运行模式，按优先级依次为：

| 优先级 | 模式 | 触发参数 | 适用场景 |
|:------:|------|----------|----------|
| 1 | 脚本模式 | `--script` / `-s <file>` | 回归测试、批量操作序列、操作文档化 |
| 2 | 批处理模式 | `--input-file` / `-i <file>` | 直接求解、解析输入文件并运行求解器 |
| 3 | 交互模式 | 无上述参数 | 手动交互探索、使用 FTXUI 终端界面 |

若同时指定了 `--script` 和 `--input-file`，脚本模式优先执行。

---

## 2. 命令行参数

```
Usage: NovaFEA_app [options]
```

| 参数 | 简写 | 说明 |
|------|------|------|
| `--input-file` | `-i` | 指定输入文件（`.xfem`、`.json`、`.jsonc`），进入批处理模式 |
| `--export` | `-e` | 导出预处理后的网格（`.xfem` 或 `.jsonc`） |
| `--output` | `-o` | 指定结果输出文件（`.vtu`） |
| `--output-file` | - | [已废弃] `--export` 的别名 |
| `--script` | `-s` | 指定脚本文件，进入脚本模式 |
| `--log-level` | `-l` | 设置日志级别（trace/debug/info/warn/error/critical） |
| `--log-directory` | `-d` | 设置日志文件路径 |
| `--help` | `-h` | 显示帮助信息 |

---

## 3. 批处理模式

直接解析输入文件并运行求解器，适合自动化计算流程。

### 用法

```bash
NovaFEA_app -i case/model.jsonc -e case/output.xfem -o case/result.vtu
```

### 执行流程

1. 根据文件扩展名自动选择解析器：
   - `.json` / `.jsonc` → `JsonParser`
   - `.xfem` → `FemParser`（传统格式）
2. 解析成功后，根据分析类型自动调用求解器：
   - `explicit` → 显式求解器
   - `static` → 线性静力求解器
3. 若指定了 `--export`，将网格导出为 `.xfem` 或 `.jsonc`
4. 若指定了 `--output`，将结果导出为 `.vtu`

### 支持的输入格式

| 格式 | 扩展名 | 说明 |
|------|--------|------|
| JSON | `.json` / `.jsonc` | 推荐格式，支持注释 |
| XFEM | `.xfem` | 传统文本格式，向后兼容 |

---

## 4. 脚本模式（Script Mode）

脚本模式从文件中逐行读取命令并执行，适合批量操作、回归测试和操作流程文档化。

### 用法

```bash
NovaFEA_app --script <脚本文件路径>
# 或简写
NovaFEA_app -s <脚本文件路径>
```

### 脚本语法

- **每行一条命令**，与交互模式输入格式完全一致
- **空行**自动跳过
- **注释行**：以 `#` 开头的行（允许前导空白）会被跳过
- 遇到 `quit` 或 `exit` 命令时停止执行（`session.is_running` 置为 `false`）
- 到达文件末尾时自然结束

### 脚本示例

```
# basic_mesh_workflow.nova
# 基本网格操作流程示例

# 1. 导入 Abaqus 模型
import test/cases/abaqus_T01.inp
info

# 2. 添加节点和单元
node_add 10.0 20.0 30.0
elem_add 304 1 2 3 9

# 3. 集合操作
list_sets
set_addnode myset 1 2 9
set_info myset

# 4. 修改材料参数
set_material 1 LinearElastic E 210000
set_material 1 LinearElastic nu 0.30

# 5. 导出结果
export_abaqus output/basic_mesh_out.inp
quit
```

运行：

```bash
NovaFEA_app -s basic_mesh_workflow.nova
```

### 脚本模式特性

- **日志可见**：每条命令执行前会打印 `[script:行号]> 命令内容`，便于定位
- **正常退出**：`quit` / `exit` 或文件末尾都会正常退出
- **复用交互命令**：脚本中可使用全部交互模式命令，详见 [交互模式使用手册](./交互模式使用手册.md)

### 脚本模式用于回归测试

脚本模式天然适合端到端回归测试。典型流程：

1. 编写 `.nova` 脚本描述操作序列
2. 脚本末尾用 `export_abaqus` / `save` / `export_simdroid` 导出结果文件
3. 将导出文件与 golden 参考文件对比

```
# regression_test.nova
import test/cases/abaqus_T01.inp
node_add 10.0 20.0 30.0
elem_delete 5
export_abaqus test/output/regression_out.inp
```

```bash
NovaFEA_app -s regression_test.nova
diff test/output/regression_out.inp test/golden/regression_out.inp
```

---

## 5. 交互模式

不带 `--input-file` 或 `--script` 参数启动时进入交互模式，提供 FTXUI 终端界面。

### 用法

```bash
NovaFEA_app
```

启动后显示 `NovaFEA>` 提示符，逐行输入命令执行。支持网格导入导出、节点/单元增删改、集合操作、材料/截面参数编辑、拓扑分析、部件检查等全部交互命令。

完整命令参考请见 [交互模式使用手册](./交互模式使用手册.md)。

---

## 6. 命令速查

| 分类 | 命令 |
|------|------|
| 网格导入/导出 | `import`、`import_simdroid`、`export_simdroid`、`export_abaqus`、`save`、`json_apply` |
| 信息与拓扑 | `info`、`build_topology`、`list_bodies`、`show_body` |
| 部件与图分析 | `list_parts`、`delete_part`、`graph`、`remesh_plan`、`remesh_generate` |
| 约束检查 | `validate_constraints`、`list_constraint_warnings` |
| 节点操作 | `node`、`node_add`、`node_move`、`node_delete`、`list_nodes` |
| 单元操作 | `elem`、`elem_add`、`elem_delete`、`list_elements` |
| 集合操作 | `list_sets`、`set_info`、`set_addnode`、`set_addelem`、`set_removenode`、`set_removeelem` |
| 材料编辑 | `set_material` |
| 截面编辑 | `set_section` |
| 检查面板 | `panel` |
| 其他 | `help`、`quit` / `exit` |

> 各命令的详细参数说明请参考 [交互模式使用手册](./交互模式使用手册.md)。

---

## 7. 日志配置

NovaFEA 使用 `spdlog` 同时输出日志到文件和控制台。

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--log-level` | `info` | 日志级别：trace/debug/info/warn/error/critical |
| `--log-directory` | `logs/NovaFEA.log` | 日志文件路径 |

日志格式：`[YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message`

---

## 8. 典型使用场景

### 场景 1：求解有限元算例

```bash
NovaFEA_app -i case/beam_analysis.jsonc -o result/beam.vtu
```

### 场景 2：网格格式转换

```bash
NovaFEA_app -i case/model.inp -e case/model.xfem
```

### 场景 3：批量修改并导出（脚本模式）

```bash
NovaFEA_app -s scripts/modify_and_export.nova
```

### 场景 4：交互式探索

```bash
NovaFEA_app
NovaFEA> import case/model.jsonc
NovaFEA> info
NovaFEA> panel node 1
NovaFEA> quit
```
