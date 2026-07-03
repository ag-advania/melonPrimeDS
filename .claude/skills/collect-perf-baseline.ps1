param(
    [string] $Label = $(if ($env:MELONPRIME_PERF_LABEL) { $env:MELONPRIME_PERF_LABEL } else { 'windows' }),
    [string] $OutDir = $(if ($env:MELONPRIME_PERF_OUT_DIR) { $env:MELONPRIME_PERF_OUT_DIR } else { 'artifacts/perf-baseline' }),
    [Parameter(Mandatory = $true, Position = 0)]
    [string] $Binary,
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]] $AppArgs
)

$ErrorActionPreference = 'Stop'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent (Split-Path -Parent $scriptDir)
if (-not [System.IO.Path]::IsPathRooted($OutDir)) {
    $OutDir = Join-Path $repoRoot $OutDir
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$log = Join-Path $OutDir "$Label-perf-$timestamp.log"
$summary = [System.IO.Path]::ChangeExtension($log, '.summary.txt')
$summarizer = Join-Path $repoRoot '.claude/skills/summarize-melonprime-perf.py'
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command python3 -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'python or python3 is required to summarize MelonPrime perf logs.'
}

Write-Host "Writing perf log: $log"
Write-Host "After the app opens, load the ROM, enter the agreed in-game scene, soak for 10 minutes, then quit cleanly."

$oldPerf = $env:MELONPRIME_PERF
$env:MELONPRIME_PERF = '1'
try {
    & $Binary @AppArgs 2>&1 | Tee-Object -FilePath $log
    $appStatus = $LASTEXITCODE
}
finally {
    $env:MELONPRIME_PERF = $oldPerf
}

& $python.Source $summarizer --markdown-platform $Label $log | Tee-Object -FilePath $summary
Write-Host "Wrote perf summary: $summary"

exit $appStatus
