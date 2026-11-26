# Runs every stage test against the profile given by -Profile (A or B).
# Usage: . scripts\env.ps1 -Profile A ; powershell -File scripts\run_all.ps1 -Profile A
param([ValidateSet('A', 'B')] [string]$Profile = 'A')
$ErrorActionPreference = 'Continue'
$root = Split-Path -Parent $PSScriptRoot
$py = Join-Path $env:LO_PROGRAM 'python.exe'
$env:PLUGINHOST_LOG = Join-Path $root ("logs\run-" + $Profile + "-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".log")
$summary = @()
foreach ($t in Get-ChildItem (Join-Path $root 'tests') -Filter 'test_stage*.py' | Sort-Object Name) {
    & $py $t.FullName
    $summary += ($t.Name + " exit=" + $LASTEXITCODE)
}
$summary | ForEach-Object { Write-Output $_ }
Write-Output ("log: " + $env:PLUGINHOST_LOG)
