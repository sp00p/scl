#!/bin/bash
# Test script for SCL (Linux/macOS)
# Usage: ./scripts/test.sh [debug|release]

set -e

BUILD_TYPE="${1:-release}"

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
esac

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Run ./scripts/build.sh first."
    exit 1
fi

echo "Running tests ($BUILD_TYPE)..."
ctest --test-dir "$BUILD_DIR" -C "$BUILD_TYPE" --output-on-failure

echo ""
echo "All tests passed!"
