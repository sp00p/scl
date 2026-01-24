# Test script for SCL (Windows PowerShell)
# Usage: .\scripts\test.ps1 [-BuildType debug|release]

param(
    [ValidateSet("debug", "release", "Debug", "Release")]
    [string]$BuildType = "Release"
)

$ErrorActionPreference = "Stop"

$BuildType = (Get-Culture).TextInfo.ToTitleCase($BuildType.ToLower())

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
}
$BuildDir = Join-Path $ProjectRoot "build"

if (-not (Test-Path $BuildDir)) {
    Write-Error "Build directory not found. Run .\scripts\build.ps1 first."
    exit 1
}

Write-Host "Running tests ($BuildType)..."
ctest --test-dir $BuildDir -C $BuildType --output-on-failure

Write-Host ""
Write-Host "All tests passed!" -ForegroundColor Green
