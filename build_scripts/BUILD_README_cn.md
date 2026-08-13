# NovaFEA 构建指南

本文档介绍如何使用构建脚本来编译和运行 NovaFEA 项目。

## 前置条件

- CMake 3.19 或更高版本
- 支持 C++20 的编译器（如 Visual Studio 2019/2022 或 GCC 10+）
- vcpkg 包管理器（用于依赖管理）
- Python 3.6 或更高版本（用于运行高级构建脚本）

## 构建脚本

项目提供以下构建脚本：

### 主要构建脚本
- `build.bat` - 用于 Windows 系统
- `build.sh` - 用于 Linux/macOS 系统

### vcpkg 安装脚本
- `vcpkg_install.bat` - 用于 Windows 系统
- `vcpkg_install.sh` - 用于 Linux/macOS 系统

### Python 构建脚本
- `run_build_and_test.py` - 跨平台的高级构建脚本

## 基本用法

### 使用 Python 脚本（推荐）

Python 脚本提供实时进度显示和更好的错误处理等高级功能：

```bash
python run_build_and_test.py [选项]
```

### 使用原生脚本

#### Windows
```bash
.\build.bat [选项]
```

#### Linux/macOS
```bash
./build.sh [选项]
```

确保脚本具有执行权限：
```bash
chmod +x build.sh vcpkg_install.sh
```

## 可用选项

### Python 脚本选项
| 选项 | 描述 |
|------|------|
| `--build` | 运行构建脚本 |
| `--itest` | 运行 `test_case/` 中的集成测试（symtest） |
| `--mode` | 构建模式：debug（默认）、release、msvc 或 msvc-release |
| `--rebuild` | 在构建前清理构建目录 |

### 原生脚本选项
| 选项 | 描述 |
|------|------|
| `--debug` | 构建 GCC Debug 版本（默认） |
| `--release` | 构建 GCC Release 版本 |
| `--msvc` | 使用 MSVC 编译器构建 Debug 版本 |
| `--msvc-release` | 使用 MSVC 编译器构建 Release 版本 |
| `--clean` | 在构建前清理构建目录 |

## 构建模式说明

项目支持四种构建模式：

1. **debug**（默认）- GCC Debug 模式
   - 编译器：GCC (MinGW-w64)
   - 构建类型：Debug
   - 输出目录：`bin/Debug`

2. **release** - GCC Release 模式
   - 编译器：GCC (MinGW-w64)
   - 构建类型：Release
   - 输出目录：`bin/Release`

3. **msvc** - MSVC Debug 模式
   - 编译器：MSVC (Visual Studio)
   - 构建类型：Debug
   - 输出目录：`bin/msvc`

4. **msvc-release** - MSVC Release 模式
   - 编译器：MSVC (Visual Studio)
   - 构建类型：Release
   - 输出目录：`bin/msvc`

## 运行集成测试

```bash
python run_build_and_test.py --build --itest
```

## vcpkg 依赖安装

使用 vcpkg 安装脚本可以自动安装和配置所需依赖：

### Windows
```bash
.\vcpkg_install.bat
```

### Linux/macOS
```bash
./vcpkg_install.sh
```

## 项目依赖

项目使用以下主要依赖库：

- **spdlog** - 日志库
- **fmt** - 格式化库
- **eigen3** - 线性代数库
- **entt** - 实体组件系统库

## 示例

### 使用 Python 脚本

#### 构建 GCC Debug 版本并运行集成测试
```bash
python run_build_and_test.py --build --itest
```

#### 构建 GCC Release 版本并清理之前的构建
```bash
python run_build_and_test.py --build --mode release --rebuild
```

#### 构建 MSVC Debug 版本
```bash
python run_build_and_test.py --build --mode msvc
```

#### 构建 MSVC Release 版本
```bash
python run_build_and_test.py --build --mode msvc-release
```

### 使用原生脚本

#### 构建 GCC Debug 版本
```bash
.\build.bat
```

#### 构建 GCC Release 版本并清理之前的构建
```bash
.\build.bat --release --clean
```

#### 构建 MSVC Debug 版本
```bash
.\build.bat --msvc
```

#### 构建 MSVC Release 版本
```bash
.\build.bat --msvc-release
```

## 错误处理

### 构建日志
- 构建日志保存在 `build/log.txt` 中
- 构建状态保存在 `build/good.build` 或 `build/bad.build` 中

### 手动编译
如果出现编译错误但日志中未显示错误信息，可以尝试手动编译：

```bash
# GCC 版本
cd build/default && mingw32-make

# MSVC 版本
cd build/msvc && cmake --build . --config Debug
```

## 注意事项

- 构建完成后，可执行文件将位于相应的 `bin` 目录中
- Python 脚本提供更好的进度显示和错误处理功能，推荐使用
- MSVC 和 GCC 版本使用不同的输出目录，不会相互冲突
