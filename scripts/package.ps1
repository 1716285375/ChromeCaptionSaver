param(
    [string]$BuildDirectory = 'build/Release',
    [string]$InstalledDirectory = 'build/release/vcpkg_installed/x64-windows-static'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$packageRoot = Join-Path $root 'build/package'
New-Item -ItemType Directory -Force $packageRoot | Out-Null
$stage = Join-Path $packageRoot ([Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $stage | Out-Null

foreach ($file in @('ChromeCaptionSaver.exe', 'caption_probe.exe')) {
    Copy-Item -LiteralPath (Join-Path $BuildDirectory $file) -Destination $stage
}

$extension = Join-Path $stage 'ChatGPTBridge'
New-Item -ItemType Directory $extension | Out-Null
foreach ($name in @('manifest.json','background.js','bridge-core.js','content.js','popup.html','popup.js','popup.css','README.md')) {
    Copy-Item -LiteralPath (Join-Path 'browser-extension' $name) -Destination $extension
}
Copy-Item -LiteralPath README.md -Destination $stage

$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory $licenses | Out-Null
Copy-Item -LiteralPath external/imgui/LICENSE.txt -Destination (Join-Path $licenses 'imgui.txt')
foreach ($dependency in @('spdlog','fmt','nlohmann-json')) {
    Copy-Item -LiteralPath (Join-Path $InstalledDirectory "share/$dependency/copyright") -Destination (Join-Path $licenses "$dependency.txt")
}

$process = Start-Process -FilePath (Join-Path $stage 'ChromeCaptionSaver.exe') -ArgumentList '--smoke-test' -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw 'Packaged GUI smoke test failed' }
& (Join-Path $stage 'caption_probe.exe') --version
if ($LASTEXITCODE -ne 0) { throw 'Packaged CLI failed' }
$archive = Join-Path $packageRoot 'ChromeCaptionSaver-windows-x64.zip'
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -Force
Write-Host "Package: $archive"
