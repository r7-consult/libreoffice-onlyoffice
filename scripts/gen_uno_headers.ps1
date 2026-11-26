# Generates UNO C++ headers for the types the plugin uses, from the SDK's offapi.rdb.
# Requires env.ps1 to have set LO_SDK. Output: build\gen
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = $env:LO_SDK
if (-not $sdk) { throw 'LO_SDK is not set. Run scripts\env.ps1 first.' }
$cppumaker = Join-Path $sdk 'bin\cppumaker.exe'
$types = Join-Path $sdk 'ure-link-lib\types.rdb'
$offapi = Join-Path $sdk 'ure-link-lib\types\offapi.rdb'
foreach ($p in @($cppumaker, $types, $offapi)) {
    if (-not (Test-Path $p)) { throw "Missing SDK file: $p" }
}
$gen = Join-Path $root 'build\gen'
New-Item -ItemType Directory -Force -Path $gen | Out-Null
& $cppumaker -Gc -BUCR -O $gen $types $offapi
if ($LASTEXITCODE -ne 0) { throw "cppumaker failed with exit code $LASTEXITCODE" }
Write-Output "Generated headers in $gen"
