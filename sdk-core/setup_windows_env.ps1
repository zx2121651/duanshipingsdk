# setup_windows_env.ps1
# Script to setup vcpkg, install ANGLE, and configure CMake for testing on Windows.

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host "========================================================" -ForegroundColor Cyan
Write-Host " ShortVideo SDK - Windows Render Test Environment Setup" -ForegroundColor Cyan
Write-Host "========================================================" -ForegroundColor Cyan

# 1. Setup vcpkg if not present
$VcpkgDir = Join-Path $ScriptDir "vcpkg"
if (-not (Test-Path $VcpkgDir)) {
    Write-Host "`n[1/4] Cloning vcpkg..." -ForegroundColor Yellow
    git clone https://github.com/microsoft/vcpkg.git

    Write-Host "`n[2/4] Bootstrapping vcpkg..." -ForegroundColor Yellow
    Set-Location $VcpkgDir
    .\bootstrap-vcpkg.bat -disableMetrics
    Set-Location $ScriptDir
} else {
    Write-Host "`n[1/2] vcpkg found at $VcpkgDir. Skipping clone." -ForegroundColor Green
}

# 2. Install ANGLE library via vcpkg
Write-Host "`n[3/4] Installing ANGLE (OpenGL ES implementation for Windows)..." -ForegroundColor Yellow
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"
& $VcpkgExe install angle:x64-windows

# 3. Configure CMake with vcpkg toolchain
Write-Host "`n[4/4] Configuring project with CMake..." -ForegroundColor Yellow
$BuildDir = Join-Path $ScriptDir "build"
if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Set-Location $BuildDir
$ToolchainFile = Join-Path $VcpkgDir "scripts/buildsystems/vcpkg.cmake"

# Use CMake to generate the build system
cmake .. "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile" -A x64

if ($LASTEXITCODE -eq 0) {
    Write-Host "`n========================================================" -ForegroundColor Green
    Write-Host " Setup Complete!" -ForegroundColor Green
    Write-Host " You can now open the Visual Studio solution in 'build\' folder"
    Write-Host " or build it directly using:"
    Write-Host "   cmake --build . --config Release"
    Write-Host "========================================================" -ForegroundColor Green
} else {
    Write-Host "`n[ERROR] CMake configuration failed." -ForegroundColor Red
}

Set-Location $ScriptDir
