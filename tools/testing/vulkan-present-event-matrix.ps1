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
    [switch]$ValidateSync,
    [string]$OutDir = $env:TEMP
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path $BuildDir)) { throw "build directory not found: $BuildDir" }
if (-not (Test-Path $Rom)) { throw "ROM not found: $Rom" }
$dir = (Resolve-Path $BuildDir).Path
$exe = Join-Path $dir 'melonPrimeDS.exe'
if (-not (Test-Path $exe)) { throw "melonPrimeDS.exe not found in $dir" }
$out = Join-Path $OutDir "vk-$Tag.out.log"
$err = Join-Path $OutDir "vk-$Tag.err.log"
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

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public class MpWin {
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr h, int x, int y, int w, int t, bool repaint);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
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

    $proc = Start-Process -FilePath $exe -ArgumentList "`"$Rom`"" -WorkingDirectory $dir `
            -PassThru -RedirectStandardOutput $out -RedirectStandardError $err

    # The first swapchain, shader warm-up and the ROM's own boot all have to be
    # past before window events mean anything.
    Start-Sleep -Seconds $WarmupSeconds

    $h = [MpWin]::Find([uint32]$proc.Id)
    if ($h -eq [IntPtr]::Zero) { throw "could not find the melonPrimeDS window" }

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
        Add-Type -AssemblyName System.Windows.Forms
        for ($i = 0; $i -lt $FullscreenCycles; $i++) {
            [void][MpWin]::SetForegroundWindow($h)
            Start-Sleep -Milliseconds 250
            [System.Windows.Forms.SendKeys]::SendWait('{F11}')
            Start-Sleep -Milliseconds 700
        }
        Write-Host "fullscreen toggle x$FullscreenCycles"
    }

    if ($Phase -eq 'idle') {
        Start-Sleep -Seconds 20
        Write-Host "idle control"
    }

    Start-Sleep -Seconds 3
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
}

$logPaths = @($out, $err) | Where-Object { Test-Path -LiteralPath $_ }
$log = @(Get-Content -LiteralPath $logPaths -ErrorAction SilentlyContinue)
$recreations = @($log | Select-String -SimpleMatch 'swapchain ready').Count
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

Write-Host ""
Write-Host "log            : $out"
Write-Host "error log      : $err"
Write-Host "config path    : $configPath"
Write-Host "config restore : $(if ($configRestored) { 'PASS' } else { 'FAIL' })"
if ($ValidateSync) {
    Write-Host "layer restore  : $(if ($layerSettingsRestored) { 'PASS' } else { 'FAIL' })"
}
Write-Host "swapchain rebuilds: $recreations"
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
