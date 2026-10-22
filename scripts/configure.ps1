# Configure CMake
# Usage: .\scripts\configure.ps1 [-Preset debug]
param(
  [string]$Preset = "debug"
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $env:VCPKG_ROOT) {
  Write-Warning "VCPKG_ROOT is not set. vcpkg packages (spdlog, nlohmann-json, gtest) will not be found."
  Write-Warning "Set it, e.g. `$env:VCPKG_ROOT='C:\src\vcpkg'"
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
Write-Host "Configure OK (preset: $Preset)"
