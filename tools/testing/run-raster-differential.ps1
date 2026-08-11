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

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class RasterDiffNative {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);
}
'@

$oldDifferential = $env:MELONPRIME_RASTER_DIFFERENTIAL
$env:MELONPRIME_RASTER_DIFFERENTIAL = '1'
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

    if ($LoadSlot -gt 0) {
        Start-Sleep -Seconds 3
        $virtualKey = 0x70 + $LoadSlot - 1
        [RasterDiffNative]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
        [RasterDiffNative]::PostMessage($process.MainWindowHandle, 0x0100, [IntPtr]$virtualKey, [IntPtr]0) | Out-Null
        [RasterDiffNative]::PostMessage($process.MainWindowHandle, 0x0101, [IntPtr]$virtualKey, [IntPtr]0) | Out-Null
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

$mismatches = @($lines | Where-Object { $_ -match 'mismatchedPixels=([1-9][0-9]*)' })
if ($mismatches.Count -ne 0) {
    $mismatches | Select-Object -First 10 | ForEach-Object { Write-Error $_ }
    throw "$Renderer raster differential failed in $($mismatches.Count) frame(s)."
}
if (-not ($lines | Where-Object { $_ -match 'nonZeroPixels=([1-9][0-9]*)' })) {
    throw "$Renderer raster differential only observed empty frames."
}

$lines | Select-Object -Last 5
Write-Output "PASS: $Renderer 1x native 3D output exactly matched Software."
