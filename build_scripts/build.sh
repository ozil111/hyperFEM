#!/bin/bash

echo "===== hyperFEM Build Script ====="

# Check if CMake is installed
if ! command -v cmake &> /dev/null; then
    echo "Error: CMake not found. Please install CMake and add it to your PATH."
    exit 1
fi

# Check if vcpkg is available
if [ ! -f "/path/to/vcpkg/vcpkg" ]; then
    echo "Warning: vcpkg not found. You may need to install dependencies manually."
fi

# Parse command line arguments
BUILD_TYPE="Debug"
BUILD_MODE="default"
PRESET_NAME="default"

while [[ $# -gt 0 ]]; do
    case $1 in
        --release)
            BUILD_TYPE="Release"
            shift
            ;;
        --debug)
            BUILD_TYPE="Debug"
            shift
            ;;
        --msvc)
            BUILD_MODE="msvc"
            PRESET_NAME="msvc"
            shift
            ;;
        --msvc-release)
            BUILD_MODE="msvc_release"
            PRESET_NAME="msvc_release"
            BUILD_TYPE="Release"
            shift
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Set preset name based on build mode and type
if [ "$BUILD_MODE" = "default" ]; then
    if [ "$BUILD_TYPE" = "Release" ]; then
        PRESET_NAME="release"
    else
        PRESET_NAME="default"
    fi
fi

echo "Build type: $BUILD_TYPE"
echo "Build mode: $BUILD_MODE"
echo "Preset: $PRESET_NAME"

# Define the build directory based on the preset name for consistency
BUILD_DIR="build/$PRESET_NAME"

# Create build directory if it doesn't exist
if [ ! -d "$BUILD_DIR" ]; then
    mkdir -p "$BUILD_DIR"
fi

# Configure CMake
echo "Configuring CMake..."
cmake --preset "$PRESET_NAME"

if [ $? -ne 0 ]; then
    echo "CMake configuration failed"
    exit 1
fi

# Build
echo "Building..."
if [ "$BUILD_MODE" = "msvc_release" ]; then
    cmake --build "$BUILD_DIR" --config Release
elif [ "$BUILD_MODE" = "msvc" ]; then
    cmake --build "$BUILD_DIR" --config "$BUILD_TYPE"
else
    cmake --build "$BUILD_DIR"
fi

if [ $? -ne 0 ]; then
    echo "Build failed"
    exit 1
fi

# Tests are now handled separately by the test scripts

echo "Build completed successfully"
exit 0 