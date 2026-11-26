# Builds dist\pluginhost-windows-x64.oxt from build\bin\pluginhost.dll and extension\.
# Usage: powershell -ExecutionPolicy Bypass -File scripts\pack.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\bin\pluginhost.dll'
$ext = Join-Path $root 'extension'
$stage = Join-Path $root 'build\oxt-stage'
$out = Join-Path $root 'dist\pluginhost-windows-x64.oxt'

if (-not (Test-Path $dll)) { throw "Missing $dll. Build the project first (scripts\build.ps1)." }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -Recurse -Path (Join-Path $ext '*') -Destination $stage
Copy-Item -Path $dll -Destination $stage

# Platform tag in description.xml
$desc = Join-Path $stage 'description.xml'
(Get-Content $desc -Raw).Replace('UPDATED_SUPPORTED_PLATFORM', 'windows_x86_64') | Set-Content -Path $desc -Encoding utf8

if (Test-Path $out) { Remove-Item -Force $out }
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath ($out + '.zip') -Force
Rename-Item -Path ($out + '.zip') -NewName (Split-Path $out -Leaf)
Get-Item $out | Select-Object FullName, Length
