# check-inc-ownership.ps1
#
# Verifies the unity-include ownership rule from .claude/rules (repo-architecture.md,
# melonprime-full-refactor-plan.md Phase 0 / Phase 6):
#   every *.inc under src/frontend/qt_sdl must be #include'd by EXACTLY ONE file under src/.
#
# Output: a table of inc file -> including parent(s) -> status.
# Exit code: 1 if any .inc has 0 (ORPHAN) or >1 (MULTI) including files, 0 otherwise.
#
# Known quirk (informational, non-failing, until Phase 6):
#   MelonPrimeHudConfigOnScreen.cpp is a .cpp that is unity-included by MelonPrimeHudRender.cpp.

$ErrorActionPreference = 'Stop'

# .claude/skills -> repo root
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$srcRoot  = Join-Path $repoRoot 'src'
$qtSdl    = Join-Path $srcRoot  'frontend/qt_sdl'

$incFiles = @(Get-ChildItem -Path $qtSdl -Recurse -File -Filter '*.inc')

# Warn on duplicate .inc basenames (the map below keys by basename only).
$dupNames = $incFiles | Group-Object Name | Where-Object Count -gt 1
foreach ($d in $dupNames) {
    Write-Host "WARN: duplicate .inc basename '$($d.Name)' at: $($d.Group.FullName -join '; ')"
}

# One pass over all source-ish files under src/, building: included basename -> list of including files.
$sourceFiles  = Get-ChildItem -Path $srcRoot -Recurse -File -Include '*.cpp','*.h','*.hpp','*.c','*.inc'
$includeMap   = @{}
$includeRegex = [regex]'#\s*include\s+"(?:[^"]*[/\\])?([^"/\\]+)"'

foreach ($f in $sourceFiles) {
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $f.FullName) -replace '\\', '/'
    foreach ($line in [System.IO.File]::ReadLines($f.FullName)) {
        if ($line.TrimStart().StartsWith('//')) { continue }   # skip commented-out includes
        $m = $includeRegex.Match($line)
        if (-not $m.Success) { continue }
        $name = $m.Groups[1].Value.ToLowerInvariant()
        if (-not $includeMap.ContainsKey($name)) {
            $includeMap[$name] = [System.Collections.Generic.List[string]]::new()
        }
        if (-not $includeMap[$name].Contains($rel)) {
            $includeMap[$name].Add($rel)
        }
    }
}

$violations = 0
$rows = foreach ($inc in ($incFiles | Sort-Object Name)) {
    $key     = $inc.Name.ToLowerInvariant()
    $parents = if ($includeMap.ContainsKey($key)) { $includeMap[$key] } else { @() }
    $status  = switch ($parents.Count) {
        0       { 'ORPHAN' }
        1       { 'OK' }
        default { 'MULTI' }
    }
    if ($parents.Count -ne 1) { $violations++ }
    [pscustomobject]@{
        Inc     = $inc.Name
        Parents = if ($parents.Count) { $parents -join '; ' } else { '(none)' }
        Status  = $status
    }
}

$rows | Format-Table -AutoSize | Out-String -Width 400 | Write-Host

# Informational known quirk (until Phase 6).
$quirkKey = 'melonprimehudconfigonscreen.cpp'
Write-Host 'INFO: MelonPrimeHudConfigOnScreen.cpp is a .cpp unity-included by MelonPrimeHudRender.cpp (known quirk until Phase 6).'
if ($includeMap.ContainsKey($quirkKey)) {
    Write-Host ("INFO:   actual includer(s): " + ($includeMap[$quirkKey] -join '; '))
}

if ($violations -gt 0) {
    Write-Host "FAIL: $violations .inc file(s) have 0 or >1 including parents."
    exit 1
}
Write-Host "PASS: all $($incFiles.Count) .inc files have exactly one including parent."
exit 0
