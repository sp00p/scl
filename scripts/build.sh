#!/bin/bash
# Build script for SCL (Linux/macOS)
# Usage: ./scripts/build.sh [debug|release] [clean]

set -e

BUILD_TYPE="${1:-release}"
CLEAN="${2:-}"

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"

# Normalize build type
case "$BUILD_TYPE" in
    debug|Debug)
        BUILD_TYPE="Debug"
        ;;
    release|Release|"")
        BUILD_TYPE="Release"
        ;;
    *)
        echo "Unknown build type: $BUILD_TYPE"
        echo "Usage: $0 [debug|release] [clean]"
        exit 1
        ;;
esac

# Clean if requested
if [ "$CLEAN" = "clean" ] || [ "$1" = "clean" ]; then
    echo "Cleaning build directory..."
    rm -rf "$BUILD_DIR"
fi

# Check for required tools
if ! command -v cmake &> /dev/null; then
    echo "Error: cmake not found. Please install CMake 3.16+"
    exit 1
fi

# Install dependencies hint
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Linux detected. If build fails, install: sudo apt-get install build-essential libxext-dev"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "macOS detected. Xcode command line tools required."
fi

# Initialize submodules if needed
if [ ! -f "$PROJECT_ROOT/third_party/SDL2/CMakeLists.txt" ]; then
    echo "Initializing submodules..."
    git -C "$PROJECT_ROOT" submodule update --init --recursive
fi

# Configure
echo "Configuring ($BUILD_TYPE)..."
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

# Build
echo "Building..."
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" -j "$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"

echo ""
echo "Build complete!"
echo "Executable: $BUILD_DIR/scl"
echo ""
echo "Run tests:  ctest --test-dir $BUILD_DIR -C $BUILD_TYPE"
echo "Run SCL:    $BUILD_DIR/scl --help"
