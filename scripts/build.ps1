# Configures and builds pluginhost.dll with CMake + Ninja.
# Tools: third_party\tools (local machine) or cmake/ninja on PATH (CI runner).
# Requires: LO_SDK, generated headers in build\gen (gen_uno_headers.ps1), an MSVC x64 environment (vcvars64).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
function Find-Tool([string]$local, [string]$name) {
    if (Test-Path $local) { return $local }
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw "Missing $name (looked in $local and PATH)"
}
$cmake = Find-Tool (Join-Path $root 'third_party\tools\cmake\cmake-4.5.1-windows-x86_64\bin\cmake.exe') 'cmake'
$ninja = Find-Tool (Join-Path $root 'third_party\tools\ninja\ninja.exe') 'ninja'
if (-not $env:LO_SDK) { throw 'LO_SDK is not set. Run scripts\env.ps1 first.' }
$build = Join-Path $root 'build'
& $cmake -S (Join-Path $root 'src') -B $build -G Ninja -DCMAKE_MAKE_PROGRAM="$ninja" -DCMAKE_BUILD_TYPE=Release `
    -DLO_SDK="$env:LO_SDK" -DPH_GEN_DIR="$build\gen"
if ($LASTEXITCODE -ne 0) { throw "cmake configure failed ($LASTEXITCODE)" }
& $cmake --build $build --config Release
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }
Write-Output "Built $build\bin\pluginhost.dll"
