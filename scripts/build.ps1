# Build script for SCL (Windows PowerShell)
# Usage: .\scripts\build.ps1 [-BuildType debug|release] [-Clean]

param(
    [ValidateSet("debug", "release", "Debug", "Release")]
    [string]$BuildType = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# Normalize build type
$BuildType = (Get-Culture).TextInfo.ToTitleCase($BuildType.ToLower())

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
$BuildDir = Join-Path $ProjectRoot "build"

# Clean if requested
if ($Clean) {
    Write-Host "Cleaning build directory..."
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }
}

# Check for CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake 3.16+ and add to PATH."
    exit 1
}

# Initialize submodules if needed
$SDL2CMake = Join-Path $ProjectRoot "third_party\SDL2\CMakeLists.txt"
if (-not (Test-Path $SDL2CMake)) {
    Write-Host "Initializing submodules..."
    git -C $ProjectRoot submodule update --init --recursive
}

# Configure
Write-Host "Configuring ($BuildType)..."
cmake -S $ProjectRoot -B $BuildDir

# Build
Write-Host "Building..."
cmake --build $BuildDir --config $BuildType

Write-Host ""
Write-Host "Build complete!" -ForegroundColor Green
Write-Host "Executable: $BuildDir\$BuildType\scl.exe"
Write-Host ""
Write-Host "Run tests:  ctest --test-dir $BuildDir -C $BuildType"
Write-Host "Run SCL:    $BuildDir\$BuildType\scl.exe --help"
