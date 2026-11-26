# Removes the extension from an isolated profile. Never touches the system profile.
param([ValidateSet('A', 'B')] [string]$Profile = 'A')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$profileDir = Join-Path $root ("profile-" + $Profile)
$unopkg = Join-Path $env:LO_PROGRAM 'unopkg.exe'
$userUrl = 'file:///' + ($profileDir -replace '\\', '/')
& $unopkg remove --shared "-env:UserInstallation=$userUrl" org.example.pluginhost
