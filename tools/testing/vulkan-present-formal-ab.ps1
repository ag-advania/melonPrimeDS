<#
.SYNOPSIS
    Runs one fixed-condition Vulkan Formal Phase 3 capture.

.DESCRIPTION
    This is a run driver, not a second pacing implementation. It writes the
    minimal portable configuration for one mode, launches the configured
    Release capture executable, waits for the requested frame budget, and then
    closes the application so VulkanPresentLatencyCapture flushes its CSV.
    The original config is restored byte-for-byte in finally, and a run never
    overwrites an existing CSV.
#>
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [string]$BuildDir = 'build\release-mingw-x86_64',
    [Parameter(Mandatory = $true)][string]$RunId,
    [Parameter(Mandatory = $true)][string]$OutputDir,
    [ValidateSet(0, 1, 2, 3, 4)][int]$Policy = 2,
    [ValidateSet(0, 1, 2)][int]$ReflexMode = 0,
    [switch]$NoVSync,
    [int]$WarmupFrames = 600,
    [int]$MeasuredFrames = 10000,
    [double]$TargetFPS = 60.0,
    [int]$GraceSeconds = 45,
    [switch]$DisableImplicitLayers
)

$ErrorActionPreference = 'Stop'

if ($WarmupFrames -lt 0 -or $MeasuredFrames -lt 1 -or $TargetFPS -le 0) {
    throw 'WarmupFrames must be >= 0, MeasuredFrames must be > 0, and TargetFPS must be positive.'
}

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = (Resolve-Path (Join-Path $repo $BuildDir)).Path
$exe = Join-Path $build 'melonPrimeDS.exe'
$romPath = (Resolve-Path $Rom).Path
$out = (Resolve-Path (New-Item -ItemType Directory -Force -Path $OutputDir)).Path

if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Release executable not found: $exe"
}

$csv = Join-Path $out "$RunId.csv"
$stdout = Join-Path $out "$RunId.out.log"
$stderr = Join-Path $out "$RunId.err.log"
$metadata = Join-Path $out "$RunId.metadata.txt"
foreach ($path in @($csv, $stdout, $stderr, $metadata)) {
    if (Test-Path -LiteralPath $path) {
        throw "Refusing to overwrite existing formal-run artifact: $path"
    }
}

$portableDir = Join-Path $build 'portable'
$configRoot = if (Test-Path -LiteralPath $portableDir -PathType Container) {
    $portableDir
} else {
    $build
}
$configPath = Join-Path $configRoot 'melonDS.toml'
$hadConfig = Test-Path -LiteralPath $configPath
$originalConfig = if ($hadConfig) { [IO.File]::ReadAllBytes($configPath) } else { $null }

$layerSettings = Join-Path $build 'vk_layer_settings.txt'
$hadLayerSettings = Test-Path -LiteralPath $layerSettings
$originalLayerSettings = if ($hadLayerSettings) { [IO.File]::ReadAllBytes($layerSettings) } else { $null }

$oldRunId = $env:MELONPRIME_LATENCY_RUN_ID
$oldCsv = $env:MELONPRIME_LATENCY_CSV
$oldLayerDisable = $env:VK_LOADER_LAYERS_DISABLE
$proc = $null
$configRestored = $false
$layerRestored = -not $hadLayerSettings
$startedUtc = [DateTime]::UtcNow
$policyName = @(
    'TelemetryOnly',
    'PresentWait',
    'JustInTime',
    'JustInTimeFifoLatestReady',
    'PresenterOneFrameBudget'
)[$Policy]
$reflexName = @('Off', 'On', 'On+Boost')[$ReflexMode]
$vsyncName = if ($NoVSync) { 'off' } else { 'on' }
$duration = [Math]::Ceiling(($WarmupFrames + $MeasuredFrames) / $TargetFPS) + $GraceSeconds

$utf8 = [Text.UTF8Encoding]::new($false)
$cfg = @"
3D.Renderer = 3
Screen.UseGL = false
Screen.VSync = $(if ($NoVSync) { 'false' } else { 'true' })
Screen.VSyncInterval = 1
LimitFPS = true
TargetFPS = $TargetFPS
3D.Vulkan.PresentPacingPolicy = $Policy
3D.DX12.NvidiaReflexMode = $ReflexMode
3D.AMD.AntiLag2Enabled = false
Emu.DirectBoot = true
Emu.ExternalBIOSEnable = false
"@

try {
    [IO.File]::WriteAllText($configPath, $cfg, $utf8)

    $env:MELONPRIME_LATENCY_RUN_ID = $RunId
    $env:MELONPRIME_LATENCY_CSV = $csv
    if ($DisableImplicitLayers) {
        $env:VK_LOADER_LAYERS_DISABLE = '~implicit~'
    } else {
        Remove-Item Env:VK_LOADER_LAYERS_DISABLE -ErrorAction SilentlyContinue
    }

    $args = '"' + $romPath + '"'
    $proc = Start-Process -FilePath $exe -ArgumentList $args -WorkingDirectory $build `
        -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr

    Start-Sleep -Seconds $duration
    if ($proc.HasExited) {
        throw "Formal run exited before its capture window ended (exit=$($proc.ExitCode))."
    }
}
finally {
    if ($null -ne $proc -and -not $proc.HasExited) {
        [void]$proc.CloseMainWindow()
        if (-not $proc.WaitForExit(20000)) {
            Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class MpFormalWin {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@ -ErrorAction SilentlyContinue
            if ($proc.MainWindowHandle -ne [IntPtr]::Zero) {
                [void][MpFormalWin]::PostMessage($proc.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
            }
            [void]$proc.WaitForExit(15000)
        }
    }
    if ($null -ne $proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }

    if ($hadConfig) {
        [IO.File]::WriteAllBytes($configPath, $originalConfig)
        $configRestored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($configPath)) -ceq
            [Convert]::ToBase64String($originalConfig)
    } elseif (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
        $configRestored = -not (Test-Path -LiteralPath $configPath)
    }

    if ($hadLayerSettings) {
        [IO.File]::WriteAllBytes($layerSettings, $originalLayerSettings)
        $layerRestored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($layerSettings)) -ceq
            [Convert]::ToBase64String($originalLayerSettings)
    } elseif (Test-Path -LiteralPath $layerSettings) {
        Remove-Item -LiteralPath $layerSettings -Force
        $layerRestored = -not (Test-Path -LiteralPath $layerSettings)
    }

    if ($null -eq $oldRunId) { Remove-Item Env:MELONPRIME_LATENCY_RUN_ID -ErrorAction SilentlyContinue }
    else { $env:MELONPRIME_LATENCY_RUN_ID = $oldRunId }
    if ($null -eq $oldCsv) { Remove-Item Env:MELONPRIME_LATENCY_CSV -ErrorAction SilentlyContinue }
    else { $env:MELONPRIME_LATENCY_CSV = $oldCsv }
    if ($null -eq $oldLayerDisable) { Remove-Item Env:VK_LOADER_LAYERS_DISABLE -ErrorAction SilentlyContinue }
    else { $env:VK_LOADER_LAYERS_DISABLE = $oldLayerDisable }
}

$endedUtc = [DateTime]::UtcNow
$rowCount = 0
if (Test-Path -LiteralPath $csv) {
    $rowCount = [Math]::Max(0, (@(Get-Content -LiteralPath $csv).Count - 1))
}
$exitCode = -1
if ($null -ne $proc) {
    try { $proc.Refresh() } catch { }
    if ($proc.HasExited) {
        try { $exitCode = [int]$proc.ExitCode } catch { $exitCode = -1 }
    }
}
$metadataText = @"
run_id=$RunId
policy=$policyName
policy_id=$Policy
reflex=$reflexName
reflex_id=$ReflexMode
vsync=$vsyncName
target_fps=$TargetFPS
warmup_frames=$WarmupFrames
measured_frames_requested=$MeasuredFrames
minimum_capture_rows=$($WarmupFrames + $MeasuredFrames)
duration_seconds=$duration
process_exit_code=$exitCode
capture_rows=$rowCount
config_path=$configPath
config_restore=$(if ($configRestored) { 'PASS' } else { 'FAIL' })
layer_settings_restore=$(if ($layerRestored) { 'PASS' } else { 'FAIL' })
implicit_layers_disabled=$(if ($DisableImplicitLayers) { 'yes' } else { 'no' })
started_utc=$($startedUtc.ToString('o'))
ended_utc=$($endedUtc.ToString('o'))
executable=$exe
csv=$csv
stdout=$stdout
stderr=$stderr
"@
[IO.File]::WriteAllText($metadata, $metadataText, $utf8)

Write-Host "run_id             : $RunId"
Write-Host "policy             : $policyName"
Write-Host "reflex             : $reflexName"
Write-Host "vsync              : $vsyncName"
Write-Host "capture rows       : $rowCount (required >= $($WarmupFrames + $MeasuredFrames))"
Write-Host "process exit       : $exitCode"
Write-Host "config restore     : $(if ($configRestored) { 'PASS' } else { 'FAIL' })"
Write-Host "layer restore      : $(if ($layerRestored) { 'PASS' } else { 'FAIL' })"
Write-Host "csv                : $csv"

if (-not $configRestored -or -not $layerRestored -or $exitCode -ne 0 -or
    -not (Test-Path -LiteralPath $csv) -or $rowCount -lt ($WarmupFrames + $MeasuredFrames)) {
    exit 1
}
exit 0
