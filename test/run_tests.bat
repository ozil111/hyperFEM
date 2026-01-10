@echo off
setlocal enabledelayedexpansion

echo ===== hyperFEM Test Runner =====

:: Parse command line arguments
set BUILD_TYPE=Debug
set BUILD_MODE=default
set PRESET_NAME=test-default
set TEST_TARGET=all
set PROJECT_ROOT=

:parse_args
if "%~1"=="" goto :end_parse_args
if /i "%~1"=="--debug" (
    set BUILD_TYPE=Debug
    set PRESET_NAME=test-default
)
if /i "%~1"=="--release" (
    set BUILD_TYPE=Release
    set PRESET_NAME=test-release
)
if /i "%~1"=="--msvc" (
    set BUILD_MODE=msvc
    set PRESET_NAME=test-msvc
)
if /i "%~1"=="--msvc-release" (
    set BUILD_MODE=msvc_release
    set PRESET_NAME=test-msvc_release
    set BUILD_TYPE=Release
)
if /i "%~1"=="--test-target" (
    set "TEST_TARGET=%~2"
    shift
)
if /i "%~1"=="--project-root" (
    set "PROJECT_ROOT=%~2"
    shift
)
shift
goto :parse_args
:end_parse_args

:: If PROJECT_ROOT not provided, calculate it from script location
if "%PROJECT_ROOT%"=="" (
    set SCRIPT_DIR=%~dp0
    cd /d "%SCRIPT_DIR%"
    cd ..
    set PROJECT_ROOT=%CD%
)

echo Build type: %BUILD_TYPE%
echo Preset: %PRESET_NAME%
echo Test target: %TEST_TARGET%
echo Project root: %PROJECT_ROOT%

:: Change to project root
cd /d "%PROJECT_ROOT%"

:: Define the build directory based on the preset name (relative to project root)
set BUILD_DIR=build\%PRESET_NAME%

:: Convert to absolute path for ctest (use PROJECT_ROOT for reliability)
set BUILD_DIR_ABS=%PROJECT_ROOT%\build\%PRESET_NAME%

:: Check if build directory exists (use %BUILD_DIR% not !BUILD_DIR!)
if not exist "%BUILD_DIR%" (
    echo Error: Build directory %BUILD_DIR% does not exist.
    echo Absolute path: %BUILD_DIR_ABS%
    echo Please run the build script first with BUILD_TESTING=ON.
    exit /b 1
)

:: Run tests using ctest
echo Running tests...
echo Current directory: %CD%
echo Build directory: %BUILD_DIR_ABS%
if "%TEST_TARGET%"=="all" (
    if %BUILD_MODE%==msvc_release (
        ctest --test-dir "%BUILD_DIR_ABS%" -C Release
    ) else if %BUILD_MODE%==msvc (
        ctest --test-dir "%BUILD_DIR_ABS%" -C Debug
    ) else (
        ctest --test-dir "%BUILD_DIR_ABS%"
    )
) else (
    :: Run specific test executable directly
    if %BUILD_MODE%==msvc_release (
        "bin\msvc\tests\%TEST_TARGET%.exe"
    ) else if %BUILD_MODE%==msvc (
        "bin\msvc\tests\%TEST_TARGET%.exe"
    ) else (
        "bin\%BUILD_TYPE%\tests\%TEST_TARGET%.exe"
    )
)

if errorlevel 1 (
    echo Tests failed
    exit /b 1
)

echo Tests completed successfully
exit /b 0

