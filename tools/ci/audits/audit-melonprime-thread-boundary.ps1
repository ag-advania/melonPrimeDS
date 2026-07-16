param([switch]$Strict)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../../..')).Path

$emuOwned = @(
    'src/frontend/qt_sdl/MelonPrime.cpp',
    'src/frontend/qt_sdl/MelonPrimeGameInput.cpp',
    'src/frontend/qt_sdl/MelonPrimeInGame.cpp',
    'src/frontend/qt_sdl/MelonPrimeLifecycle.cpp'
)
$guiOwned = @(
    'src/frontend/qt_sdl/Screen.cpp',
    'src/frontend/qt_sdl/MelonPrimeScreenCursorPolicy.cpp',
    'src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenInput.inc'
)

$findings = @()
$emuPattern = 'getMainWindow\s*\(|->panel\b|QCursor::|mapToGlobal\s*\(|grabMouse\s*\(|releaseMouse\s*\(|PlatformInput_WarpCursor\s*\('
$guiPattern = 'core->(?:isFocused|isClipWanted|isCursorMode|isStylusMode|isFastForward|screenSyncMode|IsInGame\s*\(|IsRomDetected\s*\()'

foreach ($relative in $emuOwned) {
    $line = 0
    foreach ($text in Get-Content -LiteralPath (Join-Path $repoRoot $relative)) {
        ++$line
        if ($text -match $emuPattern) {
            $findings += "${relative}:${line}: Emu-owned path reaches GUI API: $($text.Trim())"
        }
    }
}
foreach ($relative in $guiOwned) {
    $line = 0
    foreach ($text in Get-Content -LiteralPath (Join-Path $repoRoot $relative)) {
        ++$line
        if ($text -match $guiPattern) {
            $findings += "${relative}:${line}: GUI path reads legacy Emu state directly: $($text.Trim())"
        }
    }
}

Write-Host "MelonPrime GUI/EmuThread boundary audit"
Write-Host "  findings: $($findings.Count)"
$findings | ForEach-Object { Write-Host "  $_" }

if ($Strict -and $findings.Count -ne 0) {
    Write-Error 'MelonPrime thread-boundary regression detected.'
}
