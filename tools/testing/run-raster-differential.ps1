param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Vulkan', 'DX12')]
    [string]$Renderer,
    [Parameter(Mandatory = $true)]
    [string]$Executable,
    [Parameter(Mandatory = $true)]
    [string]$RomPath,
    [ValidateRange(0, 8)]
    [int]$LoadSlot = 0,
    [ValidateRange(1, 60)]
    [int]$PostLoadSeconds = 6
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$rendererId = if ($Renderer -eq 'Vulkan') { 3 } else { 4 }
$runDir = Join-Path $repoRoot ("build\raster-differential\" + $Renderer.ToLowerInvariant())
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$runExecutable = Join-Path $runDir 'melonPrimeDS.exe'
$stdout = Join-Path $runDir 'stdout.log'
$stderr = Join-Path $runDir 'stderr.log'
Copy-Item -LiteralPath (Resolve-Path $Executable) -Destination $runExecutable -Force

@"
3D.Renderer = $rendererId
3D.GL.ScaleFactor = 1
3D.GL.HiresCoordinates = false
3D.Soft.Threaded = false
Screen.VSync = false
"@ | Set-Content -LiteralPath (Join-Path $runDir 'melonDS.toml') -Encoding utf8

$testSavestate = $null
if ($LoadSlot -gt 0) {
    $testSavestate = [System.IO.Path]::ChangeExtension(
        (Resolve-Path $RomPath).Path, ".ml$LoadSlot")
    if (!(Test-Path -LiteralPath $testSavestate)) {
        throw "Savestate slot $LoadSlot was not found: $testSavestate"
    }
}
$oldDifferential = $env:MELONPRIME_RASTER_DIFFERENTIAL
$oldTestSavestate = $env:MELONPRIME_TEST_SAVESTATE
$oldTestSavestateUnpause = $env:MELONPRIME_TEST_SAVESTATE_UNPAUSE
$env:MELONPRIME_RASTER_DIFFERENTIAL = '1'
# A diagnostic state must already represent the running gameplay scene. Never
# allow a stale caller environment to re-enable the removed synthetic START /
# extra-frame path.
Remove-Item Env:MELONPRIME_TEST_SAVESTATE_UNPAUSE -ErrorAction SilentlyContinue
if ($testSavestate) {
    # Let the developer-only frontend hook load the state after the ROM and
    # renderer exist. This also tells RasterDifferential to discard the load
    # transition.
    $env:MELONPRIME_TEST_SAVESTATE = $testSavestate
}
$process = $null
try {
    $process = Start-Process -FilePath $runExecutable `
        -ArgumentList ('"' + (Resolve-Path $RomPath).Path + '"') `
        -WorkingDirectory $runDir -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr

    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    while (!$process.HasExited -and $process.MainWindowHandle -eq 0 -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    }
    if ($process.HasExited -or $process.MainWindowHandle -eq 0) {
        throw 'Emulator window was not created.'
    }

    Start-Sleep -Seconds $PostLoadSeconds
}
finally {
    if ($process -and !$process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (!$process.WaitForExit(5000)) {
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }
    $env:MELONPRIME_RASTER_DIFFERENTIAL = $oldDifferential
    $env:MELONPRIME_TEST_SAVESTATE = $oldTestSavestate
    if ($null -eq $oldTestSavestateUnpause) {
        Remove-Item Env:MELONPRIME_TEST_SAVESTATE_UNPAUSE -ErrorAction SilentlyContinue
    }
    else {
        $env:MELONPRIME_TEST_SAVESTATE_UNPAUSE = $oldTestSavestateUnpause
    }
}

$lines = @()
if (Test-Path $stdout) {
    $stdoutLines = @(Get-Content -LiteralPath $stdout)
    $lines += $stdoutLines | Where-Object { $_ -match '\[RasterDiff\]' }
}
if (Test-Path $stderr) {
    $lines += Get-Content -LiteralPath $stderr | Where-Object { $_ -match '\[RasterDiff\]' }
}
if ($lines.Count -eq 0) {
    throw "No RasterDiff frames were reported. Logs: $stdout, $stderr"
}
$allLog = @()
if ($testSavestate) {
    if (Test-Path $stdout) { $allLog += Get-Content -LiteralPath $stdout }
    if (Test-Path $stderr) { $allLog += Get-Content -LiteralPath $stderr }
    if (!($allLog | Where-Object { $_ -match '\[SavestateDiff\].*loaded=1' })) {
        throw "Savestate slot $LoadSlot was not loaded successfully. Logs: $stdout, $stderr"
    }
    if (!($allLog | Where-Object { $_ -match '\[RasterDiffTransition\].*savestate-load' })) {
        throw "Raster differential did not discard the savestate transition. Logs: $stdout, $stderr"
    }
    if ($allLog | Where-Object { $_ -match 'ARM9:\s*data abort|DEVICE_LOST|Renderer fatal' }) {
        throw "Savestate load produced a fatal emulation/renderer error. Logs: $stdout, $stderr"
    }
}

$mismatches = @($lines | Where-Object { $_ -match 'mismatchedPixels=([1-9][0-9]*)' })
if ($mismatches.Count -ne 0) {
    $mismatches | Select-Object -First 10 | ForEach-Object { Write-Error $_ }
    throw "$Renderer raster differential failed in $($mismatches.Count) frame(s)."
}
if ($allLog.Count -eq 0) {
    if (Test-Path $stdout) { $allLog += Get-Content -LiteralPath $stdout }
    if (Test-Path $stderr) { $allLog += Get-Content -LiteralPath $stderr }
}
if ($allLog | Where-Object { $_ -match 'variant index disagreed with legacy insertion order' }) {
    throw "$Renderer variant index did not preserve the legacy per-polygon sequence."
}
if (-not ($lines | Where-Object { $_ -match 'nonZeroPixels=([1-9][0-9]*)' })) {
    throw "$Renderer raster differential only observed empty frames."
}

$lines | Select-Object -Last 5
Write-Output "PASS: $Renderer 1x native 3D output exactly matched Software."
