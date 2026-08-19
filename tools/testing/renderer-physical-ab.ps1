<#
.SYNOPSIS
    Run one fixed-condition physical renderer A/B measurement.

.DESCRIPTION
    Keeps the ROM, savestate, renderer, scale, VSync, low-latency setting,
    action sequence, and measurement window explicit. The temporary config and
    layer settings are restored byte-for-byte and run artifacts are never
    overwritten.
#>
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Vulkan', 'DX12', 'OpenGLCompute')]
    [string]$Renderer,
    [Parameter(Mandatory = $true)]
    [string]$Rom,
    [string]$Savestate = '',
    [string]$BuildDir = 'build\rebuild-mingw-x86_64',
    [Parameter(Mandatory = $true)]
    [string]$RunId,
    [Parameter(Mandatory = $true)]
    [string]$OutputDir,
    [ValidateSet(1, 4, 16)]
    [int]$Scale = 4,
    [switch]$NoVSync,
    [ValidateSet('Off', 'Reflex', 'ReflexBoost', 'AntiLag2', 'XeLL')]
    [string]$LowLatency = 'Off',
    [ValidateSet('steady-state', 'weapon-switch', 'projectile-burst',
        'room-transition', 'scoreboard', 'display-capture', 'reset',
        'savestate-load', 'all')]
    [string]$Action = 'all',
    [int]$ActionSeed = 0,
    [ValidateSet('On', 'Off')]
    [string]$Hud = 'On',
    [int]$WarmupSeconds = 15,
    [int]$MeasuredSeconds = 20,
    [int]$GraceSeconds = 15
)

$ErrorActionPreference = 'Stop'
if ($WarmupSeconds -lt 1 -or $MeasuredSeconds -lt 1 -or $GraceSeconds -lt 0) {
    throw 'WarmupSeconds and MeasuredSeconds must be positive; GraceSeconds must not be negative.'
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public struct MpRendererPerfRect { public int Left, Top, Right, Bottom; }
public static class MpRendererPerfWin {
  public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern int GetWindowTextLength(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out MpRendererPerfRect rect);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
  public const uint KeyUp = 0x0002;
  public static IntPtr Find(uint wantedPid) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint owner; GetWindowThreadProcessId(h, out owner);
      if (owner == wantedPid && IsWindowVisible(h) && GetWindowTextLength(h) > 0) {
        found = h; return false;
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static void HoldKey(byte key, int milliseconds) {
    keybd_event(key, 0, 0, UIntPtr.Zero);
    System.Threading.Thread.Sleep(milliseconds);
    keybd_event(key, 0, KeyUp, UIntPtr.Zero);
  }
}
"@

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$build = (Resolve-Path (Join-Path $repo $BuildDir)).Path
$exe = Join-Path $build 'melonPrimeDS.exe'
$romPath = (Resolve-Path $Rom).Path
$statePath = if ([string]::IsNullOrWhiteSpace($Savestate)) { $null } else { (Resolve-Path $Savestate).Path }
$out = (Resolve-Path (New-Item -ItemType Directory -Force -Path $OutputDir)).Path
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Executable not found: $exe" }

$rendererId = switch ($Renderer) {
    'OpenGLCompute' { 2 }
    'Vulkan' { 3 }
    'DX12' { 4 }
}
$useGL = if ($Renderer -eq 'OpenGLCompute') { 'true' } else { 'false' }
$reflexMode = switch ($LowLatency) {
    'Reflex' { 1 }
    'ReflexBoost' { 2 }
    default { 0 }
}
$antiLag = if ($LowLatency -eq 'AntiLag2') { 'true' } else { 'false' }
$xellEnabled = if ($LowLatency -eq 'XeLL') { 'true' } else { 'false' }
$xellPolicy = if ($LowLatency -eq 'XeLL') { 4 } else { 0 }
$vsyncName = if ($NoVSync) { 'off' } else { 'on' }
$portableDir = Join-Path $build 'portable'
$configRoot = if (Test-Path -LiteralPath $portableDir -PathType Container) { $portableDir } else { $build }
$configPath = Join-Path $configRoot 'melonDS.toml'
$layerSettings = Join-Path $build 'vk_layer_settings.txt'
$csv = Join-Path $out "$RunId.csv"
$stdout = Join-Path $out "$RunId.out.log"
$stderr = Join-Path $out "$RunId.err.log"
$harness = Join-Path $out "$RunId.harness.log"
$metadata = Join-Path $out "$RunId.metadata.txt"
$screenshot = Join-Path $out "$RunId.display.png"
foreach ($path in @($csv, $stdout, $stderr, $harness, $metadata, $screenshot)) {
    if (Test-Path -LiteralPath $path) { throw "Refusing to overwrite artifact: $path" }
}

$hadConfig = Test-Path -LiteralPath $configPath
$originalConfig = if ($hadConfig) { [IO.File]::ReadAllBytes($configPath) } else { $null }
$hadLayer = Test-Path -LiteralPath $layerSettings
$originalLayer = if ($hadLayer) { [IO.File]::ReadAllBytes($layerSettings) } else { $null }
$oldPerf = $env:MELONPRIME_PERF
$oldRunId = $env:MELONPRIME_LATENCY_RUN_ID
$oldCsv = $env:MELONPRIME_LATENCY_CSV
$oldState = $env:MELONPRIME_TEST_SAVESTATE
$oldHud = $env:MELONPRIME_TEST_CUSTOM_HUD_OFF
$proc = $null
$window = [IntPtr]::Zero
$configRestored = -not $hadConfig
$layerRestored = -not $hadLayer
$startedUtc = [DateTime]::UtcNow
$utf8 = [Text.UTF8Encoding]::new($false)

$cfg = @"
3D.Renderer = $rendererId
3D.GL.ScaleFactor = $Scale
3D.GL.HiresCoordinates = true
Screen.UseGL = $useGL
Screen.VSync = $(if ($NoVSync) { 'false' } else { 'true' })
Screen.VSyncInterval = 1
LimitFPS = true
TargetFPS = 60.0
3D.Vulkan.PresentPacingPolicy = 0
3D.DX12.NvidiaReflexMode = $reflexMode
3D.AMD.AntiLag2Enabled = $antiLag
3D.Intel.XeLLEnabled = $xellEnabled
3D.Intel.XeLLPacingPolicy = $xellPolicy
Emu.DirectBoot = true
Emu.ExternalBIOSEnable = false

[Instance0]

[Instance0.Keyboard]
HK_Reset = 82
HK_MetroidMenu = 9
HK_MetroidWeaponMissile = 73
HK_MetroidShootScan = 70
HK_MetroidMoveForward = 87
HK_MetroidMoveBack = 83
HK_MetroidMoveLeft = 65
HK_MetroidMoveRight = 68
HK_MetroidWeapon1 = 49
HK_MetroidWeapon2 = 50
HK_MetroidWeapon3 = 51
HK_MetroidWeapon4 = 52
HK_MetroidWeapon5 = 53
HK_MetroidWeapon6 = 54
"@

function Focus-RendererWindow {
    if ($window -eq [IntPtr]::Zero) { throw 'renderer window was not found' }
    [void][MpRendererPerfWin]::SetForegroundWindow($window)
    Start-Sleep -Milliseconds 100
}

function Send-Key([string]$keys) {
    Focus-RendererWindow
    [System.Windows.Forms.SendKeys]::SendWait($keys)
    Start-Sleep -Milliseconds 150
}

function Capture-Display {
    $rect = [MpRendererPerfRect]::new()
    if (-not [MpRendererPerfWin]::GetWindowRect($window, [ref]$rect)) { return $false }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { return $false }
    $bitmap = New-Object System.Drawing.Bitmap($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try { [void][MpRendererPerfWin]::PrintWindow($window, $dc, 2) }
    finally { $graphics.ReleaseHdc($dc); $graphics.Dispose() }
    $bitmap.Save($screenshot, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    return $true
}

function Run-Action([string]$name) {
    Add-Content -LiteralPath $harness -Value "action=$name sent_utc=$([DateTime]::UtcNow.ToString('o'))"
    switch ($name) {
        'steady-state' { Start-Sleep -Seconds 3 }
        'weapon-switch' { for ($i = 0; $i -lt 3; $i++) { Send-Key '123456654321' } }
        'projectile-burst' {
            Send-Key 'i'
            Focus-RendererWindow
            [MpRendererPerfWin]::HoldKey(70, 3500)
            Add-Content -LiteralPath $harness -Value 'projectile-fire-key-held=F duration_ms=3500'
        }
        'room-transition' {
            Focus-RendererWindow
            [MpRendererPerfWin]::HoldKey(87, 3500)
            [MpRendererPerfWin]::HoldKey(68, 1500)
            [MpRendererPerfWin]::HoldKey(83, 2500)
            Add-Content -LiteralPath $harness -Value 'movement-sequence=W3.5s,D1.5s,S2.5s'
        }
        'scoreboard' { Send-Key '{TAB}'; Start-Sleep -Seconds 2; Send-Key '{TAB}' }
        'display-capture' {
            if (Capture-Display) { Add-Content -LiteralPath $harness -Value "display-capture=$screenshot" }
            else { Add-Content -LiteralPath $harness -Value 'display-capture=FAIL' }
        }
        'reset' { Send-Key 'r'; Start-Sleep -Seconds 3 }
        'savestate-load' {
            if ($null -eq $statePath) { Add-Content -LiteralPath $harness -Value 'savestate-load=NOT_REQUESTED' }
            else { Add-Content -LiteralPath $harness -Value "diagnostic-state=$statePath (developer hook)"; Start-Sleep -Seconds 3 }
        }
        default { throw "Unsupported action: $name" }
    }
}

$actionSequence = if ($Action -eq 'all') {
    @('steady-state', 'weapon-switch', 'projectile-burst', 'room-transition',
        'scoreboard', 'display-capture', 'reset', 'savestate-load')
} else {
    @($Action)
}
if ($Action -eq 'all' -and $ActionSeed -ne 0) {
    $random = [System.Random]::new($ActionSeed)
    for ($i = $actionSequence.Count - 1; $i -gt 0; $i--) {
        $j = $random.Next($i + 1)
        $temporary = $actionSequence[$i]
        $actionSequence[$i] = $actionSequence[$j]
        $actionSequence[$j] = $temporary
    }
}
$actionOrder = [string]::Join(',', [string[]]$actionSequence)

try {
    [IO.File]::WriteAllText($configPath, $cfg, $utf8)
    $header = 'renderer=' + $Renderer + [Environment]::NewLine +
        'scale=' + $Scale + [Environment]::NewLine +
        'vsync=' + $vsyncName + [Environment]::NewLine +
        'low_latency=' + $LowLatency + [Environment]::NewLine +
        'hud=' + $Hud + [Environment]::NewLine +
        'action=' + $Action + [Environment]::NewLine +
        'action_seed=' + $ActionSeed + [Environment]::NewLine +
        'action_order=' + $actionOrder + [Environment]::NewLine
    [IO.File]::WriteAllText($harness, $header, $utf8)
    if ($Renderer -eq 'Vulkan') {
        $env:MELONPRIME_LATENCY_RUN_ID = $RunId
        $env:MELONPRIME_LATENCY_CSV = $csv
    } else {
        Remove-Item Env:MELONPRIME_LATENCY_RUN_ID -ErrorAction SilentlyContinue
        Remove-Item Env:MELONPRIME_LATENCY_CSV -ErrorAction SilentlyContinue
    }
    $env:MELONPRIME_PERF = '1'
    if ($null -ne $statePath) { $env:MELONPRIME_TEST_SAVESTATE = $statePath } else { Remove-Item Env:MELONPRIME_TEST_SAVESTATE -ErrorAction SilentlyContinue }
    if ($Hud -eq 'Off') { $env:MELONPRIME_TEST_CUSTOM_HUD_OFF = '1' } else { Remove-Item Env:MELONPRIME_TEST_CUSTOM_HUD_OFF -ErrorAction SilentlyContinue }

    $proc = Start-Process -FilePath $exe -ArgumentList ('"' + $romPath + '"') -WorkingDirectory $build -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $deadline = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $window = [MpRendererPerfWin]::Find([uint32]$proc.Id)
        if ($window -ne [IntPtr]::Zero) { break }
        if ($proc.HasExited) { throw "renderer exited before window creation (exit=$($proc.ExitCode))" }
        Start-Sleep -Milliseconds 250
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($window -eq [IntPtr]::Zero) { throw 'renderer window did not appear' }
    Start-Sleep -Seconds $WarmupSeconds
    Add-Content -LiteralPath $harness -Value "warmup_complete_utc=$([DateTime]::UtcNow.ToString('o'))"
    foreach ($name in $actionSequence) { Run-Action $name }
    Start-Sleep -Seconds $MeasuredSeconds
    if ($GraceSeconds -gt 0) {
        Start-Sleep -Seconds $GraceSeconds
        Add-Content -LiteralPath $harness -Value "grace_complete_utc=$([DateTime]::UtcNow.ToString('o'))"
    }
}
finally {
    if ($null -ne $proc -and -not $proc.HasExited) {
        [void]$proc.CloseMainWindow()
        if (-not $proc.WaitForExit(20000)) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    }
    if ($null -ne $proc -and -not $proc.HasExited) { Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue }
    if ($hadConfig) {
        [IO.File]::WriteAllBytes($configPath, $originalConfig)
        $configRestored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($configPath)) -ceq [Convert]::ToBase64String($originalConfig)
    } elseif (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
        $configRestored = -not (Test-Path -LiteralPath $configPath)
    }
    if ($hadLayer) {
        [IO.File]::WriteAllBytes($layerSettings, $originalLayer)
        $layerRestored = [Convert]::ToBase64String([IO.File]::ReadAllBytes($layerSettings)) -ceq [Convert]::ToBase64String($originalLayer)
    } elseif (Test-Path -LiteralPath $layerSettings) {
        Remove-Item -LiteralPath $layerSettings -Force
        $layerRestored = -not (Test-Path -LiteralPath $layerSettings)
    }
    if ($null -eq $oldPerf) { Remove-Item Env:MELONPRIME_PERF -ErrorAction SilentlyContinue } else { $env:MELONPRIME_PERF = $oldPerf }
    if ($null -eq $oldRunId) { Remove-Item Env:MELONPRIME_LATENCY_RUN_ID -ErrorAction SilentlyContinue } else { $env:MELONPRIME_LATENCY_RUN_ID = $oldRunId }
    if ($null -eq $oldCsv) { Remove-Item Env:MELONPRIME_LATENCY_CSV -ErrorAction SilentlyContinue } else { $env:MELONPRIME_LATENCY_CSV = $oldCsv }
    if ($null -eq $oldState) { Remove-Item Env:MELONPRIME_TEST_SAVESTATE -ErrorAction SilentlyContinue } else { $env:MELONPRIME_TEST_SAVESTATE = $oldState }
    if ($null -eq $oldHud) { Remove-Item Env:MELONPRIME_TEST_CUSTOM_HUD_OFF -ErrorAction SilentlyContinue } else { $env:MELONPRIME_TEST_CUSTOM_HUD_OFF = $oldHud }
}

$exitCode = -1
if ($null -ne $proc) {
    try { $proc.Refresh(); if ($proc.HasExited) { $exitCode = [int]$proc.ExitCode } } catch { }
}
$captureRows = 0
if (Test-Path -LiteralPath $csv) { $captureRows = [Math]::Max(0, (@(Get-Content -LiteralPath $csv).Count - 1)) }
$allLog = @()
foreach ($path in @($stdout, $stderr)) { if (Test-Path -LiteralPath $path) { $allLog += Get-Content -LiteralPath $path -ErrorAction SilentlyContinue } }
$badMarkers = @($allLog | Select-String -Pattern 'VUID-|SYNC-HAZARD|DEVICE_LOST|GPU failure|command submission failed|Renderer fatal' -ErrorAction SilentlyContinue)
$stateMarker = if ($null -ne $statePath) { @($allLog | Select-String -SimpleMatch "[SavestateDiff] path=$statePath loaded=1" -ErrorAction SilentlyContinue).Count } else { 0 }
$hudOffMarker = if ($Hud -eq 'Off') { @($allLog | Select-String -SimpleMatch '[SavestateDiff] customHudForcedOff=1' -ErrorAction SilentlyContinue).Count } else { 0 }
$metadataText = @"
run_id=$RunId
renderer=$Renderer
renderer_id=$rendererId
scale=$Scale
vsync=$vsyncName
low_latency=$LowLatency
hud=$Hud
action=$Action
action_seed=$ActionSeed
action_order=$actionOrder
warmup_seconds=$WarmupSeconds
measured_seconds=$MeasuredSeconds
grace_seconds=$GraceSeconds
process_exit_code=$exitCode
config_restore=$(if ($configRestored) { 'PASS' } else { 'FAIL' })
layer_settings_restore=$(if ($layerRestored) { 'PASS' } else { 'FAIL' })
savestate=$(if ($null -ne $statePath) { $statePath } else { 'NONE' })
savestate_loaded_marker=$stateMarker
custom_hud_off_marker=$hudOffMarker
capture_rows=$captureRows
bad_marker_count=$($badMarkers.Count)
source_head=$(git -C "$repo" rev-parse HEAD)
executable=$exe
csv=$csv
stdout=$stdout
stderr=$stderr
harness=$harness
display_capture=$screenshot
"@
[IO.File]::WriteAllText($metadata, $metadataText, $utf8)

Write-Host "run_id             : $RunId"
Write-Host "renderer           : $Renderer"
Write-Host "scale              : $Scale"
Write-Host "vsync              : $vsyncName"
Write-Host "low latency        : $LowLatency"
Write-Host "process exit       : $exitCode"
Write-Host "config restore     : $(if ($configRestored) { 'PASS' } else { 'FAIL' })"
Write-Host "state marker       : $stateMarker"
Write-Host "capture rows       : $captureRows"
Write-Host "bad markers        : $($badMarkers.Count)"
if (-not $configRestored -or -not $layerRestored -or $exitCode -ne 0 -or $badMarkers.Count -ne 0 -or
    ($null -ne $statePath -and $stateMarker -eq 0) -or
    ($Hud -eq 'Off' -and $hudOffMarker -eq 0) -or
    ($Renderer -eq 'Vulkan' -and $captureRows -lt 1)) {
    $badMarkers | Select-Object -First 20 | ForEach-Object { Write-Host $_.Line }
    exit 1
}
exit 0
