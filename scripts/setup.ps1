# Development setup script for SCL (Windows PowerShell)
# Usage: .\scripts\setup.ps1

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}

Write-Host "SCL Development Setup" -ForegroundColor Cyan
Write-Host "====================="
Write-Host ""

# Check for Visual Studio / Build Tools
$vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $vsWhere) {
    $vsPath = & $vsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        Write-Host "Found Visual Studio: $vsPath" -ForegroundColor Green
    } else {
        Write-Host "Visual Studio found but C++ tools not installed." -ForegroundColor Yellow
        Write-Host "Please install 'Desktop development with C++' workload."
    }
} else {
    Write-Host "Visual Studio not found." -ForegroundColor Yellow
    Write-Host "Please install Visual Studio 2019+ with 'Desktop development with C++'"
    Write-Host "Or install Build Tools for Visual Studio"
}

# Check for CMake
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    $cmakeVersion = cmake --version | Select-Object -First 1
    Write-Host "Found CMake: $cmakeVersion" -ForegroundColor Green
} else {
    Write-Host "CMake not found." -ForegroundColor Yellow
    Write-Host "Please install CMake 3.16+ from https://cmake.org/download/"
    Write-Host "Make sure to add CMake to PATH during installation."
}

# Check for Git
if (Get-Command git -ErrorAction SilentlyContinue) {
    $gitVersion = git --version
    Write-Host "Found Git: $gitVersion" -ForegroundColor Green
} else {
    Write-Host "Git not found." -ForegroundColor Yellow
    Write-Host "Please install Git from https://git-scm.com/"
}

Write-Host ""

# Initialize submodules
Write-Host "Initializing git submodules..."
git -C $ProjectRoot submodule update --init --recursive

Write-Host ""

# Build
Write-Host "Building SCL..."
& "$ProjectRoot\scripts\build.ps1" -BuildType Release

Write-Host ""

# Run tests
Write-Host "Running tests..."
& "$ProjectRoot\scripts\test.ps1" -BuildType Release

Write-Host ""
Write-Host "Setup complete!" -ForegroundColor Green
Write-Host ""
Write-Host "Quick start:"
Write-Host "  .\build\Release\scl.exe compile examples\hello.scl hello.ch8"
Write-Host "  .\build\Release\scl.exe run hello.ch8"
Write-Host ""
Write-Host "For debugging:"
Write-Host "  .\build\Release\scl.exe debug hello.ch8"
