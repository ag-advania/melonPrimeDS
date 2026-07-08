# audit-platform-scatter-budget.ps1
#
# Counts platform condition markers in macOS/Linux *input/runtime dispatch*
# call sites. This is a ratchet for melonprime-full-refactor-plan-v4/v6:
# V4 introduced MelonPrimePlatformInput.h as the canonical facade; V6 lowered
# the budget to 22 after lifecycle declutter removed duplicate markers.
#
# Scope is intentionally narrow:
#   - MelonPrime*.cpp/h under src/frontend/qt_sdl
#   - excludes MelonPrimePlatformInput.h (canonical dispatch owner)
#   - excludes MelonPrimeLocalization/ (menu-language locale detection; not
#     raw-input / cursor-warp dispatch — new __APPLE__ there must not consume
#     the input scatter budget or force Q_OS_* workarounds)
# Windows Raw Input sites are not part of this budget.
#
# Raising the budget above 22 is a regression unless input dispatch truly
# gained new platform branches. Non-input platform hooks belong outside scope
# or in the facade, not in alternate macros that evade the ratchet.

param(
    [int]$Budget = 22,
    [int]$MaxList = 40,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$qtSdl = Join-Path $repoRoot 'src/frontend/qt_sdl'

$markerRegex = [regex]'__APPLE__|__linux__'
$scatterExcludePathRegex = [regex]'(^|/)MelonPrimeLocalization/'
$cursorSetPosRegex = [regex]'\bQCursor::setPos\s*\('

function Test-CanCompileOnApple {
    param([string] $Directive)

    $line = $Directive.Trim()
    if ($line -match '^#\s*ifdef\s+__APPLE__') { return $true }
    if ($line -match '^#\s*ifndef\s+__APPLE__') { return $false }
    if ($line -match '^#\s*ifdef\s+__linux__') { return $false }
    if ($line -match '^#\s*ifndef\s+__linux__') { return $true }
    if ($line -match '^#\s*ifdef\s+_WIN32') { return $false }
    if ($line -match '^#\s*ifndef\s+_WIN32') { return $true }

    if ($line -match '!defined\s*\(\s*__APPLE__\s*\)' -or $line -match '!\s*defined\s+__APPLE__') { return $false }
    if ($line -match 'defined\s*\(\s*__APPLE__\s*\)' -or $line -match 'defined\s+__APPLE__' -or $line -match '\b__APPLE__\b') { return $true }
    if ($line -match 'defined\s*\(\s*__linux__\s*\)' -or $line -match 'defined\s+__linux__' -or $line -match '\b__linux__\b') { return $false }
    if ($line -match '!defined\s*\(\s*_WIN32\s*\)' -or $line -match '!\s*defined\s+_WIN32') { return $true }
    if ($line -match 'defined\s*\(\s*_WIN32\s*\)' -or $line -match 'defined\s+_WIN32' -or $line -match '\b_WIN32\b') { return $false }

    return $true
}

function Find-MacQCursorSetPos {
    param([System.IO.FileInfo] $File)

    $frames = [System.Collections.Generic.List[object]]::new()
    $canApple = $true
    $lineNo = 0
    $hits = @()

    foreach ($rawLine in [System.IO.File]::ReadLines($File.FullName)) {
        $lineNo++
        $codeLine = ($rawLine -replace '//.*$', '')

        if ($codeLine -match '^\s*#\s*(if|ifdef|ifndef)\b') {
            $condCanApple = Test-CanCompileOnApple $codeLine
            $currentCanApple = $canApple -and $condCanApple
            $frames.Add([pscustomobject]@{
                ParentCanApple = $canApple
                AnyPriorCanApple = $currentCanApple
            })
            $canApple = $currentCanApple
            continue
        }

        if ($codeLine -match '^\s*#\s*elif\b') {
            if ($frames.Count -eq 0) { continue }
            $idx = $frames.Count - 1
            $frame = $frames[$idx]
            $condCanApple = Test-CanCompileOnApple $codeLine
            $canApple = $frame.ParentCanApple -and (-not $frame.AnyPriorCanApple) -and $condCanApple
            $frame.AnyPriorCanApple = $frame.AnyPriorCanApple -or $canApple
            $frames[$idx] = $frame
            continue
        }

        if ($codeLine -match '^\s*#\s*else\b') {
            if ($frames.Count -eq 0) { continue }
            $idx = $frames.Count - 1
            $frame = $frames[$idx]
            $canApple = $frame.ParentCanApple -and (-not $frame.AnyPriorCanApple)
            $frame.AnyPriorCanApple = $true
            $frames[$idx] = $frame
            continue
        }

        if ($codeLine -match '^\s*#\s*endif\b') {
            if ($frames.Count -eq 0) { continue }
            $idx = $frames.Count - 1
            $frame = $frames[$idx]
            $canApple = $frame.ParentCanApple
            $frames.RemoveAt($idx)
            continue
        }

        if ($canApple -and $cursorSetPosRegex.IsMatch($codeLine)) {
            $rel = [System.IO.Path]::GetRelativePath($repoRoot, $File.FullName) -replace '\\', '/'
            $hits += [pscustomobject]@{
                File = $rel
                Line = $lineNo
                Text = $rawLine.Trim()
            }
        }
    }

    return $hits
}

$cursorGuardFiles = Get-ChildItem -Path $qtSdl -Recurse -File -Include '*.cpp','*.h','*.hpp','*.inc'
$rows = @()
$excludedRows = @()
$total = 0
$macCursorHits = @()

$allMelonPrimeSources = Get-ChildItem -Path $qtSdl -Recurse -File -Include 'MelonPrime*.cpp','MelonPrime*.h'
foreach ($file in $allMelonPrimeSources) {
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName) -replace '\\', '/'
    $count = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
        $count += $markerRegex.Matches($line).Count
    }
    if ($count -le 0) { continue }

    if ($file.Name -eq 'MelonPrimePlatformInput.h' -or $scatterExcludePathRegex.IsMatch($rel)) {
        $excludedRows += [pscustomobject]@{
            File = $rel
            Count = $count
        }
        continue
    }

    $total += $count
    $rows += [pscustomobject]@{
        File = $rel
        Count = $count
    }
}

foreach ($file in $cursorGuardFiles) {
    $macCursorHits += Find-MacQCursorSetPos $file
}

$rows = @($rows | Sort-Object -Property @{ Expression = { $_.Count }; Descending = $true }, @{ Expression = { $_.File }; Ascending = $true })

if ($Json) {
    [pscustomobject]@{
        Budget = $Budget
        Count = $total
        MarkerRegex = $markerRegex.ToString()
        ExcludedFiles = @($excludedRows)
        Files = $rows
        MacQCursorSetPosHits = @($macCursorHits)
    } | ConvertTo-Json -Depth 4
} else {
    Write-Host "Platform scatter budget: $total / $Budget"
    $rows | Select-Object -First $MaxList | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
    if ($excludedRows.Count -gt 0) {
        Write-Host "Excluded from scatter budget (facade / localization):"
        $excludedRows | Select-Object -First $MaxList | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
    }
    if ($macCursorHits.Count -gt 0) {
        Write-Host "macOS QCursor::setPos call(s) found in Apple-reachable code:"
        $macCursorHits | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
    }
}

if ($total -gt $Budget) {
    Write-Host "FAIL: platform scatter count $total exceeds budget $Budget."
    exit 1
}

if ($macCursorHits.Count -gt 0) {
    Write-Host "FAIL: QCursor::setPos is reachable on macOS; use CGWarpMouseCursorPosition/MacWarpCursorGlobal instead."
    exit 1
}

Write-Host "PASS: platform scatter count is within budget and macOS cursor warp guard is clean."
exit 0
