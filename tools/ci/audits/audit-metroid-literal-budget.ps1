# audit-metroid-literal-budget.ps1
#
# Counts quoted "Metroid.*" literals under src/frontend/qt_sdl, excluding the
# canonical owner files that intentionally define config key strings. The budget
# is a ratchet: Phase 0 sets it to the current count, and later refactor commits
# lower it as literals move to MP_HUD_PROP_KEY_* / CfgKey::* references.

param(
    [int]$Budget = 1,
    [int]$MaxList = 40,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$qtSdl = Join-Path $repoRoot 'src/frontend/qt_sdl'

$canonicalOwners = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
@(
    'src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc',
    'src/frontend/qt_sdl/MelonPrimeOsdColorSchema.inc',
    'src/frontend/qt_sdl/MelonPrimeDef.h'
) | ForEach-Object { [void]$canonicalOwners.Add($_) }

$sourceFiles = Get-ChildItem -Path $qtSdl -Recurse -File -Include '*.cpp','*.h','*.hpp','*.c','*.inc','*.ui'
$literalRegex = [regex]'"Metroid\.[^"]+"'
$rows = @()
$total = 0

foreach ($file in $sourceFiles) {
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName) -replace '\\', '/'
    if ($canonicalOwners.Contains($rel)) { continue }

    $count = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
        $count += $literalRegex.Matches($line).Count
    }
    if ($count -eq 0) { continue }

    $total += $count
    $rows += [pscustomobject]@{
        File = $rel
        Count = $count
    }
}

$rows = @($rows | Sort-Object -Property @{ Expression = { $_.Count }; Descending = $true }, @{ Expression = { $_.File }; Ascending = $true })

if ($Json) {
    [pscustomobject]@{
        Budget = $Budget
        Count = $total
        CanonicalOwners = @($canonicalOwners)
        Files = $rows
    } | ConvertTo-Json -Depth 4
} else {
    Write-Host "Metroid quoted literal budget: $total / $Budget"
    $rows | Select-Object -First $MaxList | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
}

if ($total -gt $Budget) {
    Write-Host "FAIL: Metroid quoted literal count $total exceeds budget $Budget."
    exit 1
}

Write-Host "PASS: Metroid quoted literal count is within budget."
exit 0
