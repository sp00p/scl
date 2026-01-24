#!/bin/bash
# Development setup script for SCL (Linux/macOS)
# Usage: ./scripts/setup.sh

set -e

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

echo "SCL Development Setup"
echo "====================="
echo ""

# Check OS and install dependencies
if [[ "$OSTYPE" == "linux-gnu"* ]]; then
    echo "Detected: Linux"
    echo ""
    if command -v apt-get &> /dev/null; then
        echo "Installing dependencies (requires sudo)..."
        sudo apt-get update
        sudo apt-get install -y build-essential cmake git libxext-dev
    elif command -v dnf &> /dev/null; then
        echo "Installing dependencies (requires sudo)..."
        sudo dnf install -y gcc-c++ cmake git libXext-devel
    elif command -v pacman &> /dev/null; then
        echo "Installing dependencies (requires sudo)..."
        sudo pacman -S --noconfirm base-devel cmake git libxext
    else
        echo "Could not detect package manager."
        echo "Please install: C++ compiler, CMake 3.16+, git, libxext-dev"
    fi
elif [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Detected: macOS"
    echo ""
    if ! xcode-select -p &> /dev/null; then
        echo "Installing Xcode command line tools..."
        xcode-select --install
        echo "Please re-run this script after installation completes."
        exit 0
    fi
    if command -v brew &> /dev/null; then
        echo "Installing CMake via Homebrew..."
        brew install cmake
    else
        echo "Homebrew not found. Please install CMake manually."
    fi
else
    echo "Unknown OS: $OSTYPE"
    echo "Please install: C++ compiler, CMake 3.16+, git"
fi

echo ""

# Initialize submodules
echo "Initializing git submodules..."
git -C "$PROJECT_ROOT" submodule update --init --recursive

echo ""

# Build
echo "Building SCL..."
"$PROJECT_ROOT/scripts/build.sh" release

echo ""

# Run tests
echo "Running tests..."
"$PROJECT_ROOT/scripts/test.sh" release

echo ""
echo "Setup complete!"
echo ""
echo "Quick start:"
echo "  ./build/scl compile examples/hello.scl hello.ch8"
echo "  ./build/scl run hello.ch8"
echo ""
echo "For debugging:"
echo "  ./build/scl debug hello.ch8"
