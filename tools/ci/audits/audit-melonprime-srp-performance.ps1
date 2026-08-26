# audit-melonprime-srp-performance.ps1
#
# Enforces MelonPrime SRP/performance contract checks for the v3 immediate plan.
# See docs/architecture/srp-performance-contract.md

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "../../..")
$qtSdl = Join-Path $repoRoot "src/frontend/qt_sdl"
$errors = New-Object System.Collections.Generic.List[string]

function Add-Error([string]$message) {
    $errors.Add($message) | Out-Null
}

function Get-MatchLines([string]$pattern, [string]$path) {
    $matches = New-Object System.Collections.Generic.List[string]

    if (Test-Path -Path $path -PathType Container) {
        $files = Get-ChildItem -Path $path -Recurse -File -Include `
            '*.cpp','*.h','*.hpp','*.inc','*.ps1'
    } else {
        $files = @(Get-Item -Path $path)
    }

    foreach ($file in $files) {
        $hits = @(Select-String -Path $file.FullName -Pattern $pattern)
        foreach ($hit in $hits) {
            $rel = [System.IO.Path]::GetRelativePath($repoRoot, $hit.Path) -replace '\\', '/'
            $matches.Add("${rel}:$($hit.LineNumber):$($hit.Line.Trim())") | Out-Null
        }
    }

    return @($matches)
}

$screen = Join-Path $qtSdl "Screen.cpp"
$vulkanScreen = Join-Path $qtSdl "MelonPrimeScreenVulkan.cpp"

$screenForbiddenIncludes = Get-MatchLines '#include\s+"MelonPrime(Patch|Arm9Hook)' $screen
foreach ($line in $screenForbiddenIncludes) {
    Add-Error "Screen.cpp must not include patch/hook internals: $line"
}

$qcolorRefs = Get-MatchLines 'QColorDialog::getColor|#include\s+<QColorDialog>' $qtSdl
foreach ($line in $qcolorRefs) {
    if ($line -notmatch 'MelonPrimeColorDialogPrefs\.cpp') {
        Add-Error "QColorDialog must stay in MelonPrimeColorDialogPrefs.cpp: $line"
    }
}

$rawAimRefs = Get-MatchLines 'IsPlatformRawAimActive' $qtSdl
foreach ($line in $rawAimRefs) {
    if ($line -match 'Screen.cpp' -and $line -notmatch '__linux__|__APPLE__') {
        Write-Host "Raw aim reference in Screen.cpp requires manual platform-guard review: $line"
    }
}

# The DX12 and Vulkan native radar colour-key passes bypass the CPU HUD image.
# They must therefore carry their own Custom HUD visibility gate; otherwise a
# remembered BtmOverlayEnable paints the bottom-screen radar over the top LCD
# even while CustomHUD=false.
foreach ($nativeScreen in @($screen, $vulkanScreen)) {
    $text = Get-Content -LiteralPath $nativeScreen -Raw
    if ($text -notmatch 'if\s*\(gpuFrame\s*&&\s*hudVisible\s*&&\s*m_radarEnable\)') {
        $relative = [System.IO.Path]::GetRelativePath($repoRoot, $nativeScreen) -replace '\\', '/'
        Add-Error "native radar must remain gated by hudVisible: $relative"
    }
}

# --- Rendering backend ownership -------------------------------------------
#
# The compositor declares itself the owner of the GPU2D output resource set and
# of publication state. This checks the other half of that claim: that nothing
# outside it writes them.
#
# Mutation only. Reading Gpu2D.Output is legitimate -- DX12 assembles one shared
# UAV table out of raster and slot resources, and Vulkan's rasterizer set-0
# write binds slot 0's structured input -- so a read ban would forbid the
# descriptor contract this backend actually has.
$rendererSources = @(
    (Join-Path $repoRoot "src/GPU3D_DX12.cpp"),
    (Join-Path $repoRoot "src/GPU3D_DX12.h"),
    (Join-Path $repoRoot "src/GPU3D_Vulkan.cpp"),
    (Join-Path $repoRoot "src/GPU3D_Vulkan.h")
)

# Assignment to a publication field, or to Output itself.
#
# The lookahead is `(?!=)` rather than `[^=]`. A character class needs one more
# character on the same line, so it misses the assignment an ordinary formatter
# produces:
#
#     Gpu2D.Output =
#         candidate;
#
# The lookahead matches an `=` at end of line too, while still rejecting `==`
# and `!=`, which are reads and must stay legal.
$ownershipMutations = @(
    'Gpu2D\.ComposedOutputValid\s*=(?!=)',
    'Gpu2D\.ComposedGeneration\s*=(?!=)',
    'Gpu2D\.PublishedOutputGeneration\s*=(?!=)',
    'Gpu2D\.LastComposeResult\s*=(?!=)',
    'Gpu2D\.Output\s*=(?!=)',
    'Gpu2D\.Output\.reset\s*\(',
    'std::make_shared<DX12Gpu2DOutput>',
    'std::make_shared<VulkanGpu2DOutput>'
)

foreach ($rendererSource in $rendererSources) {
    if (-not (Test-Path -LiteralPath $rendererSource)) { continue }
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $rendererSource) -replace '\\', '/'
    foreach ($pattern in $ownershipMutations) {
        foreach ($line in (Get-MatchLines $pattern $rendererSource)) {
            Add-Error ("GPU2D publication state is the compositor's: use a named " +
                "operation (RecreateOutput / ReleaseOutput / ResetForRendererEpoch / " +
                "MarkFatal) instead of writing it from ${relative}: $line")
        }
    }

    # The output resource generation is the compositor's lifetime identity. It
    # lived on the renderer once; a reappearance means the counter and the
    # creation it numbers have drifted apart again.
    foreach ($line in (Get-MatchLines 'NextOutputResourceGeneration' $rendererSource)) {
        Add-Error ("output resource generation belongs to the compositor, not " +
            "${relative}: $line")
    }
}

# And the compositor must actually offer those operations, so the check above
# cannot be satisfied by deleting the call sites.
$composerHeaders = @(
    (Join-Path $repoRoot "src/DX12Gpu2DComposer.h"),
    (Join-Path $repoRoot "src/VulkanGpu2DComposer.h")
)
foreach ($composerHeader in $composerHeaders) {
    if (-not (Test-Path -LiteralPath $composerHeader)) { continue }
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $composerHeader) -replace '\\', '/'
    # Declaration shape, not word presence. Checking that the name appears
    # anywhere in the file passes on a comment that merely mentions it, which
    # would let someone delete the operation and keep the audit green -- the
    # exact failure this ratchet exists to prevent. Not a C++ parser: just
    # enough that a comment alone cannot satisfy it.
    foreach ($declaration in @(
        @{ Name = 'RecreateOutput'; Pattern = '^\s*bool\s+RecreateOutput\s*\(' },
        @{ Name = 'ReleaseOutput'; Pattern = '^\s*void\s+ReleaseOutput\s*\(' },
        @{ Name = 'ResetForRendererEpoch'; Pattern = '^\s*void\s+ResetForRendererEpoch\s*\(' },
        @{ Name = 'MarkFatal'; Pattern = '^\s*void\s+MarkFatal\s*\(' },
        @{ Name = 'HasValidOutput'; Pattern = '^\s*\[\[nodiscard\]\]\s+bool\s+HasValidOutput\s*\(' },
        @{ Name = 'GetPublishedOutputGeneration'; Pattern = '^\s*\[\[nodiscard\]\]\s+u64\s+GetPublishedOutputGeneration\s*\(' },
        @{ Name = 'GetLastComposeResult'; Pattern = '^\s*\[\[nodiscard\]\]\s+GPU2DComposeResult\s+GetLastComposeResult\s*\(' },
        @{ Name = 'GetComposedOutput'; Pattern = '^\s*\[\[nodiscard\]\]\s+RendererOutput\s+GetComposedOutput\s*\(' },
        @{ Name = 'AcquireComposedOutputLease'; Pattern = '^\s*\[\[nodiscard\]\]\s+RendererOutputLease\s+AcquireComposedOutputLease\s*\(' })) {
        if ((Get-MatchLines $declaration.Pattern $composerHeader).Count -eq 0) {
            Add-Error ("compositor lifecycle operation $($declaration.Name)() is not " +
                "declared in ${relative}")
        }
    }

    # Same reasoning for the resource generation counter: a member, not a word.
    if ((Get-MatchLines '^\s*u64\s+NextOutputResourceGeneration\s*=\s*1\s*;' $composerHeader).Count -eq 0) {
        Add-Error ("the output resource generation counter is not declared in " +
            "${relative}")
    }
}


if ($errors.Count -ne 0) {
    foreach ($e in $errors) {
        Write-Error $e
    }
    exit 1
}

Write-Host "MelonPrime SRP/performance audit passed."
