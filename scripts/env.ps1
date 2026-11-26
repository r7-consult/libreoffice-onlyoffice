# Loads config\versions.json into a hashtable $Versions and exports LO_SDK / LO_PROGRAM
# for the current session. Dot-source it: . scripts\env.ps1 -Profile A
param(
    [ValidateSet('A', 'B')] [string]$Profile = 'A'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Versions = Get-Content (Join-Path $root 'config\versions.json') -Raw | ConvertFrom-Json
$lo = $Versions.libreoffice.$Profile
if (-not $lo.install_path) { throw "config\versions.json: libreoffice.$Profile.install_path is empty" }
$env:LO_PROGRAM = Join-Path $lo.install_path 'program'
$env:LO_SDK = $lo.sdk_path
$env:PLUGINHOST_PROFILE_DIR = Join-Path $root ("profile-" + $Profile)
Write-Output "LO_PROGRAM=$env:LO_PROGRAM"
Write-Output "LO_SDK=$env:LO_SDK"
