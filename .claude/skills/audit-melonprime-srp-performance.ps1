# audit-melonprime-srp-performance.ps1
#
# Enforces MelonPrime SRP/performance contract checks for the v3 immediate plan.
# See .claude/features/melonprime-srp-performance-contract.md

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$qtSdl = Join-Path $repoRoot "src/frontend/qt_sdl"
$errors = New-Object System.Collections.Generic.List[string]

function Add-Error([string]$message) {
    $errors.Add($message) | Out-Null
}

function Get-RgLines([string]$pattern, [string]$path) {
    $result = @(rg --line-number --no-heading $pattern $path 2>$null)
    return $result
}

$screen = Join-Path $qtSdl "Screen.cpp"

$screenForbiddenIncludes = Get-RgLines '#include\s+"MelonPrime(Patch|Arm9Hook)' $screen
foreach ($line in $screenForbiddenIncludes) {
    Add-Error "Screen.cpp must not include patch/hook internals: $line"
}

$qcolorRefs = Get-RgLines 'QColorDialog::getColor|#include\s+<QColorDialog>' $qtSdl
foreach ($line in $qcolorRefs) {
    if ($line -notmatch 'MelonPrimeColorDialogPrefs\.cpp') {
        Add-Error "QColorDialog must stay in MelonPrimeColorDialogPrefs.cpp: $line"
    }
}

$rawAimRefs = Get-RgLines 'IsPlatformRawAimActive' $qtSdl
foreach ($line in $rawAimRefs) {
    if ($line -match 'Screen.cpp' -and $line -notmatch '__linux__|__APPLE__') {
        Write-Host "Raw aim reference in Screen.cpp requires manual platform-guard review: $line"
    }
}

if ($errors.Count -ne 0) {
    foreach ($e in $errors) {
        Write-Error $e
    }
    exit 1
}

Write-Host "MelonPrime SRP/performance audit passed."
