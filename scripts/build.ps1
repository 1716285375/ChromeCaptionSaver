# Build the project
# Usage: .\scripts\build.ps1 [-Preset debug] [-Test]
param(
  [string]$Preset = "debug",
  [switch]$Test
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

if ($Test) {
  $buildDir = "build\$Preset"
  ctest --test-dir $buildDir -C $(if ($Preset -eq "release") { "Release" } else { "Debug" }) --output-on-failure
  if ($LASTEXITCODE -ne 0) { throw "ctest failed" }
}
Write-Host "Build OK (preset: $Preset)"
