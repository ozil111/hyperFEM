# xFEM Build Guide

This document describes how to use build scripts to compile and run the xFEM project.

## Prerequisites

- CMake 3.19 or higher
- C++20 compatible compiler (such as Visual Studio 2019/2022 or GCC 10+)
- vcpkg package manager (for dependency management)
- Python 3.6 or higher (for running advanced build scripts)

## Build Scripts

The project provides the following build scripts:

### Main Build Scripts
- `build.bat` - for Windows systems
- `build.sh` - for Linux/macOS systems

### Test Runner Scripts
- `run_test.bat` - for Windows systems
- `run_test.sh` - for Linux/macOS systems

### vcpkg Installation Scripts
- `vcpkg_install.bat` - for Windows systems
- `vcpkg_install.sh` - for Linux/macOS systems

### Python Build Script
- `run_build_and_test.py` - cross-platform advanced build script

## Basic Usage

### Using Python Script (Recommended)

The Python script provides advanced features including real-time progress display and better error handling:

```bash
python run_build_and_test.py [options]
```

### Using Native Scripts

#### Windows
```bash
.\build.bat [options]
```

#### Linux/macOS
```bash
./build.sh [options]
```

Ensure scripts have execution permissions:
```bash
chmod +x build.sh run_test.sh vcpkg_install.sh
```

## Available Options

### Python Script Options
| Option | Description |
|--------|-------------|
| `--build` | Run the build script |
| `--test` | Run the test script |
| `--mode` | Build mode: debug (default), release, msvc, or msvc-release |
| `--rebuild` | Clean build directories before building |
| `--skip-tests` | Skip building tests |
| `--no-ignore-test-errors` | Do not ignore test build errors |
| `--test-target` | Specify test target to build and run (default: 'all') |

### Native Script Options
| Option | Description |
|--------|-------------|
| `--debug` | Build GCC Debug version (default) |
| `--release` | Build GCC Release version |
| `--msvc` | Build Debug version using MSVC compiler |
| `--msvc-release` | Build Release version using MSVC compiler |
| `--clean` | Clean build directories before building |
| `--skip-tests` | Skip building tests |
| `--no-ignore-test-errors` | Do not ignore test build errors |
| `--test <test_name>` | Build and run specified test |

## Build Mode Description

The project supports four build modes:

1. **debug** (default) - GCC Debug mode
   - Compiler: GCC (MinGW-w64)
   - Build type: Debug
   - Output directory: `bin/Debug`

2. **release** - GCC Release mode
   - Compiler: GCC (MinGW-w64)
   - Build type: Release
   - Output directory: `bin/Release`

3. **msvc** - MSVC Debug mode
   - Compiler: MSVC (Visual Studio)
   - Build type: Debug
   - Output directory: `bin/msvc`

4. **msvc-release** - MSVC Release mode
   - Compiler: MSVC (Visual Studio)
   - Build type: Release
   - Output directory: `bin/msvc`

## Building and Running Specific Unit Tests

### Using Python Script
```bash
python run_build_and_test.py --build --test --test-target <test_name>
```

### Using Native Scripts

#### Windows Example
```bash
.\build.bat --test <test_name>
```

#### Linux/macOS Example
```bash
./build.sh --test <test_name>
```

## Running Tests Separately (Without Recompiling)

### Using Python Script
```bash
python run_build_and_test.py --test --test-target <test_name>
```

### Using Native Scripts

#### Windows
```bash
.\run_test.bat --test <test_name> [--debug|--release|--msvc|--msvc-release]
```

#### Linux/macOS
```bash
./run_test.sh --test <test_name> [--debug|--release|--msvc|--msvc-release]
```

## vcpkg Dependency Installation

Use vcpkg installation scripts to automatically install and configure required dependencies:

### Windows
```bash
.\vcpkg_install.bat
```

### Linux/macOS
```bash
./vcpkg_install.sh
```

## Project Dependencies

The project uses the following main dependency libraries:

- **spdlog** - Logging library
- **fmt** - Formatting library
- **eigen3** - Linear algebra library
- **entt** - Entity Component System library
- **gtest** - Testing framework

## Examples

### Using Python Script

#### Build GCC Debug version and run all tests
```bash
python run_build_and_test.py --build --test
```

#### Build GCC Release version and clean previous build
```bash
python run_build_and_test.py --build --mode release --rebuild
```

#### Build MSVC Debug version
```bash
python run_build_and_test.py --build --mode msvc
```

#### Build MSVC Release version
```bash
python run_build_and_test.py --build --mode msvc-release
```

#### Build main program only, skip tests
```bash
python run_build_and_test.py --build --skip-tests
```

#### Run specific test
```bash
python run_build_and_test.py --test --test-target <test_name>
```

### Using Native Scripts

#### Build GCC Debug version
```bash
.\build.bat
```

#### Build GCC Release version and clean previous build
```bash
.\build.bat --release --clean
```

#### Build MSVC Debug version
```bash
.\build.bat --msvc
```

#### Build MSVC Release version
```bash
.\build.bat --msvc-release
```

#### Build main program only, skip tests
```bash
.\build.bat --skip-tests
```

#### Run already built tests without recompiling
```bash
.\run_test.bat --test <test_name>
```

## Error Handling

### Build Logs
- Build logs are saved in `build/log.txt`
- Build status is saved in `build/good.build` or `build/bad.build`

### Manual Compilation
If compilation errors occur but are not shown in the logs, try manual compilation:

```bash
# GCC version
cd build/default && mingw32-make

# MSVC version
cd build/msvc && cmake --build . --config Debug
```

## Notes

- Build scripts ignore test build errors by default, as long as the main program builds successfully
- Use the `--no-ignore-test-errors` option to stop the build process when test builds fail
- After building, executables will be located in the appropriate `bin` directories
- If test builds fail, you may need to modify test code or CMake configuration
- Python scripts provide better progress display and error handling features, recommended for use
- MSVC and GCC versions use different output directories and won't conflict with each other 