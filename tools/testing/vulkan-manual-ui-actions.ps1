<#
Runs the non-window-matrix portions of the Vulkan manual Phase 1 gate.

The executable remains a Debug Validation build. This driver only sends the
same menu/hotkey actions a person would send; it does not bypass the Qt or
emulation-thread paths. All temporary config/layer files are restored exactly.
#>
param(
    [Parameter(Mandatory = $true)][ValidateSet('video', 'speed', 'rom', 'dpi')][string]$Action,
    [Parameter(Mandatory = $true)][string]$Rom,
    [string]$BuildDir = 'build\debug-mingw-vulkan-validation2',
    [Parameter(Mandatory = $true)][string]$OutDir,
    [int]$Iterations = 20,
    [int]$WarmupSeconds = 18
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Windows.Forms

Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class MpManualWin {
  public delegate bool EnumWindowsProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder b, int n);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out MpManualRect r);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, uint dx, uint dy, uint d, UIntPtr extra);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
  public const uint KEYEVENTF_KEYUP = 0x0002;
  public const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
  public const uint MOUSEEVENTF_LEFTUP = 0x0004;
  public static void Click(int x, int y) {
    SetCursorPos(x, y);
    mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
    mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
  }
  public static IntPtr Find(uint wantedPid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == wantedPid && IsWindowVisible(h) && GetWindowTextLength(h) > 0) {
        found = h; return false;
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static string Titles(uint wantedPid) {
    var result = new StringBuilder();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != wantedPid || !IsWindowVisible(h)) return true;
      int len = GetWindowTextLength(h);
      if (len <= 0) return true;
      var text = new StringBuilder(len + 1); GetWindowText(h, text, text.Capacity);
      if (result.Length > 0) result.Append(" | ");
      result.Append(text.ToString()); return true;
    }, IntPtr.Zero);
    return result.ToString();
  }
}
public struct MpManualRect { public int Left, Top, Right, Bottom; }
"@

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = (Resolve-Path (Join-Path $repo $BuildDir)).Path
$exe = Join-Path $build 'melonPrimeDS.exe'
$romPath = (Resolve-Path $Rom).Path
$out = (Resolve-Path (New-Item -ItemType Directory -Force -Path $OutDir)).Path
$prefix = Join-Path $out "manual-$Action"
$stdout = "$prefix.out.log"
$stderr = "$prefix.err.log"
$harness = "$prefix.harness.log"
$layerSettings = Join-Path $build 'vk_layer_settings.txt'
$portableDir = Join-Path $build 'portable'
$configRoot = if (Test-Path -LiteralPath $portableDir -PathType Container) { $portableDir } else { $build }
$configPath = Join-Path $configRoot 'melonDS.toml'
$hadConfig = Test-Path -LiteralPath $configPath
$originalConfig = if ($hadConfig) { [IO.File]::ReadAllBytes($configPath) } else { $null }
$hadLayer = Test-Path -LiteralPath $layerSettings
$originalLayer = if ($hadLayer) { [IO.File]::ReadAllBytes($layerSettings) } else { $null }
$proc = $null
$script:manualWindow = [IntPtr]::Zero
$utf8 = [Text.UTF8Encoding]::new($false)

$cfg = @"
3D.Renderer = 3
Screen.UseGL = false
Screen.VSync = true
Screen.VSyncInterval = 1
LimitFPS = true
TargetFPS = 60.0
3D.Vulkan.PresentPacingPolicy = 2
3D.DX12.NvidiaReflexMode = 0
3D.AMD.AntiLag2Enabled = false
Emu.DirectBoot = true
Emu.ExternalBIOSEnable = false

[Instance0]

[Instance0.Keyboard]
HK_Reset = 82
HK_FastForward = 70
HK_FrameLimitToggle = 76
HK_SlowMo = 83
HK_SlowMoToggle = 77
"@

function Send-Key([string]$keys) {
    [System.Windows.Forms.SendKeys]::SendWait($keys)
    Start-Sleep -Milliseconds 250
}

function Focus-Window {
    if ($script:manualWindow -eq [IntPtr]::Zero) { throw 'manual action window not found' }
    [void][MpManualWin]::SetForegroundWindow($script:manualWindow)
    Start-Sleep -Milliseconds 250
}

function Wait-ForWindow {
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $script:manualWindow = [MpManualWin]::Find([uint32]$proc.Id)
        if ($script:manualWindow -ne [IntPtr]::Zero) { return }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    throw 'manual action window did not appear'
}

function Close-ProcessGracefully {
    if ($null -eq $proc) { return }
    if (-not $proc.HasExited) {
        [void]$proc.CloseMainWindow()
        [void]$proc.WaitForExit(20000)
    }
    if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
}

function Open-VideoSettings {
    Focus-Window
    $rect = [MpManualRect]::new()
    [void][MpManualWin]::GetWindowRect($script:manualWindow, [ref]$rect)
    # The Qt menu bar is intentionally compact in this validation build. The
    # fourth top-level menu is Config/設定; click it, then use its stable
    # production menu order to select Video settings/映像設定.
    [MpManualWin]::Click($rect.Left + 160, $rect.Top + 31)
    Start-Sleep -Milliseconds 350
    Send-Key '{DOWN 3}{ENTER}'
    Start-Sleep -Milliseconds 800
    $titles = [MpManualWin]::Titles([uint32]$proc.Id)
    Add-Content -LiteralPath $harness -Value "video-dialog titles: $titles"
    $videoTitleToken = ([char]0x6620) + ([char]0x50CF) + ([char]0x8A2D) + ([char]0x5B9A)
    if (($titles -notmatch '(?i)video') -and ($titles -notlike "*$videoTitleToken*")) {
        throw "Video settings dialog was not observed: $titles"
    }
}

try {
    if ($Action -eq 'dpi') {
        $monitors = @(Get-CimInstance -Namespace root\wmi -ClassName WmiMonitorBasicDisplayParams -ErrorAction SilentlyContinue)
        Add-Content -LiteralPath $harness -Value "DPI monitor count: $($monitors.Count)"
        Add-Content -LiteralPath $harness -Value 'DPI result: NOT RUN — only one physical monitor is exposed; no genuine cross-DPI transition was available.'
        exit 2
    }

    [IO.File]::WriteAllText($configPath, $cfg, $utf8)
    $syncCfg = "khronos_validation.validate_core = true`nkhronos_validation.validate_sync = true`nkhronos_validation.report_flags = error,warn,perf,info`nkhronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG`n"
    [IO.File]::WriteAllText($layerSettings, $syncCfg, [Text.Encoding]::ASCII)
    $proc = Start-Process -FilePath $exe -ArgumentList ('"' + $romPath + '"') -WorkingDirectory $build `
        -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    Wait-ForWindow
    Start-Sleep -Seconds $WarmupSeconds
    Set-Content -LiteralPath $harness -Value "action=$Action`niterations=$Iterations`nwindow=$script:manualWindow"

    if ($Action -eq 'video') {
        for ($i = 1; $i -le $Iterations; $i++) {
            Open-VideoSettings
            Send-Key '{ESC}'
            Add-Content -LiteralPath $harness -Value "cancel=$i"
        }
        for ($i = 1; $i -le $Iterations; $i++) {
            Open-VideoSettings
            Send-Key '{ENTER}'
            Add-Content -LiteralPath $harness -Value "apply-same-value=$i"
        }
    }

    if ($Action -eq 'speed') {
        Focus-Window
        # F: hold fast-forward; L: toggle fast-forward; M: toggle slow motion;
        # S: hold slow motion. These key values are installed in the temporary
        # config above so the action is deterministic on this workstation.
        [MpManualWin]::keybd_event(0x46, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Seconds 4
        [MpManualWin]::keybd_event(0x46, 0, [MpManualWin]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
        Add-Content -LiteralPath $harness -Value 'fast-forward hold: sent'
        Send-Key 'l'; Start-Sleep -Seconds 4; Send-Key 'l'
        Add-Content -LiteralPath $harness -Value 'fast-forward toggle: sent'
        Send-Key 'm'; Start-Sleep -Seconds 4; Send-Key 'm'
        Add-Content -LiteralPath $harness -Value 'slow-motion toggle: sent'
        [MpManualWin]::keybd_event(0x53, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Seconds 4
        [MpManualWin]::keybd_event(0x53, 0, [MpManualWin]::KEYEVENTF_KEYUP, [UIntPtr]::Zero)
        Add-Content -LiteralPath $harness -Value 'slow-motion hold: sent'
    }

    if ($Action -eq 'rom') {
        Focus-Window
        Send-Key '+{F1}'; Start-Sleep -Seconds 2
        Add-Content -LiteralPath $harness -Value 'savestate save slot 1: sent'
        Send-Key '{F1}'; Start-Sleep -Seconds 3
        Add-Content -LiteralPath $harness -Value 'savestate load slot 1: sent'
        Send-Key '{F12}'; Start-Sleep -Seconds 2
        Add-Content -LiteralPath $harness -Value 'savestate undo: sent'
        Send-Key 'r'; Start-Sleep -Seconds 4
        Add-Content -LiteralPath $harness -Value 'reset hotkey: sent'
        Add-Content -LiteralPath $harness -Value 'first ROM lifecycle: PASS (launch/save/load/undo/reset actions sent)'
    }

    Close-ProcessGracefully
    $proc = $null
    if ($Action -eq 'rom') {
        # Reopen the same ROM as a distinct session to catch stale renderer,
        # target baseline and swapchain-generation state across close/reopen.
        $stdout2 = "$prefix-reopen.out.log"
        $stderr2 = "$prefix-reopen.err.log"
        $proc = Start-Process -FilePath $exe -ArgumentList ('"' + $romPath + '"') -WorkingDirectory $build `
            -PassThru -RedirectStandardOutput $stdout2 -RedirectStandardError $stderr2
        Wait-ForWindow
        Start-Sleep -Seconds $WarmupSeconds
        Add-Content -LiteralPath $harness -Value 'second ROM lifecycle: reopened and ran'
        Close-ProcessGracefully
    }
}
finally {
    Close-ProcessGracefully
    if ($hadConfig) {
        [IO.File]::WriteAllBytes($configPath, $originalConfig)
    } elseif (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
    if ($hadLayer) {
        [IO.File]::WriteAllBytes($layerSettings, $originalLayer)
    } elseif (Test-Path -LiteralPath $layerSettings) {
        Remove-Item -LiteralPath $layerSettings -Force
    }
}

$allLogs = @(Get-ChildItem -LiteralPath $out -Filter "manual-$Action*.log" -File -ErrorAction SilentlyContinue)
$findings = @($allLogs | Select-String -Pattern 'VUID-|SYNC-HAZARD|DEVICE_LOST|software fallback' -ErrorAction SilentlyContinue)
Add-Content -LiteralPath $harness -Value "validation findings: $($findings.Count)"
if ($findings.Count -gt 0) { exit 1 }
if ($Action -eq 'speed') {
    $speedEvidence = @($allLogs | Select-String -Pattern 'fallback=not normal speed|targetScheduling=off|boundedWait=off' -ErrorAction SilentlyContinue)
    Add-Content -LiteralPath $harness -Value "runtime speed-path evidence: $($speedEvidence.Count)"
    if ($speedEvidence.Count -eq 0) { exit 1 }
}
Write-Host "manual action $Action PASS; validation findings=$($findings.Count)"
exit 0
