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
        $text -notmatch '^\s*(//|\*|/\*)' -and $text.Trim().Length -gt 0
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

$hotPaths = @(
    @{ File = 'MelonPrime.cpp';               Signature = 'void\s+MelonPrimeCore::RunFrameHook\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::UpdateInputStateImpl\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::ProcessMoveAndButtonsFastImpl\s*\(' },
    @{ File = 'MelonPrimeGameInput.cpp';      Signature = 'void\s+MelonPrimeCore::ProcessAimInputMouse\s*\(' },
    @{ File = 'MelonPrimeArm9Hook.cpp';       Signature = '^\s*static\s+bool\s+DispatcherCallback\s*\(' },
    @{ File = 'MelonPrimeHudRenderMain.inc';  Signature = 'QRect\s+CustomHud_Render\s*\(' }
)
$hotPathCosts = 'Config::Table|\bGetBool\s*\(|\bGetInt\s*\(|\bGetDouble\s*\(|std::function|\bvirtual\b|dynamic_cast|QMetaObject|std::make_shared|std::make_unique|QString\s*\(|\bmutex\b'
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
