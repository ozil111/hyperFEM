@echo off
setlocal enabledelayedexpansion

echo ===== xFEM Build Script =====

:: Check if CMake is installed
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo Error: CMake not found. Please install CMake and add it to your PATH.
    exit /b 1
)

:: Check if vcpkg is available
if not exist "D:\Document\vcpkg\vcpkg.exe" (
    echo Warning: vcpkg not found at D:\Document\vcpkg\vcpkg.exe
    echo You may need to install dependencies manually.
)

:: Parse command line arguments
set BUILD_TYPE=Debug
set CLEAN=0
set BUILD_MODE=default
set PRESET_NAME=default

:parse_args
if "%~1"=="" goto :end_parse_args
if /i "%~1"=="--release" set BUILD_TYPE=Release
if /i "%~1"=="--debug" set BUILD_TYPE=Debug
if /i "%~1"=="--msvc" (
    set BUILD_MODE=msvc
    set PRESET_NAME=msvc
)
if /i "%~1"=="--msvc-release" (
    set BUILD_MODE=msvc_release
    set PRESET_NAME=msvc_release
    set BUILD_TYPE=Release
)
shift
goto :parse_args
:end_parse_args

:: Set preset name based on build mode and type
if %BUILD_MODE%==default (
    if %BUILD_TYPE%==Release (
        set PRESET_NAME=release
    ) else (
        set PRESET_NAME=default
    )
)

echo Build type: %BUILD_TYPE%
echo Build mode: %BUILD_MODE%
echo Preset: %PRESET_NAME%

:: --- FIX ---
:: Define the build directory based on the preset name for consistency.
set BUILD_DIR=build\%PRESET_NAME%

:: Create build directory if it doesn't exist (optional, as CMake does this)
if not exist "!BUILD_DIR!" (
    mkdir "!BUILD_DIR!"
)

:: Configure CMake
echo Configuring CMake...
cmake --preset %PRESET_NAME%

if errorlevel 1 (
    echo CMake configuration failed
    exit /b 1
)

:: Build
echo Building...
:: --- FIX ---
:: Use the correct BUILD_DIR variable.
if %BUILD_MODE%==msvc_release (
    cmake --build !BUILD_DIR! --config Release
) else if %BUILD_MODE%==msvc (
    cmake --build !BUILD_DIR! --config %BUILD_TYPE%
) else (
    cmake --build !BUILD_DIR!
)

if errorlevel 1 (
    echo Build failed
    exit /b 1
)

:: Tests are now handled separately by the test scripts

echo Build completed successfully
exit /b 0
