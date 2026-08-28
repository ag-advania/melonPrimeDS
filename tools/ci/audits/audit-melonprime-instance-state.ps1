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

# The standalone ARM9 modules must not keep a second process-global activation
# context. Their dispatcher mask and ROM group are carried by the Core that is
# passed as the NDS hook userdata, so these names are a hard regression gate
# even when a type qualifier makes the generic mutable-state pattern miss them.
$arm9ModuleStateErrors = New-Object System.Collections.Generic.List[string]
foreach ($relativePath in @(
    'src/frontend/qt_sdl/MelonPrimePatchShadowFreezeRuntimeHook.cpp',
    'src/frontend/qt_sdl/MelonPrimePatchFixNoxusBladePersistence.cpp')) {
    $path = Join-Path $repoRoot ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $path)) {
        $arm9ModuleStateErrors.Add("required ARM9 module is missing: $relativePath") | Out-Null
        continue
    }

    $hits = @(Select-String -LiteralPath $path -Pattern 's_activeHooks|s_activeHookCount|s_enabledCached')
    foreach ($hit in $hits) {
        $arm9ModuleStateErrors.Add(("{0}:{1}:{2}" -f $relativePath, $hit.LineNumber, $hit.Line.Trim())) | Out-Null
    }
}
if ($arm9ModuleStateErrors.Count -ne 0) {
    foreach ($errorText in $arm9ModuleStateErrors) {
        Write-Error "ARM9 runtime modules must remain stateless: $errorText"
    }
    exit 1
}

# The standalone ARM9 activation state migration removed the cached module
# contexts from Shadow Freeze and Noxus. The remaining process globals are the
# intentional baseline for this phase; adding another one is a regression.
$baseline = 12
if ($findings.Count -gt $baseline) {
    Write-Error "Mutable-state finding count increased: $($findings.Count) > baseline $baseline"
}
if ($Strict -and $findings.Count -ne $baseline) {
    Write-Error "Strict baseline mismatch: expected $baseline, found $($findings.Count). Update the plan progress after intentional state migration."
}
