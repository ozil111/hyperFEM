#!/bin/bash

echo "===== hyperFEM Test Runner ====="

# Parse command line arguments
BUILD_TYPE="Debug"
BUILD_MODE="default"
PRESET_NAME="test-default"
TEST_TARGET="all"
PROJECT_ROOT=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --debug)
            BUILD_TYPE="Debug"
            PRESET_NAME="test-default"
            shift
            ;;
        --release)
            BUILD_TYPE="Release"
            PRESET_NAME="test-release"
            shift
            ;;
        --msvc)
            BUILD_MODE="msvc"
            PRESET_NAME="test-msvc"
            shift
            ;;
        --msvc-release)
            BUILD_MODE="msvc_release"
            PRESET_NAME="test-msvc_release"
            BUILD_TYPE="Release"
            shift
            ;;
        --test-target)
            TEST_TARGET="$2"
            shift 2
            ;;
        --project-root)
            PROJECT_ROOT="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# If PROJECT_ROOT not provided, calculate it from script location
if [ -z "$PROJECT_ROOT" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
fi

echo "Build type: $BUILD_TYPE"
echo "Preset: $PRESET_NAME"
echo "Test target: $TEST_TARGET"
echo "Project root: $PROJECT_ROOT"

# Change to project root
cd "$PROJECT_ROOT"

# Define the build directory based on the preset name (absolute path)
BUILD_DIR="$PROJECT_ROOT/build/$PRESET_NAME"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory $BUILD_DIR does not exist."
    echo "Please run the build script first with BUILD_TESTING=ON."
    exit 1
fi

echo "Current directory: $(pwd)"
echo "Build directory: $BUILD_DIR"

# Run tests using ctest
echo "Running tests..."
if [ "$TEST_TARGET" = "all" ]; then
    ctest --test-dir "$BUILD_DIR"
else
    # Run specific test executable directly
    if [ "$BUILD_MODE" = "msvc_release" ] || [ "$BUILD_MODE" = "msvc" ]; then
        "bin/msvc/tests/${TEST_TARGET}"
    else
        "bin/${BUILD_TYPE}/tests/${TEST_TARGET}"
    fi
fi

if [ $? -ne 0 ]; then
    echo "Tests failed"
    exit 1
fi

echo "Tests completed successfully"
exit 0

