@echo off
chcp 65001 >nul
setlocal enabledelayedexpansion

REM Get workspace directory from script location
for %%i in ("%~dp0.") do set "SCRIPT_DIR=%%~fi"
set "WORKSPACE=%SCRIPT_DIR%\.."
cd /d "%WORKSPACE%"
set "WORKSPACE=%cd%"

echo WORKSPACE directory: %WORKSPACE%

REM Install x64-windows version
echo Installing x64-windows version...
vcpkg install --triplet=x64-windows
if errorlevel 1 (
    echo [ERROR] x64-windows installation failed
    exit /b 1
)

REM Copy x64-windows to workspace root as backup
echo Copying x64-windows folder to workspace root...
robocopy "%WORKSPACE%\vcpkg_installed\x64-windows" "%WORKSPACE%\x64-windows" /E /R:0 /W:0 /IS /IT
if errorlevel 8 (
    echo [ERROR] Failed to copy x64-windows
    exit /b 1
)

REM Install x64-mingw-dynamic version
echo Installing x64-mingw-dynamic version...
vcpkg install --triplet=x64-mingw-dynamic
if errorlevel 1 (
    echo [ERROR] x64-mingw-dynamic installation failed
    exit /b 1
)

REM Restore x64-windows from backup
echo Restoring x64-windows folder...
robocopy "%WORKSPACE%\x64-windows" "%WORKSPACE%\vcpkg_installed\x64-windows" /E /R:0 /W:0 /IS /IT
if errorlevel 8 (
    echo [ERROR] Failed to restore x64-windows
    exit /b 1
)

REM Clean up temporary backup directory
echo Cleaning up temporary backup directory...
if exist "%WORKSPACE%\x64-windows" (
    rmdir /s /q "%WORKSPACE%\x64-windows"
    if errorlevel 1 (
        echo [WARNING] Failed to remove temporary x64-windows directory
    ) else (
        echo Temporary backup directory removed successfully
    )
)

echo All operations completed successfully!
endlocal