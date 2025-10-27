# vcpkg Installation Scripts

This directory contains scripts to install vcpkg dependencies for the xFEM project.

## Scripts

### Windows
- **`vcpkg_install.bat`** - Batch script for Windows
  - Uses UTF-8 encoding to avoid display issues
  - Automatically cleans up temporary files
  - No admin privileges required

### Linux/macOS
- **`vcpkg_install.sh`** - Shell script for Linux/macOS
  - Make executable: `chmod +x vcpkg_install.sh`
  - Run: `./vcpkg_install.sh`

## Installation Process

Both scripts perform the same installation process:

1. Install x64-windows triplet dependencies
2. Backup x64-windows folder to workspace root
3. Install x64-mingw-dynamic triplet dependencies  
4. Restore x64-windows folder from backup
5. Clean up temporary backup directory

## Usage

### Windows
```cmd
.\build_scripts\vcpkg_install.bat
```

### Linux/macOS
```bash
./build_scripts/vcpkg_install.sh
```

## Troubleshooting

- **Encoding issues**: Scripts now use English text to avoid encoding problems
- **Permission errors**: Removed `/COPYALL` parameter to avoid admin requirements
- **Cleanup**: Temporary `x64-windows` directory is automatically removed after installation 