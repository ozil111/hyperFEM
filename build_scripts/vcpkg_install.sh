#!/bin/bash

# Get workspace directory from script location
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
WORKSPACE="$( cd "$SCRIPT_DIR/.." && pwd )"

echo "WORKSPACE directory: $WORKSPACE"

# Install x64-windows version
echo "Installing x64-windows version..."
vcpkg install --triplet=x64-windows
if [ $? -ne 0 ]; then
    echo "[ERROR] x64-windows installation failed"
    exit 1
fi

# Copy x64-windows to workspace root as backup
echo "Copying x64-windows folder to workspace root..."
cp -r "$WORKSPACE/vcpkg_installed/x64-windows" "$WORKSPACE/x64-windows"
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to copy x64-windows"
    exit 1
fi

# Install x64-mingw-dynamic version
echo "Installing x64-mingw-dynamic version..."
vcpkg install --triplet=x64-mingw-dynamic
if [ $? -ne 0 ]; then
    echo "[ERROR] x64-mingw-dynamic installation failed"
    exit 1
fi

# Restore x64-windows from backup
echo "Restoring x64-windows folder..."
cp -r "$WORKSPACE/x64-windows"/* "$WORKSPACE/vcpkg_installed/x64-windows/"
if [ $? -ne 0 ]; then
    echo "[ERROR] Failed to restore x64-windows"
    exit 1
fi

# Clean up temporary backup directory
echo "Cleaning up temporary backup directory..."
if [ -d "$WORKSPACE/x64-windows" ]; then
    rm -rf "$WORKSPACE/x64-windows"
    if [ $? -ne 0 ]; then
        echo "[WARNING] Failed to remove temporary x64-windows directory"
    else
        echo "Temporary backup directory removed successfully"
    fi
fi

echo "All operations completed successfully!" 