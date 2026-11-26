# Configures and builds pluginhost.dll with CMake + Ninja (tools from third_party\tools).
# Requires: env.ps1 (LO_SDK), gen_uno_headers.ps1 (build\gen), Visual Studio Build Tools (vcvars64).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cmake = Join-Path $root 'third_party\tools\cmake\cmake-4.5.1-windows-x86_64\bin\cmake.exe'
$ninja = Join-Path $root 'third_party\tools\ninja\ninja.exe'
if (-not (Test-Path $cmake)) { throw "Missing $cmake" }
if (-not (Test-Path $ninja)) { throw "Missing $ninja" }
if (-not $env:LO_SDK) { throw 'LO_SDK is not set. Run scripts\env.ps1 first.' }
$build = Join-Path $root 'build'
& $cmake -S (Join-Path $root 'src') -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release -DLO_SDK="$env:LO_SDK" -DPH_GEN_DIR="$build\gen"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Output "Built $build\bin\pluginhost.dll"
