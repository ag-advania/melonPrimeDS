param(
    [string]$Binary = "build\release-mingw-x86_64\src\frontend\qt_sdl\melonPrimeDS.exe",
    [string]$Expected = "src\frontend\qt_sdl\tests\melonprime-hud-golden.txt"
)

$ErrorActionPreference = 'Stop'
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
