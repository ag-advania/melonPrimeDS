param(
    [switch]$Strict,
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path
$sourceRoot = Join-Path $repoRoot 'src/frontend/qt_sdl'

$files = Get-ChildItem -LiteralPath $sourceRoot -Recurse -File |
    Where-Object {
        ($_.Name -like 'MelonPrime*' -or $_.DirectoryName -like '*InputConfig*') -and
        $_.Extension -in @('.cpp', '.h', '.mm', '.inc')
    }

$mutablePattern = '^\s*(?:static\s+)(?!constexpr\b|const\b)(?:thread_local\s+)?(?:[A-Za-z_][A-Za-z0-9_:<> ,*&]*\s+)?(s_[A-Za-z0-9_]+)\b'
$allowedPattern = 'static\s+(?:constexpr|const)\b|function-local immutable|process-service:'
$findings = foreach ($file in $files) {
    $lineNumber = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNumber++
        if ($line -match $mutablePattern -and $line -notmatch $allowedPattern) {
            [pscustomobject]@{
                Path = [IO.Path]::GetRelativePath($repoRoot, $file.FullName).Replace('\', '/')
                Line = $lineNumber
                Symbol = $Matches[1]
                Text = $line.Trim()
            }
        }
    }
}

$groups = $findings | Group-Object Path | Sort-Object Name
Write-Host "MelonPrime process-global mutable-state audit"
Write-Host "  files:    $($groups.Count)"
Write-Host "  findings: $($findings.Count)"

foreach ($group in $groups) {
    Write-Host ("  {0}: {1}" -f $group.Name, $group.Count)
    if ($List) {
        foreach ($item in $group.Group) {
            Write-Host ("    {0}: {1}" -f $item.Line, $item.Text)
        }
    }
}

# Phase 0 captures the unsafe pre-refactor state. Later phases lower this
# ratchet; adding another file-static mutable symbol is always a regression.
$baseline = 22
if ($findings.Count -gt $baseline) {
    Write-Error "Mutable-state finding count increased: $($findings.Count) > baseline $baseline"
}
if ($Strict -and $findings.Count -ne $baseline) {
    Write-Error "Strict baseline mismatch: expected $baseline, found $($findings.Count). Update the plan progress after intentional state migration."
}
