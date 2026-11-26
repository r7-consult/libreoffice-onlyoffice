# Installs dist\pluginhost-windows-x64.oxt into an isolated LibreOffice profile.
# Usage: . scripts\env.ps1 -Profile A ; powershell -File scripts\install.ps1 -Profile A
param([ValidateSet('A', 'B')] [string]$Profile = 'A')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$oxt = Join-Path $root 'dist\pluginhost-windows-x64.oxt'
$unopkg = Join-Path $env:LO_PROGRAM 'unopkg.exe'
$profileDir = Join-Path $root ("profile-" + $Profile)
if (-not (Test-Path $oxt)) { throw "Missing $oxt. Run scripts\pack.ps1 first." }
if (-not (Test-Path $unopkg)) { throw "Missing $unopkg. Run scripts\env.ps1 first." }
$userUrl = 'file:///' + ($profileDir -replace '\\', '/')
& $unopkg add --shared --force "-env:UserInstallation=$userUrl" $oxt
if ($LASTEXITCODE -ne 0) { throw "unopkg add failed with exit code $LASTEXITCODE" }
& $unopkg list "-env:UserInstallation=$userUrl"
