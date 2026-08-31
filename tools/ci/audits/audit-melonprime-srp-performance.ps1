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
            '*.cpp','*.h','*.hpp','*.inc','*.mm','*.c','*.m','*.ps1'
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

function Get-CodeMatchLines([string]$pattern, [string]$path) {
    # Same as Get-MatchLines, minus whole-line comments. A doc comment naming an
    # API is not a declaration, so counting it would let a rule pass on prose,
    # and it is not a violation either, so banning it would fire on the comment
    # that explains where the API went. Strip trailing comments as well so an
    # inline note cannot satisfy a wiring rule.
    return @(Get-MatchLines $pattern $path | Where-Object {
        $text = ($_ -split ':', 3)[2] -replace '//.*$', ''
        $text -notmatch '^\s*(//|\*|/\*)' -and
            $text.Trim().Length -gt 0 -and
            $text -match $pattern
    })
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


# =============================================================================
#  MelonPrime SRP ownership ratchets (MP-SRP-001..004)
#
#  These are grep ratchets, not a C++ parser. Rules A-C, E, and F are shaped so
#  a false positive needs someone to write the banned name in the banned file,
#  so they hard-fail. Rule D cannot be decided by grep and only prints for
#  review.
# =============================================================================

# --- Rule A: ARM9 instruction patching left MelonPrimeGameSettings ----------
#
# Aim smoothing is an ARM9 instruction patch, not a game setting. It lives in
# MelonPrimePatchAimSmoothing.cpp with the other patch modules. Game settings
# still write RAM (ApplyUnlockHuntersMaps and friends), so this bans the patch
# vocabulary rather than ARM9 writes in general.
$gameSettings = Join-Path $qtSdl "MelonPrimeGameSettings.cpp"
$gameSettingsHeader = Join-Path $qtSdl "MelonPrimeGameSettings.h"
foreach ($settingsFile in @($gameSettings, $gameSettingsHeader)) {
    if (-not (Test-Path -LiteralPath $settingsFile)) { continue }
    foreach ($line in (Get-MatchLines 'aimPatch|ApplyAimSmoothingPatch|JUMP_INSTR' $settingsFile)) {
        Add-Error ("ARM9 instruction patching belongs in a patch module, not in " +
            "game settings -- see MelonPrimePatchAimSmoothing: $line")
    }
}

# And the patch module has to actually exist, so the rule above cannot be
# satisfied by deleting the feature.
$aimSmoothingHeader = Join-Path $qtSdl "MelonPrimePatchAimSmoothing.h"
if ((Get-CodeMatchLines '^\s*void\s+AimSmoothing_ApplyOrRestore\s*\(' $aimSmoothingHeader).Count -eq 0) {
    Add-Error ("AimSmoothing_ApplyOrRestore() is not declared in " +
        "src/frontend/qt_sdl/MelonPrimePatchAimSmoothing.h")
}

# --- Rule B: the Custom HUD render header stays a render header -------------
#
# MelonPrimeHudRender.h used to carry the editor, the native HUD patch
# lifecycle, radar preprocessing and the developer harness as well, which drags
# Qt event types and patch internals into every renderer front-end. Each group
# now has its own header; this stops them draining back.
$hudRenderHeader = Join-Path $qtSdl "MelonPrimeHudRender.h"

# Per function, not per group: a group-wide regex passes as long as one member
# of the group is still declared, which lets the other members quietly move back
# into the render header.
$hudApiOwners = [ordered]@{
    'MelonPrimeHudPresentationState.h' = @('CustomHud_IsEditMode')
    'MelonPrimeHudEdit.h' = @(
        'CustomHud_EnterEditMode', 'CustomHud_ExitEditMode',
        'CustomHud_GetOnScreenEditStyle', 'CustomHud_IsCrosshairElement',
        'CustomHud_UpdateEditContext', 'CustomHud_EditMousePress',
        'CustomHud_EditMouseMove', 'CustomHud_EditMouseRelease',
        'CustomHud_EditMouseWheel', 'CustomHud_SetEditSelectionCallback',
        'CustomHud_GetSelectedElement')
    'MelonPrimeHudPatchLifecycle.h' = @(
        'CustomHud_IsHelmetLayerHideConfigured', 'CustomHud_ClampHelmetLayersPreFrame',
        'CustomHud_EnsurePatchRestored', 'CustomHud_ResetPatchState',
        'CustomHud_ReconcilePatchAfterSavestateLoad')
    'MelonPrimeHudRadar.h' = @(
        'CustomHud_PrepareRadarColorKeySource', 'CustomHud_ResolveRadarColorKeyRadius')
    'MelonPrimeHudRuntime.h' = @(
        'CustomHud_ShouldHideForGameplayState', 'CustomHud_ShouldDrawRadarOverlay',
        'CustomHud_GetVisualGeneration', 'CustomHud_GetVisualGameFrame',
        'CustomHud_OnMatchJoin')
    'MelonPrimeHudGoldenHarness.h' = @('CustomHud_RunGoldenHarness')
}
foreach ($owner in $hudApiOwners.Keys) {
    $ownerPath = Join-Path $qtSdl $owner
    if (-not (Test-Path -LiteralPath $ownerPath)) {
        Add-Error "Custom HUD API header $owner is missing"
        continue
    }
    foreach ($api in $hudApiOwners[$owner]) {
        $pattern = "\b$api\s*\("
        foreach ($line in (Get-CodeMatchLines $pattern $hudRenderHeader)) {
            Add-Error ("MelonPrimeHudRender.h is the render entry point; " +
                "$api belongs in ${owner}: $line")
        }
        # The other half: the owner must actually declare it, so the ban above
        # cannot be satisfied by deleting the API instead of moving it.
        if ((Get-CodeMatchLines $pattern $ownerPath).Count -eq 0) {
            Add-Error "$owner no longer declares $api"
        }
    }
}

# One declaration, not two: MelonPrimeHudEdit.h includes the presentation header
# instead of repeating the query, so a consumer cannot pick the heavy header up
# by accident and still satisfy Rule B.
$hudEditHeader = Join-Path $qtSdl "MelonPrimeHudEdit.h"
foreach ($line in (Get-CodeMatchLines '\bCustomHud_IsEditMode\s*\(' $hudEditHeader)) {
    Add-Error ("CustomHud_IsEditMode is declared by MelonPrimeHudPresentationState.h; " +
        "MelonPrimeHudEdit.h includes that header rather than redeclaring it: $line")
}
if ((Get-MatchLines '#include\s+"MelonPrimeHudPresentationState\.h"' $hudEditHeader).Count -eq 0) {
    Add-Error ("MelonPrimeHudEdit.h must include MelonPrimeHudPresentationState.h so " +
        "its consumers still see CustomHud_IsEditMode")
}

# --- Rule B2: renderer front-ends stay off the editor header ----------------
#
# MelonPrimeHudEdit.h carries <QMouseEvent> and <functional> for mouse routing
# and the selection callback. A presenter only ever asks whether edit mode is
# open, which MelonPrimeHudPresentationState.h answers without Qt event types.
# This also catches a future presenter reaching for real editor API.
$presenterSources = @(
    (Join-Path $qtSdl "MelonPrimeScreenVulkan.cpp"),
    (Join-Path $qtSdl "MelonPrimeScreenMetal.mm")
)
foreach ($presenter in $presenterSources) {
    if (-not (Test-Path -LiteralPath $presenter)) { continue }
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $presenter) -replace '\\', '/'
    foreach ($line in (Get-MatchLines '#include\s+"MelonPrimeHudEdit\.h"' $presenter)) {
        Add-Error ("renderer front-ends must not take the editor header; " +
            "CustomHud_IsEditMode lives in MelonPrimeHudPresentationState.h: $line")
    }
    # And they must still be asking the question, so the ban cannot be satisfied
    # by dropping the edit-mode gate that keeps the editor overlay visible.
    if ((Get-CodeMatchLines '\bCustomHud_IsEditMode\s*\(' $presenter).Count -eq 0) {
        Add-Error ("${relative} no longer consults CustomHud_IsEditMode; the " +
            "editor overlay gate is gone")
    }
}

# --- Rule E: sampling stays free of presentation-owned types ----------------
#
# RuntimeSample is the RAM/game-semantics side of the split.  Its single frame
# allocation is aggregated by a neutral child fragment, so moving the owner did
# not change allocation count while keeping render-plan and Qt presentation
# types out of this source fragment.
$hudRuntimeSample = Join-Path $qtSdl "MelonPrimeHudRuntimeSample.inc"
$runtimeSampleForbiddenTypes = @(
    'QFont', 'QFontMetrics', 'QPainter', 'QPainterPath',
    'TextBitmapCache', 'TextMeasureCache',
    'ScoreboardRenderPlan', 'EnemyTargetRenderPlan',
    'MatchStatusStringCache', 'RankStringCache', 'TimeStringCache'
)
foreach ($typeName in $runtimeSampleForbiddenTypes) {
    foreach ($line in (Get-MatchLines "\b$typeName\b" $hudRuntimeSample)) {
        Add-Error ("MelonPrimeHudRuntimeSample.inc must not own or mention " +
            "presentation type ${typeName}; move it to the appropriate unity " +
            "fragment: $line")
    }
}

# --- Rule F: runtime drawing never samples emulated RAM --------------------
#
# RuntimeDraw receives the already-resolved HudRuntimeState snapshot from
# CustomHud_Render.  Keep all direct RAM reads and the legacy nullable fallback
# in RuntimeSample so drawing only chooses how to present resolved values.
$hudRuntimeDraw = Join-Path $qtSdl "MelonPrimeHudRuntimeDraw.inc"
$runtimeDrawForbiddenPatterns = @(
    '\bRead8\s*\(',
    '\bRead16\s*\(',
    '\bRead32\s*\(',
    '\bComputeMatchStatusState\s*\(',

    # Any explicit raw MainRAM pointer declaration, regardless of variable name.
    '(?:const\s+)?melonDS::u8\s*\*\s*\w+',

    # RuntimeDraw receives HudRuntimeState, but must never dereference its raw
    # RAM member itself. Sampling access stays behind RuntimeSample helpers.
    '(?:\.|->)\s*ram\b',

    # Do not reacquire emulated MainRAM through NDS either.
    '\bMainRAM\b',

    # Legacy nullable state fallback is forbidden.
    'const\s+HudRuntimeState\s*\*\s*\w+\s*=\s*nullptr'
)
foreach ($pattern in $runtimeDrawForbiddenPatterns) {
    foreach ($line in (Get-CodeMatchLines $pattern $hudRuntimeDraw)) {
        Add-Error ("MelonPrimeHudRuntimeDraw.inc must delegate RAM sampling to " +
            "RuntimeSample; forbidden pattern ${pattern}: $line")
    }
}

$runtimeDrawText = Get-Content -LiteralPath $hudRuntimeDraw -Raw
foreach ($signature in @(
    @{ Name = 'DrawMatchStatusHud'; Pattern = '(?s)static\s+void\s+DrawMatchStatusHud\s*\([^)]*const\s+HudRuntimeState\s*&\s*st\s*\)' },
    @{ Name = 'DrawRankAndTime'; Pattern = '(?s)static\s+void\s+DrawRankAndTime\s*\([^)]*const\s+HudRuntimeState\s*&\s*st\s*\)' })) {
    if ($runtimeDrawText -notmatch $signature.Pattern) {
        Add-Error ("$($signature.Name) must consume the resolved HudRuntimeState " +
            "by const reference in MelonPrimeHudRuntimeDraw.inc")
    }
}

# --- Rule G: HUD owner slots stay typed and cold-owned ---------------------
#
# Runtime/frame/text state is per CustomHudConfigState. Type erasure or
# lazy make_shared in these slots puts refcount/heap work back on the render
# path and makes ownership invisible to the compiler. The top-level
# m_hudConfigState shared_ptr remains the explicit Core lifetime boundary; this
# rule only covers the internal owner slots and their accessors.
$hudOwnerSources = @(Get-ChildItem -LiteralPath $qtSdl -File |
    Where-Object { $_.Name -like 'MelonPrimeHud*.inc' -or $_.Name -like 'MelonPrimeHud*.h' })
$hudOwnerForbiddenPatterns = @(
    'std::shared_ptr\s*<\s*void\s*>',
    'std::static_pointer_cast\s*<',
    'std::make_shared\s*<\s*(?:HudBattleOwnedState|HudFrameOwnedState|HudElementTextCacheState)'
)
foreach ($source in $hudOwnerSources) {
    foreach ($pattern in $hudOwnerForbiddenPatterns) {
        foreach ($line in (Get-CodeMatchLines $pattern $source.FullName)) {
            Add-Error ("Custom HUD owner slots must use typed cold ownership; " +
                "forbidden ${pattern}: $line")
        }
    }
}
$hudOwnerSlotPath = Join-Path $qtSdl 'MelonPrimeHudRenderConfig.inc'
foreach ($slot in @(
    @{ Type = 'HudBattleOwnedState'; Name = 'runtimeState' },
    @{ Type = 'HudFrameOwnedState'; Name = 'frameState' },
    @{ Type = 'HudElementTextCacheState'; Name = 'textCacheState' })) {
    $slotPattern = "std::unique_ptr\s*<\s*$($slot.Type)\s*>\s+$($slot.Name)\s*;"
    if ((Get-CodeMatchLines $slotPattern $hudOwnerSlotPath).Count -eq 0) {
        Add-Error ("CustomHudConfigState must keep a typed unique_ptr slot for " +
            "$($slot.Name) ($($slot.Type))")
    }
}
$hudOwnerFactoryPath = Join-Path $qtSdl 'MelonPrimeHudRender.cpp'
foreach ($typeName in @('HudBattleOwnedState', 'HudFrameOwnedState',
                         'HudElementTextCacheState')) {
    if ((Get-CodeMatchLines "std::make_unique\s*<\s*$typeName\s*>" $hudOwnerFactoryPath).Count -eq 0) {
        Add-Error ("CustomHudConfigState cold construction must initialize " +
            "$typeName with std::make_unique")
    }
}

# --- Rule J: standalone ARM9 modules stay stateless and config-free ---------
#
# Runtime feature policy is resolved into Arm9HookActivationPlan before the
# match hook is installed. The dispatcher mask gates the handler and the
# per-Core ARM9HookState supplies romGroupIndex; a module must not reintroduce
# a process-global activation context or a second Config interpreter.
$arm9RuntimeModules = @(
    (Join-Path $qtSdl 'MelonPrimePatchShadowFreezeRuntimeHook.cpp'),
    (Join-Path $qtSdl 'MelonPrimePatchShadowFreezeRuntimeHook.h'),
    (Join-Path $qtSdl 'MelonPrimePatchFixNoxusBladePersistence.cpp'),
    (Join-Path $qtSdl 'MelonPrimePatchFixNoxusBladePersistence.h')
)
$arm9ModuleForbiddenPatterns = @(
    '#include\s+"Config\.h"',
    '#include\s+"MelonPrimeDef\.h"',
    '\bConfig::Table\b',
    '\bCfgKey::',
    '\bGet(?:Bool|Int|Double|String)\s*\('
)
$arm9LegacyStatePatterns = @(
    '\bs_activeHooks\b'
)
foreach ($modulePath in $arm9RuntimeModules) {
    if (-not (Test-Path -LiteralPath $modulePath)) {
        Add-Error "ARM9 runtime module is missing: $modulePath"
        continue
    }
    foreach ($pattern in $arm9ModuleForbiddenPatterns) {
        foreach ($line in (Get-CodeMatchLines $pattern $modulePath)) {
            Add-Error ("Shadow/Noxus runtime modules must not reinterpret config; " +
                "forbidden ${pattern}: $line")
        }
    }
}

$arm9RomGroupNames = @('JP1_0', 'JP1_1', 'US1_0', 'US1_1', 'EU1_0', 'EU1_1', 'KR1_0')
foreach ($modulePath in @(
    (Join-Path $qtSdl 'MelonPrimePatchShadowFreezeRuntimeHook.cpp'),
    (Join-Path $qtSdl 'MelonPrimePatchFixNoxusBladePersistence.cpp'))) {
    foreach ($romGroupName in $arm9RomGroupNames) {
        if ((Get-CodeMatchLines "kHooks_$romGroupName" $modulePath).Count -eq 0) {
            Add-Error ("ARM9 runtime module is missing the $romGroupName hook table: " +
                "$modulePath")
        }
    }
    if ((Get-CodeMatchLines 'RomGroup::COUNT' $modulePath).Count -eq 0) {
        Add-Error "ARM9 runtime module must assert RomGroup::COUNT coverage: $modulePath"
    }
}

# Rule J's name-based bans above are intentionally supplemented by a narrow
# semantic scan.  These two runtime modules may contain immutable lookup data
# and static helper functions, but they must not acquire mutable module state
# under a new name (including a function-local static or thread_local).
function Get-MutableStaticDeclarations([string]$source, [string]$label) {
    $code = [regex]::Replace($source, '(?s)/\*.*?\*/', '')
    $code = [regex]::Replace($code, '//[^\r\n]*', '')
    $code = [regex]::Replace($code, '"(?:\\.|[^"\\])*"', '""')
    $staticPattern = '(?<![\w])(?:thread_local\s+static|static\s+thread_local|static)(?![\w])'
    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($match in [regex]::Matches($code, $staticPattern)) {
        $tail = $code.Substring($match.Index)
        $end = $tail.Length
        foreach ($delimiter in @(';', '{', '}')) {
            $candidate = $tail.IndexOf($delimiter)
            if ($candidate -ge 0 -and $candidate -lt $end) { $end = $candidate }
        }
        $declaration = $tail.Substring(0, $end).Trim()
        $isImmutable = $declaration -match '^\s*(?:thread_local\s+static|static\s+thread_local|static)\s+(?:constexpr\b|const\b)'
        $openParen = $declaration.IndexOf('(')
        $equals = $declaration.IndexOf('=')
        $isStaticFunction = $openParen -ge 0 -and ($equals -lt 0 -or $openParen -lt $equals)
        if (-not $isImmutable -and -not $isStaticFunction) {
            $line = 1 + ([regex]::Matches($code.Substring(0, $match.Index), "`n")).Count
            $violations.Add("${label}:${line}:$declaration") | Out-Null
        }
    }
    return @($violations)
}

# Synthetic fixtures exercise both sides of the allowlist so a future edit to
# the scanner cannot silently make it name-based or over-broad.
$statelessFixtures = @(
    @{ Name = 'static constexpr table'; Source = 'static constexpr int kTable[] = { 1, 2 };'; Allowed = $true },
    @{ Name = 'static const data'; Source = 'static const int kValue = 1;'; Allowed = $true },
    @{ Name = 'static helper function'; Source = 'static bool Helper(int value) { return value != 0; }'; Allowed = $true },
    @{ Name = 'mutable static bool'; Source = 'static bool g_enabled = false;'; Allowed = $false },
    @{ Name = 'mutable static byte'; Source = 'static uint8_t activeRom = 0;'; Allowed = $false },
    @{ Name = 'static thread_local'; Source = 'static thread_local bool active = false;'; Allowed = $false },
    @{ Name = 'thread_local static'; Source = 'thread_local static bool active = false;'; Allowed = $false },
    @{ Name = 'mutable static object'; Source = 'static State state{};'; Allowed = $false },
    @{ Name = 'mutable static factory'; Source = 'static State state = MakeState();'; Allowed = $false }
)
foreach ($fixture in $statelessFixtures) {
    $found = @(Get-MutableStaticDeclarations $fixture.Source "fixture/$($fixture.Name)")
    $accepted = $found.Count -eq 0
    if ($accepted -ne $fixture.Allowed) {
        Add-Error ("stateless-static fixture '$($fixture.Name)' expected " +
            "Allowed=$($fixture.Allowed), got Allowed=$accepted")
    }
}

# Keep the older migration bans executable as well as documented. These
# fixtures are intentionally source snippets rather than checks for today's
# module text: they fail the audit if a future edit weakens the Config or
# legacy-hook-symbol ratchet while the production files happen not to contain
# the forbidden text.
function Get-StatelessForbiddenFixtureHits([string]$source, [string]$label) {
    $code = [regex]::Replace($source, '(?s)/\*.*?\*/', '')
    $code = [regex]::Replace($code, '//[^\r\n]*', '')
    $violations = New-Object System.Collections.Generic.List[string]
    foreach ($pattern in @($arm9ModuleForbiddenPatterns + $arm9LegacyStatePatterns)) {
        foreach ($match in [regex]::Matches($code, $pattern)) {
            $line = 1 + ([regex]::Matches($code.Substring(0, $match.Index), "`n")).Count
            $violations.Add("${label}:${line}:$pattern") | Out-Null
        }
    }
    return @($violations)
}

$statelessLegacyFixtures = @(
    @{ Name = 'Config reintroduction'; Source = 'Config::Table cfg;' },
    @{ Name = 'old active hook symbol'; Source = 'static bool s_activeHooks = false;' }
)
foreach ($fixture in $statelessLegacyFixtures) {
    $textHits = @(Get-StatelessForbiddenFixtureHits $fixture.Source "fixture/$($fixture.Name)")
    if ($textHits.Count -eq 0) {
        Add-Error ("stateless legacy fixture '$($fixture.Name)' must be rejected " +
            'by the Rule J forbidden-text ratchet')
    }
    if ($fixture.Name -eq 'old active hook symbol') {
        $staticHits = @(Get-MutableStaticDeclarations $fixture.Source "fixture/$($fixture.Name)")
        if ($staticHits.Count -eq 0) {
            Add-Error ("stateless legacy fixture '$($fixture.Name)' must be rejected " +
                'by the mutable-static semantic ratchet')
        }
    }
}

foreach ($modulePath in @(
    (Join-Path $qtSdl 'MelonPrimePatchShadowFreezeRuntimeHook.cpp'),
    (Join-Path $qtSdl 'MelonPrimePatchFixNoxusBladePersistence.cpp'))) {
    if (-not (Test-Path -LiteralPath $modulePath)) { continue }
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $modulePath) -replace '\\', '/'
    foreach ($violation in (Get-MutableStaticDeclarations ([System.IO.File]::ReadAllText($modulePath)) $relative)) {
        Add-Error ("ARM9 runtime module contains mutable static state; " +
            "use per-Core state or immutable data: $violation")
    }
}

$dispatchSignatures = @(
    @{ File = (Join-Path $qtSdl 'MelonPrimePatchShadowFreezeRuntimeHook.cpp'); Name = 'ShadowFreezeRuntimeHook_DispatchCheckAndRedirect' },
    @{ File = (Join-Path $qtSdl 'MelonPrimePatchFixNoxusBladePersistence.cpp'); Name = 'FixNoxusBladePersistence_DispatchCheck' }
)
foreach ($dispatch in $dispatchSignatures) {
    if (-not (Test-Path -LiteralPath $dispatch.File)) { continue }
    $dispatchCode = [regex]::Replace([System.IO.File]::ReadAllText($dispatch.File), '(?s)/\*.*?\*/', '')
    $dispatchCode = [regex]::Replace($dispatchCode, '//[^\r\n]*', '')
    $dispatchCode = [regex]::Replace($dispatchCode, '\s+', ' ')
    $signaturePattern = '\b' + $dispatch.Name + '\s*\([^;{}]*\buint8_t\s+romGroupIndex\b[^;{}]*\)'
    if ($dispatchCode -notmatch $signaturePattern) {
        Add-Error ("$($dispatch.Name)() must take uint8_t romGroupIndex explicitly")
    }
}
$arm9DispatcherCode = [System.IO.File]::ReadAllText((Join-Path $qtSdl 'MelonPrimeArm9Hook.cpp'))
if ($arm9DispatcherCode -notmatch 'const\s+uint8_t\s+romGroupIndex\s*=\s*hookState\.romGroupIndex') {
    Add-Error 'ARM9 dispatcher must load const uint8_t romGroupIndex = hookState.romGroupIndex'
}

$arm9StatePath = Join-Path $qtSdl 'MelonPrime.h'
if ((Get-CodeMatchLines 'uint8_t\s+romGroupIndex\s*=\s*0xFFu' $arm9StatePath).Count -eq 0) {
    Add-Error 'MelonPrimeArm9HookState must own an invalid-by-default romGroupIndex'
}
$arm9HookPath = Join-Path $qtSdl 'MelonPrimeArm9Hook.cpp'
if ((Get-CodeMatchLines 'const\s+uint8_t\s+romGroupIndex\s*=\s*hookState\.romGroupIndex' $arm9HookPath).Count -eq 0) {
    Add-Error 'DispatcherCallback must load romGroupIndex from the per-Core ARM9HookState'
}

# Neither may the render header pull the split headers back in and re-export
# them, which would restore the include coupling this split removed.
foreach ($line in (Get-MatchLines '#include\s+"MelonPrimeHud(Edit|PatchLifecycle|Radar|Runtime|PresentationState|GoldenHarness)\.h"' $hudRenderHeader)) {
    Add-Error ("MelonPrimeHudRender.h must not re-export the split HUD headers; " +
        "consumers include what they call: $line")
}

# --- Rule C: MelonPrimeCore runtime state stays private ---------------------
#
# The emulation thread owns these. A public field lets Screen.cpp, InputConfig
# or EmuThread mutate emulation state directly and the ThreadBridge stops being
# the single GUI/Emu boundary. This tracks the access specifier inside the class
# body, which is what "public" actually means here -- a name search alone would
# fire on the member's own comment.
$corePath = Join-Path $qtSdl "MelonPrime.h"
$ownedRuntimeFields = 'isCursorMode|isStylusMode|m_snapTapMode|isFastForward|screenSyncMode'
$coreLines = [System.IO.File]::ReadAllLines($corePath)
$inCore = $false
$access = 'private'
$depth = 0
$sawPrivateField = $false
for ($i = 0; $i -lt $coreLines.Length; $i++) {
    $line = $coreLines[$i]
    if (-not $inCore) {
        if ($line -match '^\s*class\s+MelonPrimeCore\b') { $inCore = $true; $access = 'private'; $depth = 0 }
        continue
    }
    $opens = ([regex]::Matches($line, '\{')).Count
    $closes = ([regex]::Matches($line, '\}')).Count
    $depthBefore = $depth
    $depth += $opens - $closes
    if ($depthBefore -gt 0 -and $depth -le 0) { $inCore = $false; continue }
    if ($line -match '^\s*(public|protected|private)\s*:') { $access = $Matches[1]; continue }
    # Data member declaration only: "<type> <name> = ...;" or "<type> <name>;".
    # A method mentioning the name has a '(' before the ';' and is skipped.
    if ($depth -le 1 -and $line -notmatch '\(' -and
        $line -match ("^\s*(?:mutable\s+)?(?:bool|int|uint\d+_t|int\d+_t|float|double)\s+($ownedRuntimeFields)\s*(=|;)")) {
        if ($access -ne 'private') {
            Add-Error ("MelonPrimeCore runtime state must stay private (the emulation " +
                "thread owns it; GUI goes through MelonPrimeThreadBridge): " +
                "src/frontend/qt_sdl/MelonPrime.h:$($i + 1):$($line.Trim())")
        } else {
            $sawPrivateField = $true
        }
    }
}
if (-not $sawPrivateField) {
    Add-Error ("no private MelonPrimeCore runtime-state field was found in " +
        "src/frontend/qt_sdl/MelonPrime.h -- the ownership ratchet is not " +
        "checking anything")
}

# --- Rule C2: fast-forward has one emulation-thread writer -----------------
#
# SetFastForwardState is intentionally a narrow public setter because EmuThread
# owns the decision.  Keep the call-site contract explicit: the declaration and
# definition do not match this receiver form, and exactly one source call must
# remain in EmuThread.cpp.
$sourceRoot = Join-Path $repoRoot "src"
$fastForwardCalls = @(Get-CodeMatchLines '->SetFastForwardState\s*\(|\.SetFastForwardState\s*\(' $sourceRoot)
if ($fastForwardCalls.Count -ne 1) {
    Add-Error ("SetFastForwardState must have exactly one source call in " +
        "the emulation thread; found $($fastForwardCalls.Count): " +
        ($fastForwardCalls -join '; '))
}
foreach ($line in $fastForwardCalls) {
    if ($line -notmatch 'src/frontend/qt_sdl/EmuThread\.cpp:') {
        Add-Error ("SetFastForwardState is an EmuThread-only writer; unexpected " +
            "call site: $line")
    }
}

# --- Rule D: hot-path cost review (manual, never a hard fail) ---------------
#
# The contract bans new abstraction cost in these bodies. A grep cannot tell a
# real per-frame Config lookup from a name in a comment, so this prints for a
# human instead of failing the build.
function Get-FunctionBody {
    param([string[]] $Lines, [int] $StartIndex)

    $depth = 0
    $started = $false
    $body = New-Object System.Collections.Generic.List[object]
    for ($i = $StartIndex; $i -lt $Lines.Length; $i++) {
        # Strip line comments and string literals so their braces do not count.
        $code = $Lines[$i] -replace '//.*$', '' -replace '"(\\.|[^"\\])*"', '""'
        $depth += ([regex]::Matches($code, '\{')).Count
        if ($depth -gt 0) { $started = $true }
        $depth -= ([regex]::Matches($code, '\}')).Count
        if ($started) { $body.Add([pscustomobject]@{ Line = $i + 1; Text = $Lines[$i] }) | Out-Null }
        if ($started -and $depth -le 0) { break }
    }
    return $body
}

function Get-FunctionText {
    param([string] $Path, [string] $Signature)

    $lines = [System.IO.File]::ReadAllLines($Path)
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match $Signature -and $lines[$i] -notmatch ';\s*$') {
            return ((Get-FunctionBody -Lines $lines -StartIndex $i) |
                ForEach-Object { $_.Text }) -join "`n"
        }
    }
    return $null
}

# --- Rule L: input SRP ownership and latency contract -----------------------
#
# GameInput is the logical owner of Aim-derived state and lifecycle reset
# profiles. Storage remains embedded in MelonPrimeCore for locality; this rule
# prevents the logical owner from drifting back into orchestration/config TUs.
$gameInputPath = Join-Path $qtSdl 'MelonPrimeGameInput.cpp'
$corePath = Join-Path $qtSdl 'MelonPrime.cpp'
$lifecyclePath = Join-Path $qtSdl 'MelonPrimeLifecycle.cpp'
$coreHeaderPath = Join-Path $qtSdl 'MelonPrime.h'
$gameInputText = Get-Content -LiteralPath $gameInputPath -Raw
$coreText = Get-Content -LiteralPath $corePath -Raw
$lifecycleText = Get-Content -LiteralPath $lifecyclePath -Raw
$coreHeaderText = Get-Content -LiteralPath $coreHeaderPath -Raw

foreach ($legacyReset in @(
    'ResetTransientInputState',
    'TR_AimResiduals',
    'TR_OverlayHeld',
    'TR_DirectTransform',
    'TR_BipedFire',
    'TR_WeaponSwitchPending',
    'TR_DirectInvocation'
)) {
    if (($gameInputText + $coreText + $lifecycleText + $coreHeaderText) -match
        [regex]::Escape($legacyReset)) {
        Add-Error "Rule L: legacy field-mask reset API reappeared: $legacyReset"
    }
}

$aimOwnerDefinitions = @(
    'ApplyAimConfigSnapshot',
    'ReloadAimConfigFromTable',
    'ApplyRuntimeAimSensitivity',
    'RecalcAimFixedPoint',
    'ApplyAimRuntimeConfig',
    'ResetAimTransientState',
    'ResetInputForLifecycleBoundary'
)
foreach ($name in $aimOwnerDefinitions) {
    $definition = "MelonPrimeCore::$name"
    if ($gameInputText -notmatch [regex]::Escape($definition)) {
        Add-Error "Rule L: GameInput owner definition is missing: $definition"
    }
    if ($coreText -match [regex]::Escape($definition) -or
        $lifecycleText -match [regex]::Escape($definition)) {
        Add-Error "Rule L: $definition must be defined by MelonPrimeGameInput.cpp"
    }
}

foreach ($boundary in @(
    'EmuStart', 'Boot', 'EmuStop', 'GameLeave', 'FocusLoss', 'GameJoin',
    'SavestateLoad'
)) {
    if ($coreHeaderText -notmatch "\b$boundary\b") {
        Add-Error "Rule L: InputLifecycleBoundary::$boundary is missing"
    }
    if ($gameInputText -notmatch "InputLifecycleBoundary::$boundary") {
        Add-Error "Rule L: reset profile is missing for InputLifecycleBoundary::$boundary"
    }
}

$requiredBoundaryCalls = @(
    @{ Text = $lifecycleText; Token = 'InputLifecycleBoundary::EmuStart' },
    @{ Text = $lifecycleText; Token = 'InputLifecycleBoundary::Boot' },
    @{ Text = $lifecycleText; Token = 'InputLifecycleBoundary::EmuStop' },
    @{ Text = $lifecycleText; Token = 'InputLifecycleBoundary::SavestateLoad' },
    @{ Text = $coreText; Token = 'InputLifecycleBoundary::GameLeave' },
    @{ Text = $coreText; Token = 'InputLifecycleBoundary::FocusLoss' },
    @{ Text = $coreText; Token = 'InputLifecycleBoundary::GameJoin' }
)
foreach ($call in $requiredBoundaryCalls) {
    if ($call.Text -notmatch [regex]::Escape($call.Token)) {
        Add-Error "Rule L: lifecycle caller is missing $($call.Token)"
    }
}

# Aim carry/delivery writers belong to the GameInput unity TU (its hook .inc
# children are part of that same owner). Header initializers are declarations,
# not runtime writers. No other standalone .cpp may assign them.
foreach ($writerPattern in @(
    'm_aimResidualX\s*=',
    'm_aimResidualY\s*=',
    'm_nativeAimDeltaX\s*=',
    'm_nativeAimDeltaY\s*='
)) {
    foreach ($line in (Get-CodeMatchLines $writerPattern $qtSdl)) {
        if ($line -match '\.cpp:' -and
            $line -notmatch 'MelonPrimeGameInput\.cpp:') {
            Add-Error "Rule L: Aim transient writer escaped GameInput ownership: $line"
        }
    }
}

# The latency-sensitive bodies hard-fail on abstractions that always add or
# hide work. Existing atomics in input acquisition are reviewed separately and
# remain covered by the single-writer contract.
$inputHotFunctions = @(
    @{ Path = $corePath; Signature = 'void\s+MelonPrimeCore::RunFrameHook\s*\(' },
    @{ Path = $gameInputPath; Signature = 'void\s+MelonPrimeCore::UpdateInputStateImpl\s*\(' },
    @{ Path = $gameInputPath; Signature = 'void\s+MelonPrimeCore::ProcessMoveAndButtonsFastImpl\s*\(' },
    @{ Path = $gameInputPath; Signature = 'void\s+MelonPrimeCore::ProcessAimInputMouse\s*\(' }
)
$inputAlwaysForbidden = 'std::vector|std::deque|std::map|std::unordered_map|std::function|\bvirtual\b|dynamic_cast|QMetaObject|Config::Table|\bGetBool\s*\(|\bGetInt\s*\(|\bGetDouble\s*\(|QString|std::string|\bnew\b'
foreach ($hot in $inputHotFunctions) {
    $lines = [System.IO.File]::ReadAllLines($hot.Path)
    $start = -1
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -match $hot.Signature -and $lines[$i] -notmatch ';\s*$') {
            $start = $i
            break
        }
    }
    if ($start -lt 0) {
        Add-Error "Rule L: input hot-path definition not found: $($hot.Signature)"
        continue
    }
    foreach ($entry in (Get-FunctionBody -Lines $lines -StartIndex $start)) {
        $code = $entry.Text -replace '//.*$', '' -replace '"(\\.|[^"\\])*"', '""'
        if ($code -match $inputAlwaysForbidden) {
            $relative = [System.IO.Path]::GetRelativePath($repoRoot, $hot.Path) -replace '\\', '/'
            Add-Error "Rule L: forbidden input hot-path abstraction: ${relative}:$($entry.Line):$($entry.Text.Trim())"
        }
    }
}

# Pin the visible sequencing contract. Match code tokens in order inside the
# production function so wrappers cannot silently shuffle input after guest
# simulation or hide lifecycle stages behind a generic ProcessEverything call.
$coreLinesForOrder = [System.IO.File]::ReadAllLines($corePath)
$runFrameStart = -1
for ($i = 0; $i -lt $coreLinesForOrder.Length; $i++) {
    if ($coreLinesForOrder[$i] -match 'void\s+MelonPrimeCore::RunFrameHook\s*\(') {
        $runFrameStart = $i
        break
    }
}
if ($runFrameStart -lt 0) {
    Add-Error 'Rule L: RunFrameHook definition was not found for order audit'
} else {
    $runFrameCode = ((Get-FunctionBody -Lines $coreLinesForOrder -StartIndex $runFrameStart) |
        ForEach-Object { $_.Text -replace '//.*$', '' }) -join "`n"
    $orderedTokens = @(
        'if (UNLIKELY(m_isRunningHook))',
        'm_configReloadPending.exchange',
        'm_isRunningHook = true;',
        'const bool focused =',
        'UpdateInputState(focused);',
        'InputReset();',
        'm_flags.clear(StateFlags::BIT_BLOCK_STYLUS);',
        'HandleGlobalHotkeys();',
        'DetectRomAndSetAddresses();',
        'const bool isInGame =',
        'HandleGameJoinInit();',
        'HandleBattleRuntimeEnter();',
        'CustomHud_ClampHelmetLayersPreFrame(',
        'DamageNotifyPurpleTick();',
        'if (focused) {',
        'if (isCursorMode) {',
        'm_flags.test(StateFlags::BIT_LAST_FOCUSED) != focused',
        'if (m_directTransformPendingFrames != 0) {',
        'm_isRunningHook = false;'
    )
    $cursor = 0
    foreach ($token in $orderedTokens) {
        $next = $runFrameCode.IndexOf($token, $cursor, [System.StringComparison]::Ordinal)
        if ($next -lt 0) {
            Add-Error "Rule L: RunFrameHook order token missing/out of order: $token"
            break
        }
        $cursor = $next + $token.Length
    }
}

# --- Rule L2: platform input event-path and rare-claim cost ratchets --------
#
# These checks only pin source shapes whose producer/consumer semantics were
# audited. They prevent a locked RMW, platform warp, guest read, or HK_MAX scan
# from returning to a steady input path; they do not substitute for runtime
# latency or hardware validation.
$linuxRawPath = Join-Path $qtSdl 'MelonPrimeRawInputLinuxFilter.cpp'
$threadBridgePath = Join-Path $qtSdl 'MelonPrimeThreadBridge.h'
$emuInstanceInputPath = Join-Path $qtSdl 'EmuInstanceInput.cpp'
$linuxRawText = Get-Content -LiteralPath $linuxRawPath -Raw
$threadBridgeText = Get-Content -LiteralPath $threadBridgePath -Raw
$emuInstanceInputText = Get-Content -LiteralPath $emuInstanceInputPath -Raw

$aimBody = Get-FunctionText -Path $gameInputPath `
    -Signature 'void\s+MelonPrimeCore::ProcessAimInputMouse\s*\('
if (-not $aimBody) {
    Add-Error 'Rule L2: ProcessAimInputMouse definition not found'
} else {
    if ($aimBody -match 'CaptureWantedForEmu\s*\(') {
        Add-Error 'Rule L2: raw macOS aim must not request recenter from capture-wanted alone'
    }
    if ($aimBody -notmatch 'm_warpCursorAfterAimThisFrame') {
        Add-Error 'Rule L2: ProcessAimInputMouse lost the once-per-frame cached warp policy'
    }
}

$absLoad = $linuxRawText.IndexOf('absBaseInvalid.load', [System.StringComparison]::Ordinal)
$absExchange = $linuxRawText.IndexOf('absBaseInvalid.exchange', [System.StringComparison]::Ordinal)
if ($absLoad -lt 0 -or $absExchange -lt 0 -or $absLoad -gt $absExchange) {
    Add-Error 'Rule L2: Linux absBaseInvalid must load before its rare exchange claim'
}
if ($linuxRawText -match '(?:acc[XY]|total)\.fetch_add\s*\(' -or
    $linuxRawText -notmatch 'std::atomic<uint64_t>\s+total' -or
    $linuxRawText -notmatch 'total\.load\s*\(std::memory_order_relaxed\)' -or
    $linuxRawText -notmatch 'total\.store\s*\(') {
    Add-Error 'Rule L2: Linux raw motion must use one packed single-writer load/store publication'
}
$linuxAccumulateBody = Get-FunctionText -Path $linuxRawPath `
    -Signature 'void\s+AccumulateRawMotion\s*\('
if (-not $linuxAccumulateBody -or
    $linuxAccumulateBody -notmatch 'receivedMotionPublished' -or
    $linuxAccumulateBody -match 'receivedMotion\.load\s*\(' -or
    $linuxAccumulateBody -notmatch 'receivedMotion\.store\s*\(') {
    Add-Error 'Rule L2: Linux first-motion publication must use its filter-thread shadow without a per-event atomic load'
}

$overlayBody = Get-FunctionText -Path $gameInputPath `
    -Signature 'void\s+MelonPrimeCore::ApplyPostPollOverlayInput\s*\('
if (-not $overlayBody) {
    Add-Error 'Rule L2: ApplyPostPollOverlayInput definition not found'
} else {
    $disabledGate = $overlayBody.IndexOf('!m_enableNativeBipedFire', [System.StringComparison]::Ordinal)
    $guestRead = $overlayBody.IndexOf('hookLocalPlayerPtrGlobal', [System.StringComparison]::Ordinal)
    if ($disabledGate -lt 0 -or $guestRead -lt 0 -or $disabledGate -gt $guestRead) {
        Add-Error 'Rule L2: post-poll overlay must reject both-disabled before its guest pointer read'
    }
    if ($overlayBody -match '\bm_overlayLocalPlayerPtr\b') {
        Add-Error 'Rule L2: legacy feature-ambiguous overlay player baseline reappeared'
    }
}
foreach ($featureReset in @(
    'ResetImmediateOverlayInputState',
    'ResetNativeBipedFireInputState'
)) {
    $resetBody = Get-FunctionText -Path $gameInputPath `
        -Signature "void\s+MelonPrimeCore::$featureReset\s*\("
    if (-not $resetBody) {
        Add-Error "Rule L2: overlay feature reset missing: $featureReset"
    } elseif ($resetBody -match 'm_postPollOverlayLocalPlayerPtr') {
        Add-Error "Rule L2: $featureReset must not own the shared player baseline"
    }
}
$coordinatorReset = Get-FunctionText -Path $gameInputPath `
    -Signature 'void\s+MelonPrimeCore::ResetPostPollOverlayCoordinatorState\s*\('
if (-not $coordinatorReset -or
    $coordinatorReset -notmatch 'm_postPollOverlayLocalPlayerPtr\s*=\s*0') {
    Add-Error 'Rule L2: post-poll coordinator must uniquely reset its shared player baseline'
}

$configLoad = $runFrameCode.IndexOf('m_configReloadPending.load', [System.StringComparison]::Ordinal)
$configExchange = $runFrameCode.IndexOf('m_configReloadPending.exchange', [System.StringComparison]::Ordinal)
if ($configLoad -lt 0 -or $configExchange -lt 0 -or $configLoad -gt $configExchange) {
    Add-Error 'Rule L2: config reload must load before its rare exchange claim'
}
foreach ($claim in @(
    @{ Signature = 'ConsumeWheelForEmu\s*\('; Load = 'm_wheelMailbox.load'; Exchange = 'm_wheelMailbox.exchange' },
    @{ Signature = 'ConsumeCursorModeForEmu\s*\('; Load = 'm_cursorModeCommand.load'; Exchange = 'm_cursorModeCommand.exchange' }
)) {
    $claimBody = Get-FunctionText -Path $threadBridgePath -Signature $claim.Signature
    $loadAt = if ($claimBody) { $claimBody.IndexOf($claim.Load, [System.StringComparison]::Ordinal) } else { -1 }
    $exchangeAt = if ($claimBody) { $claimBody.IndexOf($claim.Exchange, [System.StringComparison]::Ordinal) } else { -1 }
    if ($loadAt -lt 0 -or $exchangeAt -lt 0 -or $loadAt -gt $exchangeAt) {
        Add-Error "Rule L2: $($claim.Signature) must load before its rare exchange claim"
    }
}
if ($threadBridgeText -notmatch 'generation-only publication is\s*\r?\n?\s*// nonzero') {
    Add-Error 'Rule L2: wheel load-first invariant must document generation-only nonzero state'
}

$updateInputBody = Get-FunctionText -Path $gameInputPath `
    -Signature 'void\s+MelonPrimeCore::UpdateInputStateImpl\s*\('
if (-not $updateInputBody -or
    $updateInputBody -notmatch 'wheelHotkeyMaskForDelta\s*\(' -or
    $updateInputBody -match '\bhkKeyMapping\b') {
    Add-Error 'Rule L2: Windows raw wheel path must use the cold-precomputed hotkey mask'
}
$mouseWheelBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::onMouseWheel\s*\('
if (-not $mouseWheelBody -or
    $mouseWheelBody -notmatch 'wheelHotkeyMaskForDelta\s*\(' -or
    $mouseWheelBody -match 'for\s*\(') {
    Add-Error 'Rule L2: Qt wheel path must not rescan HK_MAX per pulse'
}

# --- Rule O-S: controller/macOS/bridge next-layer input contract ------------
$emuInstanceHeaderPath = Join-Path $qtSdl 'EmuInstance.h'
$emuThreadPath = Join-Path $qtSdl 'EmuThread.cpp'
$macRawPath = Join-Path $qtSdl 'MelonPrimeRawInputMacFilter.mm'
$emuInstanceHeaderText = Get-Content -LiteralPath $emuInstanceHeaderPath -Raw
$emuThreadText = Get-Content -LiteralPath $emuThreadPath -Raw
$macRawText = Get-Content -LiteralPath $macRawPath -Raw

# Rule O: lifecycle cadence is instance-local, never a function-static shared
# between independent EmuThreads.
$inputProcessBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::inputProcess\s*\('
$lateJoystickBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::inputRefreshJoystickState\s*\('
foreach ($bodySpec in @(
    @{ Name = 'inputProcess'; Body = $inputProcessBody },
    @{ Name = 'inputRefreshJoystickState'; Body = $lateJoystickBody }
)) {
    if (-not $bodySpec.Body) {
        Add-Error "Rule O: $($bodySpec.Name) definition not found"
    } elseif ($bodySpec.Body -match 'static\s+[^;]*(?:counter|check)') {
        Add-Error "Rule O: $($bodySpec.Name) contains shared function-static lifecycle cadence"
    }
}
if ($emuInstanceHeaderText -notmatch 'uint8_t\s+joystickLifecycleCheckCounter') {
    Add-Error 'Rule O: joystick lifecycle cadence must be stored per EmuInstance'
}

# Rule P: closeJoystick owns physical lifetime; EmuThread owns derived reset.
$closeJoystickBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::closeJoystick\s*\('
$resetJoystickBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::resetLateJoystickGameplayState\s*\('
if (-not $closeJoystickBody -or
    $closeJoystickBody -notmatch 'SDL_GameControllerClose\s*\(' -or
    $closeJoystickBody -notmatch 'SDL_JoystickClose\s*\(' -or
    $closeJoystickBody -notmatch 'joystickGameplayResetPending\.store\s*\(' -or
    $closeJoystickBody -match 'lateJoystick\.' -or
    $closeJoystickBody -notmatch 'hasRumble\s*=\s*false') {
    Add-Error 'Rule P: joystick physical lifetime/reset-request owner is incomplete'
}
if (-not $resetJoystickBody -or
    $resetJoystickBody -notmatch 'lateJoystick\.hotkeyHeld\s*=\s*0' -or
    $resetJoystickBody -match 'SDL_(?:GameController|Joystick)Close\s*\(') {
    Add-Error 'Rule P: EmuThread gameplay-derived joystick reset owner is incomplete'
}
if (-not $inputProcessBody -or
    $inputProcessBody -notmatch 'joystickGameplayResetPending\.load' -or
    $inputProcessBody -notmatch 'joystickGameplayResetPending\.exchange') {
    Add-Error 'Rule P: inputProcess must consume GUI/device reset publication with load-first claim'
}
foreach ($pollBody in @($inputProcessBody, $lateJoystickBody)) {
    if ($pollBody -match 'SDL_(?:GameController|Joystick)Close\s*\(') {
        Add-Error 'Rule P: joystick poll path bypasses the central close owner'
    }
}

# Rule Q: the late gameplay snapshot is distinct from global emulator edges.
if ($emuInstanceHeaderText -notmatch 'struct\s+LateJoystickSnapshot' -or
    $emuInstanceHeaderText -match 'keyHotkeyPress|lastKeyHotkeyMask' -or
    -not $lateJoystickBody -or
    $lateJoystickBody -notmatch 'lateJoystick\.hotkeyPressed\s*=') {
    Add-Error 'Rule Q: MelonPrime late joystick held/press snapshot is incomplete'
}
if ($lateJoystickBody -match 'lateJoystick\.hotkeyReleased') {
    Add-Error 'Rule Q: unconsumed late joystick release state reappeared'
}
if ($updateInputBody -notmatch 'qtGameplayPressed' -or
    $updateInputBody -notmatch 'm_qtGameplayHotkeyPrevious' -or
    $updateInputBody -notmatch 'lateJoystick\.hotkeyPressed' -or
    $updateInputBody -notmatch '~qtWheelMask') {
    Add-Error 'Rule Q: late gameplay projection must combine Qt and joystick edges while excluding Qt wheel levels'
}
if ($lateJoystickBody -match '(?m)^\s*hotkeyPress\s*=') {
    Add-Error 'Rule Q: late joystick poll must not rewrite global emulator hotkey edges'
}
$latePollAt = $emuThreadText.IndexOf(
    'inputRefreshJoystickState(', [System.StringComparison]::Ordinal)
$runFrameAt = $emuThreadText.IndexOf(
    'RunFrameHook(', $latePollAt + 1, [System.StringComparison]::Ordinal)
if ($latePollAt -lt 0 -or $runFrameAt -lt 0 -or $latePollAt -gt $runFrameAt) {
    Add-Error 'Rule Q: late joystick sample must remain before RunFrameHook'
}
if ($emuThreadText -notmatch 'inputRefreshJoystickState\s*\(\s*!melonPrime->IsNestedFrameAdvanceForInput\(\)\s*\)' -or
    $lateJoystickBody -notmatch 'if\s*\(!commitGameplayEdges\)' -or
    $lateJoystickBody -notmatch 'if\s*\(commitGameplayEdges\)\s*\r?\n\s*previousLateJoystickHotkeyMask') {
    Add-Error 'Rule Q: re-entrant FrameAdvance must refresh held state without committing the late edge baseline'
}

# Rule R: Apple documents GCDevice.handlerQueue as the callback execution
# authority. GCMouse uses one serial queue, IOHID one runloop, and each owns a
# separate packed cumulative total; neither event path needs fetch_add.
if ($macRawText -match '(?:acc[XY]|gcTotal|hidTotal)\.fetch_add\s*\(') {
    Add-Error 'Rule R: macOS raw event accumulator regressed to locked fetch_add'
}
foreach ($macNeedle in @(
    'DISPATCH_QUEUE_SERIAL',
    'mouse.handlerQueue = gcHandlerQueue',
    'std::atomic<CFRunLoopRef> runLoop',
    'std::atomic<uint64_t> gcTotal',
    'std::atomic<uint64_t> hidTotal',
    'AccumulateSingleWriter'
)) {
    if ($macRawText.IndexOf($macNeedle, [System.StringComparison]::Ordinal) -lt 0) {
        Add-Error "Rule R: macOS writer contract missing: $macNeedle"
    }
}
if ($macRawText -notmatch 'std::atomic<uint32_t>\s+backendBits' -or
    $macRawText -notmatch 'backendBits\.fetch_or\s*\(' -or
    $macRawText -notmatch 'backendBits\.fetch_and\s*\(' -or
    $macRawText -match 'std::atomic<bool>\s+(?:available|gcActive|hidOpen)' -or
    $macRawText -match 'RecomputeAvailable\s*\(') {
    Add-Error 'Rule R: macOS backend availability must be one fetch_or/fetch_and bitset'
}

# Rule S: rare mailboxes load before claim; panel aim is a GUI-owned packed
# cumulative total; center publication is one coherent pair.
$takeGuiBody = Get-FunctionText -Path $threadBridgePath `
    -Signature 'TakeGuiRequestsFromGui\s*\('
$persistBody = Get-FunctionText -Path $threadBridgePath `
    -Signature 'TakePersistRequestForGui\s*\('
foreach ($rare in @(
    @{ Name = 'GUI request'; Body = $takeGuiBody; Load = 'm_guiRequests.load'; Exchange = 'm_guiRequests.exchange' },
    @{ Name = 'persist request'; Body = $persistBody; Load = 'm_aimSensitivityPersist.load'; Exchange = 'm_aimSensitivityPersist.exchange' }
)) {
    $loadAt = if ($rare.Body) { $rare.Body.IndexOf($rare.Load, [System.StringComparison]::Ordinal) } else { -1 }
    $exchangeAt = if ($rare.Body) { $rare.Body.IndexOf($rare.Exchange, [System.StringComparison]::Ordinal) } else { -1 }
    if ($loadAt -lt 0 -or $exchangeAt -lt 0 -or $loadAt -gt $exchangeAt) {
        Add-Error "Rule S: $($rare.Name) must load before its rare exchange claim"
    }
}
if ($threadBridgeText -match 'm_panelAim[XY]\b' -or
    $threadBridgeText -match 'm_panelAimTotal\.fetch_add\s*\(' -or
    $threadBridgeText -notmatch 'std::atomic<uint64_t>\s+m_panelAimTotal' -or
    $threadBridgeText -notmatch 'std::atomic<uint64_t>\s+m_panelAimGuiResetBoundary' -or
    $threadBridgeText -notmatch 'std::atomic<uint32_t>\s+m_panelAimGuiResetGeneration' -or
    $threadBridgeText -notmatch 'uint64_t\s+m_panelAimCursor' -or
    $threadBridgeText -notmatch 'uint32_t\s+m_panelAimGuiResetSeen') {
    Add-Error 'Rule S: panel aim must retain GUI reset publication and Emu-owned cursor state'
}
$panelGuiResetBody = Get-FunctionText -Path $threadBridgePath `
    -Signature 'ResetPanelAimDeltaFromGui\s*\('
$panelEmuResetBody = Get-FunctionText -Path $threadBridgePath `
    -Signature 'ResetPanelAimDeltaFromEmu\s*\('
if (-not $panelGuiResetBody -or
    $panelGuiResetBody -notmatch 'm_panelAimGuiResetBoundary\.store' -or
    $panelGuiResetBody -notmatch 'm_panelAimGuiResetGeneration\.store' -or
    $panelGuiResetBody -match 'm_panelAimCursor\s*=') {
    Add-Error 'Rule S: GUI panel reset must publish only boundary plus generation'
}
if (-not $panelEmuResetBody -or
    $panelEmuResetBody -notmatch 'm_panelAimCursor\s*=' -or
    $panelEmuResetBody -match 'm_panelAimGuiReset(?:Boundary|Generation)\.store') {
    Add-Error 'Rule S: Emu panel reset must update only its consumer cursor/generation'
}
if ($threadBridgeText -match 'm_center[XY]\b' -or
    $threadBridgeText -notmatch 'std::atomic<uint64_t>\s+m_center') {
    Add-Error 'Rule S: center X/Y must remain one coherent packed publication'
}
if (-not $inputProcessBody -or
    $inputProcessBody.IndexOf('wheelHotkeyPulseMask.load', [System.StringComparison]::Ordinal) -lt 0 -or
    $inputProcessBody.IndexOf('wheelHotkeyPulseMask.load', [System.StringComparison]::Ordinal) -gt
        $inputProcessBody.IndexOf('wheelHotkeyPulseMask.exchange', [System.StringComparison]::Ordinal)) {
    Add-Error 'Rule S: wheel pulse must load before its rare exchange claim'
}
if ($linuxRawText -notmatch 'lastSourceState' -or
    $linuxRawText -notmatch 'std::min\(2,\s*raw->valuators\.mask_len\s*\*\s*8\)') {
    Add-Error 'Rule S: Linux common-source cache or X/Y-only packed decode is missing'
}
$controllerSampleAt = if ($lateJoystickBody) {
    $lateJoystickBody.IndexOf('SDL_JoystickGetButton', [System.StringComparison]::Ordinal)
} else { -1 }
$controllerUnlockAt = if ($lateJoystickBody -and $controllerSampleAt -ge 0) {
    $lateJoystickBody.IndexOf('SDL_UnlockMutex(joyMutex.get())', $controllerSampleAt, [System.StringComparison]::Ordinal)
} else { -1 }
$controllerAssemblyAt = if ($lateJoystickBody -and $controllerUnlockAt -ge 0) {
    $lateJoystickBody.IndexOf('uint16_t nextInputMask', $controllerUnlockAt, [System.StringComparison]::Ordinal)
} else { -1 }
if ($controllerSampleAt -lt 0 -or $controllerUnlockAt -lt 0 -or
    $controllerAssemblyAt -lt 0 -or
    $controllerSampleAt -gt $controllerUnlockAt -or
    $controllerUnlockAt -gt $controllerAssemblyAt) {
    Add-Error 'Rule S: controller lock must cover physical sampling but not numeric mask assembly'
}

# Rule T: cold frame work must stay before the late-input critical path.
$rtcAt = $emuThreadText.IndexOf('syncRTC();', [System.StringComparison]::Ordinal)
$shaderAt = $emuThreadText.IndexOf('NeedsShaderCompile()', [System.StringComparison]::Ordinal)
$inputSampleAt = $emuThreadText.IndexOf('MarkInputSample();', [System.StringComparison]::Ordinal)
if ($rtcAt -lt 0 -or $shaderAt -lt 0 -or $inputSampleAt -lt 0 -or
    $rtcAt -gt $inputSampleAt -or $shaderAt -gt $inputSampleAt) {
    Add-Error 'Rule T: RTC sync and shader readiness decision must precede the late input sample'
}

# Rule U: mouse-button release recovery is fixed-mask work, never HK_MAX scans.
$mouseSyncBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::syncMouseHotkeysFromQtButtons\s*\('
if (-not $mouseSyncBody -or
    $mouseSyncBody -notmatch 'mouseButtonMasks' -or
    $mouseSyncBody -match 'HK_MAX|hkKeyMapping' -or
    $mouseSyncBody -notmatch 'staleInput' -or
    $mouseSyncBody -notmatch 'staleHotkeys') {
    Add-Error 'Rule U: mouse-move release recovery must consume cold-precomputed fixed masks'
}

# Rule V: GUI level publications and input-generation publication are changed-only.
foreach ($setter in @('PublishCenterFromGui', 'PublishWindowHandleFromGui', 'PublishStylusPointerFromGui')) {
    $setterBody = Get-FunctionText -Path $threadBridgePath -Signature "$setter\s*\("
    if (-not $setterBody -or $setterBody -notmatch '\.load\s*\(' -or
        $setterBody -notmatch '\.store\s*\(') {
        Add-Error "Rule V: $setter must skip unchanged GUI publications"
    }
}
if ($threadBridgeText -notmatch 'SetGuiInputPolicyBit' -or
    $threadBridgeText -notmatch 'm_guiInputPolicyShadow') {
    Add-Error 'Rule V: focused/capture/panel policy must use one changed-only GUI publication'
}
if ($updateInputBody -notmatch 'm_publishedInputGeneration' -or
    $updateInputBody -notmatch 'SetInputGenerationFromEmu') {
    Add-Error 'Rule V: Core must cache input-generation publication'
}

# Rule W: GUI event presses survive a sub-frame tap and only normal frames claim them.
if ($emuInstanceHeaderText -notmatch 'std::atomic<uint64_t>\s+qtGameplayPressPending' -or
    $emuInstanceInputText -notmatch 'qtGameplayPressPending\.fetch_or' -or
    $emuInstanceInputText -notmatch 'isAutoRepeat\s*\(\)' -or
    $updateInputBody -notmatch 'qtGameplayPressPending\.exchange' -or
    $updateInputBody -notmatch 'if constexpr\s*\(!kReentrant\)') {
    Add-Error 'Rule W: Qt gameplay event-edge mailbox/autorepeat/reentrant contract is incomplete'
}

# Rule X: panel reset readers commit only generation-stable snapshots.
$panelReadBody = Get-FunctionText -Path $threadBridgePath `
    -Signature 'void\s+getAimMouseDelta\s*\('
if (-not $panelReadBody -or
    $panelReadBody -notmatch 'generationBefore' -or
    $panelReadBody -notmatch 'generationAfter' -or
    $panelReadBody -notmatch 'generationBefore\s*==\s*generationAfter') {
    Add-Error 'Rule X: panel aim consumer lost its stable-generation retry'
}

# Rule Y: GCMouse claims before handler install and drains before releasing IOHID.
$gcStartBody = Get-FunctionText -Path $macRawPath -Signature 'bool\s+StartGC\s*\('
$gcStopBody = Get-FunctionText -Path $macRawPath -Signature 'void\s+StopGC\s*\('
if (-not $gcStartBody -or
    $gcStartBody -notmatch 'dispatch_queue_set_specific' -or
    $gcStartBody -notmatch 'gcProducerEnabled' -or
    $gcStartBody -notmatch 'backendBits\.fetch_or[\s\S]*AttachGCMouse' -or
    $gcStartBody -notmatch 'mouseMovedHandler\s*=\s*nil[\s\S]*dispatch_async[\s\S]*backendBits\.fetch_and') {
    Add-Error 'Rule Y: macOS GC/HID connect/disconnect ownership transaction is incomplete'
}
if (-not $gcStopBody -or
    $gcStopBody -notmatch 'DispatchGcSync' -or
    $gcStopBody -notmatch 'backendBits\.fetch_and') {
    Add-Error 'Rule Y: macOS shutdown must drain the GC queue before releasing ownership'
}

# Rule Z: one primary ScreenPanel owns every shared input-surface publication.
$screenText = Get-Content -LiteralPath $screen -Raw
$windowText = Get-Content -LiteralPath (Join-Path $qtSdl 'Window.cpp') -Raw
if ($screenText -notmatch 'isMelonPrimeInputSurfaceAuthority' -or
    $screenText -notmatch 'emuInstance->getMainWindow\(\)\s*==\s*mainWindow' -or
    $windowText -notmatch 'inputSurfaceAuthority' -or
    $windowText -notmatch 'emuInstance->getMainWindow\(\)\s*==\s*this') {
    Add-Error 'Rule Z: per-EmuInstance primary input-surface authority guard is incomplete'
}

# Rule AA: remaining accepted P2 reductions stay data-oriented and allocation-free.
$mousePressBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::onMousePress\s*\('
$mouseReleaseBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::onMouseRelease\s*\('
$setJoystickBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::setJoystick\s*\('
$inputLoadBody = Get-FunctionText -Path $emuInstanceInputPath `
    -Signature 'void\s+EmuInstance::inputLoadConfig\s*\('
if ($emuInstanceHeaderText -notmatch 'joystickPhysicalSources' -or
    $emuInstanceHeaderText -notmatch 'joystickFanoutRules' -or
    $lateJoystickBody -notmatch 'joystickPhysicalSourceCount' -or
    $lateJoystickBody -notmatch 'joystickFanoutRuleCount') {
    Add-Error 'Rule AA: controller mappings must compile to unique physical sources plus fanout rules'
}
foreach ($mouseBody in @($mousePressBody, $mouseReleaseBody)) {
    if (-not $mouseBody -or $mouseBody -notmatch 'mouseButtonMasks' -or
        $mouseBody -match 'HK_MAX|hkKeyMapping') {
        Add-Error 'Rule AA: tracked mouse press/release must reuse cold-precomputed masks'
    }
}
if (-not $setJoystickBody -or $setJoystickBody -notmatch 'setJoystickLocked' -or
    -not $inputLoadBody -or $inputLoadBody -notmatch 'setJoystickLocked' -or
    $inputLoadBody -match '(?s)setJoystick\s*\(') {
    Add-Error 'Rule AA: joystick selection must use a locked helper without recursive mutex entry'
}

# Rule AB: owner/source resolution, packed Linux totals, UI result reuse and GUI revision gate.
$platformInputPath = Join-Path $qtSdl 'MelonPrimePlatformInput.h'
$platformInputText = Get-Content -LiteralPath $platformInputPath -Raw
$melonPrimeCppText = Get-Content -LiteralPath (Join-Path $qtSdl 'MelonPrime.cpp') -Raw
if ($platformInputText -notmatch 'bool\s+resolvedOwner' -or
    $platformInputText -match 'PlatformInputOwnerService::IsOwner\s*\(') {
    Add-Error 'Rule AB: Aim source resolution must reuse the owner result from this frame'
}
if ($linuxRawText -notmatch 'is_always_lock_free' -or
    $linuxRawText -notmatch 'std::atomic<uint64_t>\s+total' -or
    $linuxRawText -match 'std::atomic<int64_t>\s+acc[XY]') {
    Add-Error 'Rule AB: Linux raw X/Y must use one lock-free packed cumulative total'
}
if ($melonPrimeCppText -match 'PlatformInput_IsRuntimeRawAimActive' -or
    $gameInputText -notmatch 'm_rawAimActiveThisFrame\s*=\s*resolvedAim\.rawActive') {
    Add-Error 'Rule AB: UI raw-active state must reuse the frame Aim resolution result'
}
if ($threadBridgeText -notmatch 'std::atomic<uint32_t>\s+m_guiInputPolicy' -or
    $threadBridgeText -notmatch 'std::atomic<uint64_t>\s+m_guiWorkRevision' -or
    $screenText -notmatch 'GuiWorkRevisionForGui' -or
    $screenText -notmatch 'm_melonPrimeGuiRevisionSeen') {
    Add-Error 'Rule AB: GUI policy packing or revision-driven reconciliation is incomplete'
}

# --- Rule K: state-dependent Custom HUD APIs own their active-state scope ----
#
# The per-instance HUD state is reached through a thread_local pointer that only
# ScopedHudConfigState sets:
#
#     static thread_local CustomHudConfigState* g_activeHudConfigState;
#     static CustomHudConfigState& ActiveHudConfigState()
#     { Q_ASSERT(g_activeHudConfigState); return *g_activeHudConfigState; }
#
# Q_ASSERT compiles out in release, so a public API that reaches that state
# without a scope dereferences null on every call. That is not hypothetical: it
# shipped once, when a visibility query was changed from direct RAM reads to the
# frame cache while the radar presenters kept calling it outside the painter
# path.
#
# The contract this pins:
#   - an exported CustomHud_* that transitively reaches the state accessors must
#     take CustomHudConfigState& (so a caller cannot forget to supply one)
#   - one that reaches them in its own body must also construct a
#     ScopedHudConfigState (a pure delegator does not need its own)
$hudUnityFiles = @(Get-ChildItem -Path $qtSdl -File -Filter 'MelonPrimeHud*.inc') +
    @(Get-Item -LiteralPath (Join-Path $qtSdl 'MelonPrimeHudRender.cpp'))

# The state macros are declared next to the accessors they expand to, so read
# the set out of the source rather than restating it here.
$hudStateAccessors = New-Object System.Collections.Generic.HashSet[string]
foreach ($accessor in @('ActiveHudConfigState', 'HudFrameState', 'HudBattleState',
        'HudElementTextCaches')) {
    $hudStateAccessors.Add($accessor) | Out-Null
}
foreach ($file in $hudUnityFiles) {
    foreach ($line in [System.IO.File]::ReadAllLines($file.FullName)) {
        $m = [regex]::Match(
            $line,
            '^#define\s+(s_\w+)\s+\((ActiveHudConfigState|HudFrameState|HudBattleState|HudElementTextCaches)\(\)')
        if ($m.Success) { $hudStateAccessors.Add($m.Groups[1].Value) | Out-Null }
    }
}
if ($hudStateAccessors.Count -lt 10) {
    Add-Error ("Rule K could not recover the Custom HUD state macro set " +
        "(found $($hudStateAccessors.Count)); the #define shape must have changed")
}

# Every function defined in the HUD unity TU, with its body, so the reachability
# below can follow a public API into the static helpers it calls.
$hudFunctions = @{}
foreach ($file in $hudUnityFiles) {
    $lines = [System.IO.File]::ReadAllLines($file.FullName)
    $rel = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName) -replace '\\', '/'
    for ($i = 0; $i -lt $lines.Length; $i++) {
        $m = [regex]::Match(
            $lines[$i],
            '^(?<qual>(?:static\s+|inline\s+|HOT_FUNCTION\s+|COLD_FUNCTION\s+|FORCE_INLINE\s+)*)' +
            '(?:[A-Za-z_][\w:<>,\*&\s]*?\s[\*&]?\s*)?(?<name>[A-Za-z_]\w*)\s*\(')
        if (-not $m.Success) { continue }
        $name = $m.Groups['name'].Value
        if ($name -in @('if', 'for', 'while', 'switch', 'return', 'sizeof', 'catch')) { continue }
        $body = Get-FunctionBody -Lines $lines -StartIndex $i
        if ($body.Count -eq 0) { continue }
        # A signature can span lines; keep from the definition line to the brace.
        $signature = ($lines[$i..([math]::Min($body[0].Line - 1, $lines.Length - 1))] -join ' ')
        $text = (($body | ForEach-Object { $_.Text -replace '//.*$', '' }) -join "`n")
        if (-not $hudFunctions.ContainsKey($name)) {
            $hudFunctions[$name] = [pscustomobject]@{
                Name = $name
                File = $rel
                Line = $i + 1
                Signature = $signature
                Body = $text
                Exported = ($m.Groups['qual'].Value -notmatch '\bstatic\b')
            }
        }
    }
}

# Direct reach: the body names a state accessor or one of its macros.
$hudDirect = @{}
foreach ($name in $hudFunctions.Keys) {
    $body = $hudFunctions[$name].Body
    $hit = $false
    foreach ($accessor in $hudStateAccessors) {
        if ($body -match ("\b" + [regex]::Escape($accessor) + "\b")) { $hit = $true; break }
    }
    $hudDirect[$name] = $hit
}

# Transitive reach: follow calls to other functions in the same TU.
$hudReaches = @{}
foreach ($name in $hudFunctions.Keys) { $hudReaches[$name] = $hudDirect[$name] }
for ($pass = 0; $pass -lt 12; $pass++) {
    $changed = $false
    foreach ($name in @($hudFunctions.Keys)) {
        if ($hudReaches[$name]) { continue }
        foreach ($callee in [regex]::Matches($hudFunctions[$name].Body, '\b([A-Za-z_]\w*)\s*\(')) {
            $target = $callee.Groups[1].Value
            if ($target -eq $name) { continue }
            if ($hudReaches.ContainsKey($target) -and $hudReaches[$target]) {
                $hudReaches[$name] = $true
                $changed = $true
                break
            }
        }
    }
    if (-not $changed) { break }
}

$hudScopeChecked = 0
foreach ($name in $hudFunctions.Keys) {
    if ($name -notlike 'CustomHud_*') { continue }
    $fn = $hudFunctions[$name]
    if (-not $fn.Exported) { continue }
    if (-not $hudReaches[$name]) { continue }
    $hudScopeChecked++
    # Owning the state is as good as being handed it: the developer golden
    # harness constructs its own CustomHudConfigState rather than taking one.
    $ownsState = ($fn.Signature -match 'CustomHudConfigState\s*&') -or
        ($fn.Body -match 'CustomHudConfigState\s+\w+\s*[;{]')
    if (-not $ownsState) {
        Add-Error ("Rule K: $name reaches per-instance Custom HUD state but neither " +
            "takes nor owns a CustomHudConfigState -- ActiveHudConfigState() " +
            "dereferences a null thread_local when it is called outside a painter " +
            "scope ($($fn.File):$($fn.Line))")
    } elseif ($hudDirect[$name] -and $fn.Body -notmatch 'ScopedHudConfigState') {
        Add-Error ("Rule K: $name touches per-instance Custom HUD state without " +
            "constructing a ScopedHudConfigState ($($fn.File):$($fn.Line))")
    }
}
if ($hudScopeChecked -eq 0) {
    Add-Error ("Rule K matched no state-dependent Custom HUD API; the definition " +
        "scan must have stopped working")
}


# --- Rule H: CustomHud_Render consumes the screen snapshot ----------------
#
# Screen.cpp refreshes m_hudEnabled once per HUD config epoch and uses that
# same value for visibility/restore and the render call. A live Config::Table
# lookup here would make those decisions disagree on a toggle edge.
$hudRenderMainPath = Join-Path $qtSdl 'MelonPrimeHudRenderMain.inc'
$hudRenderMainLines = [System.IO.File]::ReadAllLines($hudRenderMainPath)
$hudRenderStartIndex = -1
for ($i = 0; $i -lt $hudRenderMainLines.Length; $i++) {
    if ($hudRenderMainLines[$i] -match '^\s*(?:HOT_FUNCTION\s+)?QRect\s+CustomHud_Render\s*\(') {
        $hudRenderStartIndex = $i
        break
    }
}
if ($hudRenderStartIndex -lt 0) {
    Add-Error 'CustomHud_Render() definition was not found for the HUD enabled-snapshot check'
} else {
    $hudRenderBody = @(Get-FunctionBody -Lines $hudRenderMainLines -StartIndex $hudRenderStartIndex)
    foreach ($entry in $hudRenderBody) {
        $code = $entry.Text -replace '//.*$', ''
        if ($code -match 'CustomHud_IsEnabled\s*\(') {
            Add-Error ("CustomHud_Render must consume hudEnabledSnapshot, not " +
                "CustomHud_IsEnabled(): $($hudRenderMainPath):$($entry.Line):$($entry.Text.Trim())")
        }
        if ($code -match '\bGet(?:Bool|Int|Double|String)\s*\(') {
            Add-Error ("CustomHud_Render must not perform a live Config::Table lookup: " +
                "$($hudRenderMainPath):$($entry.Line):$($entry.Text.Trim())")
        }
    }
}

# --- Rule I: ARM9 install consumes only the resolved activation plan --------
#
# RuntimeConfigSnapshot owns config interpretation. ARM9Hook_Install is the
# cold installer and may collect addresses and set module state, but it must
# not grow a second Config::Table/key interpreter.
$arm9HookPath = Join-Path $qtSdl 'MelonPrimeArm9Hook.cpp'
$arm9HookLines = [System.IO.File]::ReadAllLines($arm9HookPath)
$arm9InstallStartIndex = -1
for ($i = 0; $i -lt $arm9HookLines.Length; $i++) {
    if ($arm9HookLines[$i] -match '^\s*void\s+ARM9Hook_Install\s*\(') {
        $arm9InstallStartIndex = $i
        break
    }
}
if ($arm9InstallStartIndex -lt 0) {
    Add-Error 'ARM9Hook_Install() definition was not found for the activation-plan check'
} else {
    $arm9SignatureEndIndex = [Math]::Min(
        $arm9HookLines.Length - 1, $arm9InstallStartIndex + 12)
    $arm9Signature = $arm9HookLines[$arm9InstallStartIndex..$arm9SignatureEndIndex] -join "`n"
    if ($arm9Signature -notmatch 'const\s+Arm9HookActivationPlan\s*&\s*plan') {
        Add-Error 'ARM9Hook_Install() must consume const Arm9HookActivationPlan& plan'
    }

    $arm9InstallBody = @(Get-FunctionBody -Lines $arm9HookLines -StartIndex $arm9InstallStartIndex)
    foreach ($entry in $arm9InstallBody) {
        $code = $entry.Text -replace '//.*$', ''
        if ($code -match 'Config::Table|CfgKey::|\bGet(?:Bool|Int|Double|String)\s*\(') {
            Add-Error ('ARM9Hook_Install must not reinterpret runtime config; ' +
                "${arm9HookPath}:$($entry.Line):$($entry.Text.Trim())")
        }
    }
}

# --- Rule A2: aim smoothing remains wired at game join ---------------------
#
# Rule A proves the patch vocabulary left GameSettings and that the patch
# declaration exists.  This companion rule keeps the behavior-critical call
# in the cold HandleGameJoinInit body and keeps the implementation in the
# target executable's source list.
$melonPrimeCore = Join-Path $qtSdl "MelonPrime.cpp"
$melonPrimeCoreLines = [System.IO.File]::ReadAllLines($melonPrimeCore)
$joinStartIndex = -1
for ($i = 0; $i -lt $melonPrimeCoreLines.Length; $i++) {
    if ($melonPrimeCoreLines[$i] -match '^\s*(?:COLD_FUNCTION\s+)?void\s+MelonPrimeCore::HandleGameJoinInit\s*\(') {
        $joinStartIndex = $i
        break
    }
}
if ($joinStartIndex -lt 0) {
    Add-Error "MelonPrimeCore::HandleGameJoinInit() definition was not found for the aim-smoothing wiring check"
} else {
    $joinBody = @(Get-FunctionBody -Lines $melonPrimeCoreLines -StartIndex $joinStartIndex)
    $aimSmoothingCalls = @($joinBody | Where-Object {
        $code = $_.Text -replace '//.*$', ''
        $code -match '\bAimSmoothing_ApplyOrRestore\s*\('
    })
    if ($aimSmoothingCalls.Count -ne 1) {
        Add-Error ("HandleGameJoinInit() must call AimSmoothing_ApplyOrRestore() " +
            "exactly once; found $($aimSmoothingCalls.Count): " +
            (($aimSmoothingCalls | ForEach-Object { "line $($_.Line): $($_.Text.Trim())" }) -join '; '))
    }
}

$qtSdlCmake = Join-Path $qtSdl "CMakeLists.txt"
if ((Get-CodeMatchLines '^\s*MelonPrimePatchAimSmoothing\.cpp\s*$' $qtSdlCmake).Count -eq 0) {
    Add-Error "MelonPrimePatchAimSmoothing.cpp is missing from src/frontend/qt_sdl/CMakeLists.txt"
}

# --- Rule I2: EmuThread frame loop consumes renderer pointer caches ----------
#
# Renderer RTTI belongs at the renderer transition boundary. The emulation
# frame loop is deliberately marked in EmuThread.cpp so this ratchet can reject
# a future per-frame dynamic_cast while still allowing the one-time refresh
# helper to inspect the actual live renderer after updateRenderer().
$emuThreadPath = Join-Path $qtSdl 'EmuThread.cpp'
$emuThreadSource = [System.IO.File]::ReadAllText($emuThreadPath)
$frameHotBegin = '// MELONPRIME_EMUTHREAD_FRAME_HOT_PATH_BEGIN'
$frameHotEnd = '// MELONPRIME_EMUTHREAD_FRAME_HOT_PATH_END'
$frameHotBeginIndex = $emuThreadSource.IndexOf($frameHotBegin)
$frameHotEndIndex = $emuThreadSource.IndexOf($frameHotEnd)
$rendererRttiPattern = 'dynamic_cast\s*<\s*[^>]*Renderer[^>]*>'
if ($frameHotBeginIndex -lt 0 -or $frameHotEndIndex -le $frameHotBeginIndex) {
    Add-Error 'EmuThread frame hot-path markers are missing or out of order'
} else {
    $frameHotLength = $frameHotEndIndex + $frameHotEnd.Length - $frameHotBeginIndex
    $frameHotSource = $emuThreadSource.Substring($frameHotBeginIndex, $frameHotLength)
    $frameHotCode = [regex]::Replace($frameHotSource, '//[^\r\n]*', '')
    if ($frameHotCode -match $rendererRttiPattern) {
        Add-Error 'EmuThread frame hot path must use cached renderer pointers; renderer RTTI belongs at the transition boundary'
    }
    foreach ($cacheName in @('cachedVulkanLowLatencyRenderer', 'cachedStructuredSoft2DRenderer')) {
        if ($frameHotSource -notmatch [regex]::Escape($cacheName)) {
            Add-Error "EmuThread frame hot path no longer consumes $cacheName"
        }
    }
}

$refreshSignature = 'void\s+EmuThread::RefreshRendererFrameCache\s*\('
if ((Get-CodeMatchLines $refreshSignature $emuThreadPath).Count -eq 0) {
    Add-Error 'RefreshRendererFrameCache() is missing from EmuThread.cpp'
}
$dx12CacheDeclaration = 'DX12Renderer\s*\*\s*cachedDX12FrameRenderer'
if ((Get-CodeMatchLines $dx12CacheDeclaration (Join-Path $qtSdl 'EmuThread.h')).Count -eq 0) {
    Add-Error 'EmuThread DX12 renderer frame cache declaration is missing'
}
$cleanRendererRttiFixture = 'bool helper() { return cachedDX12FrameRenderer != nullptr; }'
$forbiddenRendererRttiFixture = 'bool helper() { auto* r = dynamic_cast<DX12Renderer*>(&nds->GPU.GetRenderer()); return r != nullptr; }'
if ($cleanRendererRttiFixture -match $rendererRttiPattern) {
    Add-Error 'Renderer RTTI audit fixture incorrectly rejects a cache-only helper'
}
if ($forbiddenRendererRttiFixture -notmatch $rendererRttiPattern) {
    Add-Error 'Renderer RTTI audit fixture failed to recognize a forbidden renderer cast'
}
$refreshCallToken = 'RefreshRendererFrameCache();'
$refreshCallIndex = $emuThreadSource.IndexOf($refreshCallToken)
$rendererBeforeIndex = $emuThreadSource.IndexOf('#include "MelonPrimeEmuThreadUpdateRendererBefore.inc"')
$cacheClearIndex = $emuThreadSource.IndexOf('cachedStructuredSoft2DRenderer = nullptr;')
if (($cacheClearIndex -lt 0) -or ($rendererBeforeIndex -lt 0) -or ($cacheClearIndex -gt $rendererBeforeIndex)) {
    Add-Error 'EmuThread renderer pointer caches must clear before the transition helper'
}
$dx12CacheClearIndex = $emuThreadSource.IndexOf('cachedDX12FrameRenderer = nullptr;')
if (($dx12CacheClearIndex -lt 0) -or ($rendererBeforeIndex -lt 0) -or ($dx12CacheClearIndex -gt $rendererBeforeIndex)) {
    Add-Error 'EmuThread DX12 renderer cache must clear before the transition helper'
}
if ($refreshCallIndex -lt 0 -or $refreshCallIndex -lt $rendererBeforeIndex) {
    Add-Error 'RefreshRendererFrameCache() must run after updateRenderer() settles the actual renderer'
}

$dx12FailureSignature = '^\s*bool\s+EmuThread::handleDX12RuntimeFailure\s*\('
$emuThreadLines = [System.IO.File]::ReadAllLines($emuThreadPath)
$dx12FailureStartIndex = -1
for ($i = 0; $i -lt $emuThreadLines.Length; $i++) {
    if ($emuThreadLines[$i] -match $dx12FailureSignature) {
        $dx12FailureStartIndex = $i
        break
    }
}
if ($dx12FailureStartIndex -lt 0) {
    Add-Error 'handleDX12RuntimeFailure() definition was not found for the renderer RTTI check'
} else {
    $dx12FailureBody = @(Get-FunctionBody -Lines $emuThreadLines -StartIndex $dx12FailureStartIndex)
    $dx12FailureBodyText = ($dx12FailureBody | ForEach-Object { $_.Text }) -join "`n"
    $dx12FailureBodyCode = [regex]::Replace($dx12FailureBodyText, '//[^\r\n]*', '')
    if ($dx12FailureBodyCode -match $rendererRttiPattern) {
        Add-Error 'handleDX12RuntimeFailure() must use cachedDX12FrameRenderer; renderer RTTI is forbidden in the frame helper'
    }
    if ($dx12FailureBodyCode -notmatch '\bcachedDX12FrameRenderer\b') {
        Add-Error 'handleDX12RuntimeFailure() no longer consumes cachedDX12FrameRenderer'
    }
}

$hotPaths = @(
    @{ File = 'MelonPrime.cpp';               Signature = 'void\s+MelonPrimeCore::RunFrameHook\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::UpdateInputStateImpl\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::ProcessMoveAndButtonsFastImpl\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::ProcessAimInputMouse\s*\(' },
    @{ File = 'MelonPrimeArm9Hook.cpp';       Signature = '^\s*static\s+bool\s+DispatcherCallback\s*\(' },
    @{ File = 'MelonPrimeHudRenderMain.inc';  Signature = 'QRect\s+CustomHud_Render\s*\(' },
    # Native presenters are included as manual-review paths. Their output
    # boundary is performance-critical, but platform-specific diagnostics and
    # the one-time layout/renderer transitions are not suitable for a hard
    # regex gate here.
    @{ File = 'MelonPrimeScreenVulkan.cpp';   Signature = 'void\s+ScreenPanelVulkan::drawScreenFrame\s*\(' },
    @{ File = 'Screen.cpp';                   Signature = 'void\s+ScreenPanelDX12::drawScreen\s*\(' },
    @{ File = 'MelonPrimeScreenMetal.mm';     Signature = 'void\s+ScreenPanelMetal::drawScreen\s*\(' }
)
$hotPathCosts = 'Config::Table|\bGetBool\s*\(|\bGetInt\s*\(|\bGetDouble\s*\(|std::function|\bvirtual\b|dynamic_cast|QMetaObject|std::make_shared|std::make_unique|QString\s*\(|\bmutex\b|std::shared_ptr|std::static_pointer_cast|QMutexLocker|std::lock_guard|std::unique_lock|\bnew\b|std::string'
$hotPathNotes = New-Object System.Collections.Generic.List[string]
foreach ($hot in $hotPaths) {
    $hotFile = Join-Path $qtSdl $hot.File
    if (-not (Test-Path -LiteralPath $hotFile)) {
        $hotPathNotes.Add("hot-path file not found (rule needs updating): $($hot.File)") | Out-Null
        continue
    }
    $hotLines = [System.IO.File]::ReadAllLines($hotFile)
    $startIndex = -1
    for ($i = 0; $i -lt $hotLines.Length; $i++) {
        if ($hotLines[$i] -match $hot.Signature -and $hotLines[$i] -notmatch ';\s*$') { $startIndex = $i; break }
    }
    if ($startIndex -lt 0) {
        $hotPathNotes.Add("hot-path definition not found (rule needs updating): $($hot.File) /$($hot.Signature)/") | Out-Null
        continue
    }
    foreach ($entry in (Get-FunctionBody -Lines $hotLines -StartIndex $startIndex)) {
        $code = $entry.Text -replace '//.*$', ''
        if ($code -match $hotPathCosts) {
            $hotPathNotes.Add("$($hot.File):$($entry.Line):$($entry.Text.Trim())") | Out-Null
        }
    }
}
if ($hotPathNotes.Count -ne 0) {
    Write-Host ""
    Write-Host "Hot-path cost review (manual, not a failure) -- confirm each is not a new"
    Write-Host "per-frame allocation, config lookup, indirect dispatch or lock:"
    foreach ($note in $hotPathNotes) { Write-Host "  $note" }
    Write-Host ""
}

if ($errors.Count -ne 0) {
    foreach ($e in $errors) {
        Write-Error $e
    }
    exit 1
}

Write-Host "MelonPrime SRP/performance audit passed."
