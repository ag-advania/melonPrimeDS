# audit-color-dialog-prefs.ps1
#
# Ensures QColorDialog usage stays inside MelonPrimeColorDialogPrefs.cpp.
# Custom HUD color pickers must call MelonPrime::ColorDialogPrefs::getColor().

param(
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$qtSdl = Join-Path $repoRoot 'src/frontend/qt_sdl'
$allowedRel = 'src/frontend/qt_sdl/MelonPrimeColorDialogPrefs.cpp'

$getColorPattern = 'QColorDialog::getColor'
$includePattern = '#include\s+<QColorDialog>'

$badGetColor = @()
$badInclude = @()

$sourceFiles = Get-ChildItem -Path $qtSdl -Recurse -File -Include '*.cpp','*.h','*.hpp','*.inc'
foreach ($file in $sourceFiles) {
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName) -replace '\\', '/'
    if ($rel -eq $allowedRel) { continue }

    $lineNo = 0
    foreach ($line in [System.IO.File]::ReadLines($file.FullName)) {
        $lineNo++
        if ($line -match $getColorPattern) {
            $badGetColor += [pscustomobject]@{
                File = $rel
                Line = $lineNo
                Text = $line.Trim()
            }
        }
        if ($line -match $includePattern) {
            $badInclude += [pscustomobject]@{
                File = $rel
                Line = $lineNo
                Text = $line.Trim()
            }
        }
    }
}

$pass = ($badGetColor.Count -eq 0) -and ($badInclude.Count -eq 0)

if ($Json) {
    [pscustomobject]@{
        Pass = $pass
        AllowedFile = $allowedRel
        BadGetColor = @($badGetColor)
        BadInclude = @($badInclude)
    } | ConvertTo-Json -Depth 4
} else {
    if ($badGetColor.Count -gt 0) {
        Write-Host "Unexpected QColorDialog::getColor usage:"
        $badGetColor | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
    }
    if ($badInclude.Count -gt 0) {
        Write-Host "Unexpected #include <QColorDialog>:"
        $badInclude | Format-Table -AutoSize | Out-String -Width 240 | Write-Host
    }

    if ($pass) {
        Write-Host "PASS: QColorDialog is confined to $allowedRel"
    } else {
        Write-Error "FAIL: QColorDialog usage must go through MelonPrimeColorDialogPrefs"
    }
}

if (-not $pass) { exit 1 }
