# Generates UNO C++ headers (com/sun/star/...) with cppumaker.
# On Windows the type libraries ship with the office, not the SDK:
#   <office>\program\types.rdb and <office>\program\types\offapi.rdb
# cppumaker.exe (in the SDK) loads URE DLLs from <office>\program, so that folder goes on PATH.
# Needs LO_SDK and LO_PROGRAM (scripts\env.ps1 or the CI workflow). Output: build\gen
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
if (-not $env:LO_SDK) { throw 'LO_SDK is not set. Run scripts\env.ps1 first.' }
if (-not $env:LO_PROGRAM) { throw 'LO_PROGRAM is not set. Run scripts\env.ps1 first.' }
$cppumaker = Join-Path $env:LO_SDK 'bin\cppumaker.exe'
$types = Join-Path $env:LO_PROGRAM 'types.rdb'
$offapi = Join-Path $env:LO_PROGRAM 'types\offapi.rdb'
foreach ($p in @($cppumaker, $types, $offapi)) {
    if (-not (Test-Path $p)) { throw "Missing file: $p" }
}
$env:PATH = $env:LO_PROGRAM + ';' + $env:PATH
$gen = Join-Path $root 'build\gen'
New-Item -ItemType Directory -Force -Path $gen | Out-Null
# All types (no -T list): slower, but no type list to drift from the #includes.
& $cppumaker -Gc -O $gen $types $offapi
if ($LASTEXITCODE -ne 0) { throw "cppumaker failed with exit code $LASTEXITCODE" }
Write-Output "Generated headers in $gen"
