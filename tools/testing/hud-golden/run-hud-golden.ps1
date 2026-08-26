param(
    [string]$Binary = "build\release-mingw-x86_64\melonPrimeDS.exe",
    [string]$Expected = "src\frontend\qt_sdl\tests\melonprime-hud-golden.txt"
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Binary -PathType Leaf)) {
    throw "HUD golden binary not found: $Binary"
}
if (-not (Test-Path -LiteralPath $Expected -PathType Leaf)) {
    throw "HUD golden expected output not found: $Expected"
}
$tmp = New-TemporaryFile
try {
    & $Binary --melonprime-hud-golden $tmp.FullName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    git diff --no-index -- $Expected $tmp.FullName
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
finally {
    Remove-Item -Force $tmp.FullName -ErrorAction SilentlyContinue
}
