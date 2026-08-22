<#
.SYNOPSIS
    Drives the Vulkan presenter's swapchain-lifecycle event matrix automatically.

.DESCRIPTION
    The present-pacing runbook's Phase 1 asks for resize, minimize/restore and
    fullscreen cycling under the Validation Layer. Doing that by hand is slow and
    not repeatable, and the interesting failures are races that need dozens of
    events to surface -- so this drives the window through Win32 instead.

    Point it at a Debug build (tools\build\windows\build-mingw-validation.bat),
    which is the configuration that enables MELONDS_VULKAN_ENABLE_VALIDATION.
    A Release build will run but proves nothing about validation.

    Each phase can be run alone, which is what makes it a diagnostic rather than
    just a stress test: attributing a validation error to "resize" or to
    "minimize/restore" is most of the work of fixing it.

.PARAMETER Phase
    all | resize | minimize | fullscreen | idle
    "idle" is the control: same runtime, no window events.

.PARAMETER ValidateSync
    Enable core and Synchronization Validation through vk_layer_settings.txt,
    require the layer's CURRENT-VALIDATION-ENABLED banner to list
    Synchronization, and fail on SYNC-HAZARD messages. The actual melonDS
    config and temporary layer settings are restored or removed when the run
    ends; the effective policy/Reflex/VSync/present-mode state is self-checked.

.EXAMPLE
    powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 `
        -Rom "C:\roms\game.nds" -Phase minimize -Tag min-run1
    Select-String -Path $env:TEMP\vk-min-run1.out.log -Pattern 'VUID-'
#>
param(
    [Parameter(Mandatory = $true)][string]$Rom,
    [string]$BuildDir = "build\debug-mingw-x86_64",
    [ValidateSet('all', 'resize', 'minimize', 'fullscreen', 'idle')][string]$Phase = 'all',
    # VSync off drops FIFO for a free-running present mode (IMMEDIATE on the
    # NVIDIA surface). Measured: it does NOT fill the present-timing results
    # queue -- minimize/restore does, because presents stall while the window is
    # hidden. Use it as the VSync-off contract control, not as timing-queue
    # stress.
    [switch]$NoVSync,
    [int]$ReflexMode = 0,
    [int]$Policy = 2,
    [string]$Tag = "event-matrix",
    [int]$WarmupSeconds = 18,
    [ValidateRange(1, 1000)][int]$FullscreenCycles = 8,
    [ValidateRange(100, 5000)][int]$FullscreenDelayMilliseconds = 1800,
    [switch]$ValidateSync,
    [string]$OutDir = $env:TEMP
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $BuildDir)) { throw "build directory not found: $BuildDir" }
if (-not (Test-Path $Rom)) { throw "ROM not found: $Rom" }
$null = New-Item -ItemType Directory -Force -Path $OutDir
$outRoot = (Resolve-Path -LiteralPath $OutDir).Path
$dir = (Resolve-Path $BuildDir).Path
$exe = Join-Path $dir 'melonPrimeDS.exe'
if (-not (Test-Path $exe)) { throw "melonPrimeDS.exe not found in $dir" }
$out = Join-Path $outRoot "vk-$Tag.out.log"
$err = Join-Path $outRoot "vk-$Tag.err.log"
$frameCsv = Join-Path $outRoot "vk-$Tag.frames.csv"
foreach ($artifact in @($out, $err, $frameCsv)) {
    if (Test-Path -LiteralPath $artifact) {
        throw "Refusing to overwrite artifact: $artifact"
    }
}
$portableDir = Join-Path $dir 'portable'
$configRoot = if (Test-Path -LiteralPath $portableDir -PathType Container) {
    $portableDir
} else {
    $dir
}
$configPath = Join-Path $configRoot 'melonDS.toml'
$hadConfig = Test-Path -LiteralPath $configPath
$originalConfig = if ($hadConfig) {
    [System.IO.File]::ReadAllBytes($configPath)
} else {
    $null
}
$layerSettings = Join-Path $dir 'vk_layer_settings.txt'
$hadLayerSettings = Test-Path -LiteralPath $layerSettings
$originalLayerSettings = if ($hadLayerSettings) {
    [System.IO.File]::ReadAllBytes($layerSettings)
} else {
    $null
}
$configRestored = $false
$layerSettingsRestored = -not $ValidateSync
$oldPerf = $env:MELONPRIME_PERF
$oldRunId = $env:MELONPRIME_LATENCY_RUN_ID
$oldFrameCsv = $env:MELONPRIME_PERF_CSV
$measurementStartTicks = 0L
$measurementEndTicks = 0L

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public struct MpRect {
  public int Left, Top, Right, Bottom;
}
public class MpWin {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool repaint);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out MpRect rect);
  [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  public const uint KeyUp = 0x0002;
  public static void TapF11(IntPtr h) {
    const uint KeyDownMessage = 0x0100;
    const uint KeyUpMessage = 0x0101;
    IntPtr scanDown = (IntPtr)0x00570001;
    IntPtr scanUp = (IntPtr)unchecked((int)0xC0570001);
    if (PostMessage(h, KeyDownMessage, (IntPtr)0x7A, scanDown)) {
      System.Threading.Thread.Sleep(50);
      PostMessage(h, KeyUpMessage, (IntPtr)0x7A, scanUp);
      return;
    }
    keybd_event(0x7A, 0, 0, UIntPtr.Zero);
    System.Threading.Thread.Sleep(50);
    keybd_event(0x7A, 0, KeyUp, UIntPtr.Zero);
  }
  public static IntPtr Find(uint pid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint wpid; GetWindowThreadProcessId(h, out wpid);
      if (wpid == pid && IsWindowVisible(h) && GetWindowTextLength(h) > 0) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }
}
"@

# Write the config where melonDS actually reads it. Windows portable builds
# prefer <exe>\portable\melonDS.toml; a build without that directory falls
# back to the executable directory (or the platform config path in a
# non-portable build). Preserve the exact original bytes for restoration.
$cfg = @"
3D.Renderer = 3
Screen.UseGL = false
Screen.VSync = $(if ($NoVSync) { 'false' } else { 'true' })
Screen.VSyncInterval = 1
LimitFPS = true
TargetFPS = 60.0
3D.Vulkan.PresentPacingPolicy = $Policy
3D.DX12.NvidiaReflexMode = $ReflexMode
3D.AMD.AntiLag2Enabled = false
Emu.DirectBoot = true
Emu.ExternalBIOSEnable = false
"@
$proc = $null
function Get-WindowFullscreenState {
    $rect = [MpRect]::new()
    if (-not [MpWin]::GetWindowRect($h, [ref]$rect)) { return $null }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $screenWidth = [MpWin]::GetSystemMetrics(0)
    $screenHeight = [MpWin]::GetSystemMetrics(1)
    if ($width -ge ($screenWidth - 4) -and $height -ge ($screenHeight - 4)) {
        return 1
    }
    return 0
}
try {
    Set-Content -LiteralPath $configPath -Value $cfg -NoNewline

    if ($ValidateSync) {
        $syncCfg = @"
khronos_validation.validate_core = true
khronos_validation.validate_sync = true
khronos_validation.report_flags = error,warn,perf,info
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
"@
        Set-Content -LiteralPath $layerSettings -Value $syncCfg -NoNewline -Encoding ASCII
    }

    $env:MELONPRIME_PERF = '1'
    $env:MELONPRIME_LATENCY_RUN_ID = $Tag
    $env:MELONPRIME_PERF_CSV = $frameCsv
    $proc = Start-Process -FilePath $exe -ArgumentList "`"$Rom`"" -WorkingDirectory $dir `
            -PassThru -RedirectStandardOutput $out -RedirectStandardError $err

    # The first swapchain, shader warm-up and the ROM's own boot all have to be
    # past before window events mean anything.
    Start-Sleep -Seconds $WarmupSeconds

    $h = [MpWin]::Find([uint32]$proc.Id)
    if ($h -eq [IntPtr]::Zero) { throw "could not find the melonPrimeDS window" }
    $measurementStartTicks = [Diagnostics.Stopwatch]::GetTimestamp()

    if ($Phase -eq 'all' -or $Phase -eq 'resize') {
        $sizes = @(@(900,700),@(1280,800),@(640,480),@(1024,768),@(1600,900),@(800,600),@(1440,900),@(720,540))
        for ($i = 0; $i -lt 40; $i++) {
            $s = $sizes[$i % $sizes.Count]
            [void][MpWin]::MoveWindow($h, 80, 60, $s[0], $s[1], $true)
            Start-Sleep -Milliseconds 220
        }
        Write-Host "resize x40"
    }

    if ($Phase -eq 'all' -or $Phase -eq 'minimize') {
        # Minimize drives the surface to a zero extent, which is the one path
        # that skips swapchain creation entirely and retries on restore.
        for ($i = 0; $i -lt 20; $i++) {
            [void][MpWin]::ShowWindow($h, 6)   # SW_MINIMIZE
            Start-Sleep -Milliseconds 400
            [void][MpWin]::ShowWindow($h, 9)   # SW_RESTORE
            Start-Sleep -Milliseconds 500
        }
        Write-Host "minimize/restore x20"
    }

    if ($Phase -eq 'all' -or $Phase -eq 'fullscreen') {
        $observedFullscreenState = Get-WindowFullscreenState
        if ($null -eq $observedFullscreenState) {
            throw 'could not observe the initial window fullscreen state'
        }
        for ($i = 0; $i -lt $FullscreenCycles; $i++) {
            $targetFullscreenState = if ($observedFullscreenState -eq 0) { 1 } else { 0 }
            $observedTarget = $false
            for ($attempt = 0; $attempt -lt 3 -and -not $observedTarget; $attempt++) {
                [void][MpWin]::SetForegroundWindow($h)
                Start-Sleep -Milliseconds 250
                [MpWin]::TapF11($h)
                $stateDeadline = [Diagnostics.Stopwatch]::GetTimestamp() +
                    [Diagnostics.Stopwatch]::Frequency * 8
                while ([Diagnostics.Stopwatch]::GetTimestamp() -lt $stateDeadline) {
                    $candidateState = Get-WindowFullscreenState
                    if ($candidateState -eq $targetFullscreenState) {
                        $observedFullscreenState = $candidateState
                        $observedTarget = $true
                        break
                    }
                    Start-Sleep -Milliseconds 100
                }
            }
            if (-not $observedTarget) {
                throw "fullscreen cycle $($i + 1) did not observe state $targetFullscreenState"
            }
            if ($FullscreenDelayMilliseconds -gt 100) {
                Start-Sleep -Milliseconds ($FullscreenDelayMilliseconds - 100)
            }
        }
        Write-Host "fullscreen toggle x$FullscreenCycles"
    }

    if ($Phase -eq 'idle') {
        Start-Sleep -Seconds 20
        Write-Host "idle control"
    }

    Start-Sleep -Seconds 3
    $measurementEndTicks = [Diagnostics.Stopwatch]::GetTimestamp()
}
finally {
    if ($null -ne $proc -and -not $proc.HasExited) {
        Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    }
    if ($null -ne $proc) { Start-Sleep -Seconds 2 }
    if ($ValidateSync) {
        if ($hadLayerSettings) {
            [System.IO.File]::WriteAllBytes($layerSettings, $originalLayerSettings)
            $layerSettingsRestored = [Convert]::ToBase64String(
                [System.IO.File]::ReadAllBytes($layerSettings)) -ceq
                [Convert]::ToBase64String($originalLayerSettings)
        } elseif (Test-Path -LiteralPath $layerSettings) {
            Remove-Item -LiteralPath $layerSettings -Force
            $layerSettingsRestored = -not (Test-Path -LiteralPath $layerSettings)
        }
    }
    if ($hadConfig) {
        [System.IO.File]::WriteAllBytes($configPath, $originalConfig)
        $configRestored = [Convert]::ToBase64String(
            [System.IO.File]::ReadAllBytes($configPath)) -ceq
            [Convert]::ToBase64String($originalConfig)
    } elseif (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
        $configRestored = -not (Test-Path -LiteralPath $configPath)
    }
    if ($null -eq $oldPerf) { Remove-Item Env:MELONPRIME_PERF -ErrorAction SilentlyContinue } else { $env:MELONPRIME_PERF = $oldPerf }
    if ($null -eq $oldRunId) { Remove-Item Env:MELONPRIME_LATENCY_RUN_ID -ErrorAction SilentlyContinue } else { $env:MELONPRIME_LATENCY_RUN_ID = $oldRunId }
    if ($null -eq $oldFrameCsv) { Remove-Item Env:MELONPRIME_PERF_CSV -ErrorAction SilentlyContinue } else { $env:MELONPRIME_PERF_CSV = $oldFrameCsv }
}

$logPaths = @($out, $err) | Where-Object { Test-Path -LiteralPath $_ }
$log = @(Get-Content -LiteralPath $logPaths -ErrorAction SilentlyContinue)
$recreationMatches = @(
    foreach ($line in $log) {
        $match = [regex]::Match($line, 'swapchain recreated fullscreen=(?<state>[01])')
        if ($match.Success) {
            [pscustomobject]@{
                State = [int]$match.Groups['state'].Value
                Line = $line
            }
        }
    }
)
$recreations = $recreationMatches.Count
$actualFullscreenCount = @($recreationMatches | Where-Object State -eq 1).Count
$actualWindowedCount = @($recreationMatches | Where-Object State -eq 0).Count
$actualFullscreenTransitions = 0
$sameStateRecreateCount = 0
$previousFullscreenState = $null
foreach ($recreation in $recreationMatches) {
    if ($null -ne $previousFullscreenState) {
        if ($recreation.State -ne $previousFullscreenState) {
            ++$actualFullscreenTransitions
        } else {
            ++$sameStateRecreateCount
        }
    }
    $previousFullscreenState = $recreation.State
}
$expectedFullscreenTransitions = if ($Phase -in @('fullscreen', 'all')) {
    $FullscreenCycles
} else { 0 }
$expectedFinalFullscreenState = if (($FullscreenCycles % 2) -eq 1) { 1 } else { 0 }
$actualFinalFullscreenState = if ($recreationMatches.Count -gt 0) {
    $recreationMatches[-1].State
} else { -1 }

function Get-PercentileValue {
    param(
        [double[]]$Values,
        [double]$Percentile
    )
    if ($null -eq $Values -or $Values.Count -eq 0) { return 0.0 }
    $sorted = [double[]]@($Values | Sort-Object)
    $position = ($sorted.Count - 1) * $Percentile
    $lower = [int][Math]::Floor($position)
    $upper = [int][Math]::Ceiling($position)
    if ($lower -eq $upper) { return $sorted[$lower] }
    return $sorted[$lower] + ($sorted[$upper] - $sorted[$lower]) * ($position - $lower)
}

$fpsSamples = [System.Collections.Generic.List[double]]::new()
$frameTimeSamples = [System.Collections.Generic.List[double]]::new()
if (Test-Path -LiteralPath $frameCsv) {
    foreach ($row in @(Import-Csv -LiteralPath $frameCsv)) {
        [Int64]$endTicks = 0
        [double]$frameTimeUs = 0
        if (-not [Int64]::TryParse([string]$row.frame_end_ticks, [ref]$endTicks)) { continue }
        if (-not [double]::TryParse(
                [string]$row.frame_time_us,
                [Globalization.NumberStyles]::Float,
                [Globalization.CultureInfo]::InvariantCulture,
                [ref]$frameTimeUs)) { continue }
        if ($measurementStartTicks -gt 0 -and $measurementEndTicks -gt $measurementStartTicks -and
            ($endTicks -lt $measurementStartTicks -or $endTicks -gt $measurementEndTicks)) {
            continue
        }
        if ($frameTimeUs -le 0) { continue }
        $frameTimeSamples.Add($frameTimeUs)
        $fpsSamples.Add(1000000.0 / $frameTimeUs)
    }
}
$fpsP50 = Get-PercentileValue ([double[]]$fpsSamples) 0.50
$fpsP95 = Get-PercentileValue ([double[]]$fpsSamples) 0.95
$fpsP99 = Get-PercentileValue ([double[]]$fpsSamples) 0.99
$frameTimeP50 = Get-PercentileValue ([double[]]$frameTimeSamples) 0.50
$frameTimeP95 = Get-PercentileValue ([double[]]$frameTimeSamples) 0.95
$frameTimeP99 = Get-PercentileValue ([double[]]$frameTimeSamples) 0.99
$presentedSerialRegressionCount = 0
$presentedSerialTelemetryLines = 0
foreach ($line in $log) {
    $match = [regex]::Match($line, 'presented_serial_regression_count=(?<count>\d+)')
    if ($match.Success) {
        ++$presentedSerialTelemetryLines
        $presentedSerialRegressionCount = [Math]::Max(
            $presentedSerialRegressionCount,
            [int]$match.Groups['count'].Value)
    }
}
$vuids = @(
    $log | Select-String -Pattern 'VUID-[A-Za-z0-9-]+' -AllMatches |
        ForEach-Object { $_.Matches | ForEach-Object { $_.Value } }
)
$syncHazards = @(
    $log | Select-String -Pattern 'SYNC-HAZARD' -AllMatches |
        ForEach-Object { $_.Matches | ForEach-Object { $_.Value } }
)
$deviceLost = @($log | Select-String -SimpleMatch 'DEVICE_LOST').Count
$syncBanner = @($log | Select-String -SimpleMatch 'CURRENT-VALIDATION-ENABLED')
$syncBannerWithSynchronization = @()
for ($i = 0; $i -lt $log.Count; $i++) {
    if ($log[$i] -notmatch 'CURRENT-VALIDATION-ENABLED') { continue }
    $blockEnd = [Math]::Min($log.Count - 1, $i + 20)
    if (@($log[$i..$blockEnd] | Select-String -SimpleMatch 'Synchronization').Count -gt 0) {
        $syncBannerWithSynchronization += $log[$i]
    }
}
$syncEnabled = $syncBannerWithSynchronization

$expectedPolicy = switch ($Policy) {
    0 { 'TelemetryOnly' }
    1 { 'PresentWait' }
    2 { 'JustInTime' }
    3 { 'JustInTimeFifoLatestReady' }
    4 { 'PresenterOneFrameBudget' }
    default { "policy=$Policy" }
}
$expectedReflexRequest = if ($ReflexMode -eq 0) { 'off' } else { 'on' }
$expectedReflexBoost = $ReflexMode -eq 2
$expectedVsync = if ($NoVSync) { 'off' } else { 'on' }
$policyStates = @()
$reflexStates = @()
$reflexModeStates = @()
$presentStates = @()
foreach ($line in $log) {
    if ($line -match 'policy=(TelemetryOnly|PresentWait|JustInTime|JustInTimeFifoLatestReady|PresenterOneFrameBudget)') {
        $policyStates += $Matches[1]
    }
    if ($line -match 'NVIDIA Reflex.*requested=(on|off).*actual=(active|inactive)') {
        $reflexStates += [pscustomobject]@{
            Requested = $Matches[1]
            Actual = $Matches[2]
        }
    }
    if ($line -match 'NVIDIA Reflex mode=(on|off) lowLatencyMode=(true|false) lowLatencyBoost=(true|false)') {
        $reflexModeStates += [pscustomobject]@{
            Mode = $Matches[1]
            LowLatencyMode = $Matches[2]
            Boost = $Matches[3]
        }
    }
    if ($line -match 'requested-vsync=(on|off).*selected-present-mode=([A-Z_]+)') {
        $presentStates += [pscustomobject]@{
            Requested = $Matches[1]
            Mode = $Matches[2]
        }
    }
}

$configMismatch = @()
if ($fpsSamples.Count -eq 0) {
    $configMismatch += 'per-frame FPS telemetry missing from the measurement window'
}
if (-not $configRestored) {
    $configMismatch += 'test melonDS.toml was not restored byte-for-byte'
}
if (-not $layerSettingsRestored) {
    $configMismatch += 'test vk_layer_settings.txt was not restored byte-for-byte'
}
if ($policyStates.Count -eq 0) {
    $configMismatch += 'policy state missing from runtime log'
} elseif ($policyStates[-1] -ne $expectedPolicy) {
    $configMismatch += "policy expected=$expectedPolicy actual=$($policyStates[-1])"
}
if ($reflexStates.Count -eq 0) {
    $configMismatch += 'NVIDIA Reflex requested/actual state missing from runtime log'
} else {
    $lastReflex = $reflexStates[-1]
    if ($lastReflex.Requested -ne $expectedReflexRequest) {
        $configMismatch += "Reflex requested expected=$expectedReflexRequest actual=$($lastReflex.Requested)"
    }
    $expectedReflexActual = if ($ReflexMode -eq 0) { 'inactive' } else { 'active' }
    if ($lastReflex.Actual -ne $expectedReflexActual) {
        $configMismatch += "Reflex actual expected=$expectedReflexActual actual=$($lastReflex.Actual)"
    }
}
if ($reflexModeStates.Count -eq 0) {
    $configMismatch += 'NVIDIA Reflex mode state missing from runtime log'
} else {
    $lastReflexMode = $reflexModeStates[-1]
    $expectedMode = if ($ReflexMode -eq 0) { 'off' } else { 'on' }
    $expectedLowLatency = if ($ReflexMode -eq 0) { 'false' } else { 'true' }
    $expectedBoostText = if ($expectedReflexBoost) { 'true' } else { 'false' }
    if ($lastReflexMode.Mode -ne $expectedMode -or
        $lastReflexMode.LowLatencyMode -ne $expectedLowLatency -or
        $lastReflexMode.Boost -ne $expectedBoostText) {
        $configMismatch += "Reflex mode expected=$expectedMode/$expectedLowLatency/$expectedBoostText " +
            "actual=$($lastReflexMode.Mode)/$($lastReflexMode.LowLatencyMode)/$($lastReflexMode.Boost)"
    }
}
if ($presentStates.Count -eq 0) {
    $configMismatch += 'requested VSync/present mode state missing from runtime log'
} else {
    $lastPresent = $presentStates[-1]
    $fifoModes = @('FIFO', 'FIFO_RELAXED', 'FIFO_LATEST_READY')
    $presentModeMismatch = $lastPresent.Requested -ne $expectedVsync
    if ($NoVSync) {
        $presentModeMismatch = $presentModeMismatch -or ($fifoModes -contains $lastPresent.Mode)
    } else {
        $presentModeMismatch = $presentModeMismatch -or -not ($fifoModes -contains $lastPresent.Mode)
    }
    if ($presentModeMismatch) {
        $configMismatch += "VSync/present mode expected=$expectedVsync actual=$($lastPresent.Requested)/$($lastPresent.Mode)"
    }
}
if ($expectedFullscreenTransitions -gt 0) {
    if ($actualFullscreenTransitions -ne $expectedFullscreenTransitions) {
        $configMismatch += "fullscreen state transitions expected=$expectedFullscreenTransitions actual=$actualFullscreenTransitions"
    }
    if ($actualFullscreenCount -eq 0 -or $actualWindowedCount -eq 0) {
        $configMismatch += "fullscreen/windowed swapchain states expected both actual=fullscreen:$actualFullscreenCount windowed:$actualWindowedCount"
    }
    if ($actualFinalFullscreenState -ne $expectedFinalFullscreenState) {
        $configMismatch += "final fullscreen state expected=$expectedFinalFullscreenState actual=$actualFinalFullscreenState"
    }
}
if ($presentedSerialRegressionCount -ne 0) {
    $configMismatch += "presented renderer serial regressed count=$presentedSerialRegressionCount"
}
if ($presentedSerialTelemetryLines -eq 0) {
    $configMismatch += 'presented renderer serial telemetry missing from VulkanPerf surface report'
}

Write-Host ""
Write-Host "log            : $out"
Write-Host "error log      : $err"
Write-Host "config path    : $configPath"
Write-Host "config restore : $(if ($configRestored) { 'PASS' } else { 'FAIL' })"
if ($ValidateSync) {
    Write-Host "layer restore  : $(if ($layerSettingsRestored) { 'PASS' } else { 'FAIL' })"
}
Write-Host "swapchain rebuilds: $recreations"
Write-Host "swapchain recreated states: fullscreen=$actualFullscreenCount windowed=$actualWindowedCount"
Write-Host "fullscreen transitions: $actualFullscreenTransitions expected=$expectedFullscreenTransitions"
Write-Host "same-state recreates: $sameStateRecreateCount"
Write-Host ("FPS p50/p95/p99   : {0:N2}/{1:N2}/{2:N2} (n={3})" -f $fpsP50, $fpsP95, $fpsP99, $fpsSamples.Count)
Write-Host ("frame ms p50/p95/p99: {0:N3}/{1:N3}/{2:N3}" -f ($frameTimeP50 / 1000.0), ($frameTimeP95 / 1000.0), ($frameTimeP99 / 1000.0))
Write-Host "presented serial regressions: $presentedSerialRegressionCount"
Write-Host "device lost       : $deviceLost"
if ($policyStates.Count -gt 0 -and $reflexStates.Count -gt 0 -and $presentStates.Count -gt 0) {
    $lastReflex = $reflexStates[-1]
    $lastPresent = $presentStates[-1]
    Write-Host "config self-check: policy=$($policyStates[-1]) reflex=$($lastReflex.Requested)/$($lastReflex.Actual) vsync=$($lastPresent.Requested) present-mode=$($lastPresent.Mode)"
}
if ($configMismatch.Count -eq 0) {
    Write-Host "config integrity : PASS"
} else {
    Write-Host "config integrity : MISMATCH"
    $configMismatch | ForEach-Object { Write-Host "  $_" }
}
if ($ValidateSync) {
    if ($syncBanner.Count -gt 0 -and $syncEnabled.Count -gt 0) {
        Write-Host "sync validation   : enabled (banner confirmed)"
    } else {
        Write-Host "sync validation   : NOT CONFIRMED (banner missing)"
    }
    Write-Host "sync hazards      : $($syncHazards.Count)"
}
if ($vuids.Count -eq 0) {
    Write-Host "validation        : clean"
} else {
    Write-Host "validation        : $($vuids.Count) message(s)"
    $vuids | Group-Object | Sort-Object Count -Descending |
        ForEach-Object { Write-Host ("  {0,4}x {1}" -f $_.Count, $_.Name) }
}
if ($configMismatch.Count -gt 0 -or $deviceLost -gt 0 -or $vuids.Count -gt 0 -or $syncHazards.Count -gt 0) { exit 1 }
if ($ValidateSync -and ($syncBanner.Count -eq 0 -or $syncEnabled.Count -eq 0)) { exit 1 }
exit 0
