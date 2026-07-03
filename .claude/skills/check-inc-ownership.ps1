# check-inc-ownership.ps1
#
# Verifies the unity-include ownership rule from .claude/rules (repo-architecture.md,
# completed/melonprime-full-refactor-plan.md Phase 0 / Phase 6):
#   every unity *.inc under src/frontend/qt_sdl must be #include'd by EXACTLY ONE file under src/.
#   known macro-section injection fragments must match their explicit parent set.
#   no src/frontend/qt_sdl *.cpp file may be #include'd as a unity fragment.
#   no *.inc file may be listed as a CMake translation unit.
#
# Output: a table of inc file -> including parent(s) -> status.
# Exit code: 1 if any ownership rule fails, 0 otherwise.
#
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
$cppIncludes  = @{}
$includeRegex = [regex]'#\s*include\s+"(?:[^"]*[/\\])?([^"/\\]+)"'

$expectedMultiParentMap = @{
    'melonprimearm9instructionhook.inc' = @(
        'src/ARM.cpp',
        'src/ARMJIT_x64/ARMJIT_Compiler.cpp',
        'src/ARMJIT_x64/ARMJIT_Compiler.h',
        'src/NDS.cpp',
        'src/NDS.h'
    )
    'melonprimehudpropschema.inc' = @(
        'src/frontend/qt_sdl/Config.cpp',
        'src/frontend/qt_sdl/EmuInstance.cpp',
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudTables.inc',
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp',
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp',
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigInternal.h',
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigPreview.cpp',
        'src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenDefs.inc',
        'src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp',
        'src/frontend/qt_sdl/MelonPrimeHudRender.cpp',
        'src/frontend/qt_sdl/MelonPrimePatchAspectRatio.cpp',
        'src/frontend/qt_sdl/MelonPrimePatchOsdColor.cpp',
        'src/frontend/qt_sdl/Screen.cpp',
        'src/frontend/qt_sdl/Window.cpp'
    )
    'melonprimeosdcolorschema.inc' = @(
        'src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudDialogProps.inc',
        'src/frontend/qt_sdl/MelonPrimePatchOsdColor.cpp'
    )
}

function Test-SameStringSet {
    param(
        [string[]] $Actual,
        [string[]] $Expected
    )
    $diff = Compare-Object -ReferenceObject ($Expected | Sort-Object) -DifferenceObject ($Actual | Sort-Object)
    return $null -eq $diff
}

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
        if ($name.EndsWith('.cpp')) {
            if (-not $cppIncludes.ContainsKey($name)) {
                $cppIncludes[$name] = [System.Collections.Generic.List[string]]::new()
            }
            if (-not $cppIncludes[$name].Contains($rel)) {
                $cppIncludes[$name].Add($rel)
            }
        }
    }
}

$violations = $dupNames.Count
$rows = foreach ($inc in ($incFiles | Sort-Object Name)) {
    $key     = $inc.Name.ToLowerInvariant()
    $parents = if ($includeMap.ContainsKey($key)) { $includeMap[$key] } else { @() }

    if ($expectedMultiParentMap.ContainsKey($key)) {
        $status = if (Test-SameStringSet -Actual $parents -Expected $expectedMultiParentMap[$key]) {
            'OK_EXPECTED_MULTI'
        } else {
            'BAD_EXPECTED_MULTI'
        }
        if ($status -ne 'OK_EXPECTED_MULTI') { $violations++ }
    } else {
        $status  = switch ($parents.Count) {
            0       { 'ORPHAN' }
            1       { 'OK' }
            default { 'MULTI' }
        }
        if ($parents.Count -ne 1) { $violations++ }
    }
    [pscustomobject]@{
        Inc     = $inc.Name
        Parents = if ($parents.Count) { $parents -join '; ' } else { '(none)' }
        Status  = $status
    }
}

$rows | Format-Table -AutoSize | Out-String -Width 400 | Write-Host

$cppIncludeRows = foreach ($key in ($cppIncludes.Keys | Sort-Object)) {
    [pscustomobject]@{
        IncludedCpp = $key
        Parents     = $cppIncludes[$key] -join '; '
        Status      = 'FORBIDDEN'
    }
}
if ($cppIncludeRows) {
    $cppIncludeRows | Format-Table -AutoSize | Out-String -Width 400 | Write-Host
    $violations += $cppIncludeRows.Count
}

$cmakePath = Join-Path $qtSdl 'CMakeLists.txt'
$cmakeText = if (Test-Path $cmakePath) { [System.IO.File]::ReadAllText($cmakePath) } else { '' }
$cmakeIncRows = foreach ($inc in ($incFiles | Sort-Object Name)) {
    if ($cmakeText.Contains($inc.Name)) {
        [pscustomobject]@{
            Inc    = $inc.Name
            Listed = 'src/frontend/qt_sdl/CMakeLists.txt'
            Status = 'FORBIDDEN'
        }
    }
}
if ($cmakeIncRows) {
    $cmakeIncRows | Format-Table -AutoSize | Out-String -Width 400 | Write-Host
    $violations += $cmakeIncRows.Count
}

if ($violations -gt 0) {
    Write-Host "FAIL: $violations unity ownership violation(s) found."
    exit 1
}
Write-Host "PASS: all $($incFiles.Count) .inc files match ownership rules; no included .cpp or CMake-listed .inc files found."
exit 0
