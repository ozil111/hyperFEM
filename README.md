# NovaFEA

[![License](https://img.shields.io/badge/license-MPL--2.0-blue.svg)](LICENSE)
[![Language](https://img.shields.io/badge/language-C%2B%2B%20%26%20Python-orange.svg)]()

**NovaFEA** 是一个从零开始构建的、现代、高性能的有限元分析（FEA）引擎。它旨在融合传统高性能计算与现代 AI 驱动的开发范式，探索从符号数学定义到高效计算内核的自动化演进路径。

## 核心特性

- **高性能 C++ 核心**：采用现代 C++ (17/20) 编写，利用 CMake 与 Vcpkg 进行构建和依赖管理。
- **声明式输入文件**：支持 `.jsonc`格式，使算例定义清晰且具有注释支持。
- **符号代码生成 (Symbolic Code Generation)**：核心创新点，利用 Python/SymPy 自动生成高度优化的 C++、CUDA 与 JAX 计算内核。
- **Python/JAX 生态**：利用 JAX 进行快速算法原型设计和求解器验证。

## 安装与构建

项目使用 `vcpkg`管理 C++ 依赖，并提供了一套脚本来简化环境配置。

### 0. 获取子模块

```bash
git submodule update --init --recursive
```

后续更新子模块到各自分支最新提交：

```bash
git submodule update --remote --recursive
```

### 1. 依赖管理 (Vcpkg)

在首次构建前，需安装 C++ 依赖项：

- **Windows**: 运行 `.\build_scripts\vcpkg_install.bat`
- **Linux/macOS**: 运行 `./build_scripts/vcpkg_install.sh`

该脚本会自动下载并引导 vcpkg，随后安装 `vcpkg.json`中定义的所有库。

### 2. 自动化构建与测试工具

我们推荐使用根目录下的 `run_build_and_test.py`脚本，它整合了构建、单元测试和集成测试流程。

**基本用法：**

```bash
python run_build_and_test.py [OPTIONS]
```

**常用选项：**

- `--build`: 执行 CMake 构建流程。
- `--test`: 运行 C++ 单元测试（位于 `test/`目录）。
- `--itest`: 运行集成测试（位于 `test_case/`目录）。
- `--mode {debug, release, msvc, msvc-release}`: 指定构建模式（默认为 `debug`）。
- `--rebuild`: 在构建前清理之前的构建目录。

**示例：**

```bash
# 构建并运行所有测试
python run_build_and_test.py --build --test --itest --mode release
```

## 测试框架

项目采用多层次的测试策略确保代码质量。

### 单元测试 (C++)

位于 `test/`目录，主要验证 C++ 核心组件的逻辑正确性。

### 集成测试 (cli-test-framework)

位于 `test_case/`目录，基于 cli-test-framework 开发。

该框架通过 `test_case/test_cases.json`定义测试矩阵，自动运行 `NovaFEA`命令行工具并验证输出结果。

- **运行方式**：

```bash
python run_build_and_test.py --itest
```

- **手动运行**（需激活 Python 环境）：

```bash
cd test_case
python test.py
```

测试结果将自动生成报告并保存于 `test_case/test_report.txt`。

## 目录结构

```markdown
.
├── system/             # C++ 核心系统（求解器、装配、IO 等）
├── data_center/        # C++ 核心数据（网格、组件、上下文等）
├── extern/fea_codegen/ # FEA 代码生成器（git submodule）
├── docs/demos/jax/     # JAX 示例脚本
├── case/               # 仿真算例输入文件
├── build_scripts/      # 基础构建脚本 (vcpkg_install, build)
├── test/               # C++ 单元测试
├── test_case/          # 基于 cli-test-framework 的集成测试
├── run_build_and_test.py # 统一工作流脚本
└── vcpkg.json          # C++ 依赖定义
```

## 许可

本项目采用[Mozilla Public License 2.0 (MPL-2.0)](LICENSE) 许可证
