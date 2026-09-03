# audit-screen-panel-srp.ps1
#
# Source-shape ratchet for the Screen.cpp SRP boundaries established by
# .codex/MelonPrimeDS_ScreenCpp_Audit_c3793ace_*.md (SCR-SRP-001/002) and the
# hot-path rules in its §23.
#
# Three groups, all cheap textual checks. Arithmetic and state transitions are
# covered by unit tests, and runtime cost by tools/perf/ -- this file only
# guards where code is allowed to live.
#
#   1. Platform isolation: the DX12 panel body and its headers stay out of the
#      generic screen translation unit, and its TU is registered only in the
#      DX12-active CMake block.
#   2. HUD fragments: the unity-include count may shrink but never grow, and no
#      new MelonPrimeHudScreenCpp*.inc may appear.
#   3. High-rate Qt events: no per-event Config lookup, heap allocation, or
#      queued GUI invocation in the mouse/tablet move handlers.

param(
    [int]$HudFragmentBudget = 2,
    [switch]$Json
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$qtSdl = Join-Path $repoRoot 'src/frontend/qt_sdl'
$screenCpp = Join-Path $qtSdl 'Screen.cpp'
$dx12Cpp = Join-Path $qtSdl 'MelonPrimeScreenDX12.cpp'
$vulkanCpp = Join-Path $qtSdl 'MelonPrimeScreenVulkan.cpp'
$hudIntegrationCpp = Join-Path $qtSdl 'MelonPrimeHudScreenIntegration.cpp'
$cmake = Join-Path $qtSdl 'CMakeLists.txt'

$errors = New-Object System.Collections.Generic.List[string]

function Get-CodeLines {
    param([string] $Path)

    # Strip whole-line comments so a note that names a symbol cannot trip a
    # ban, and so a rule cannot be satisfied by prose either.
    $lines = New-Object System.Collections.Generic.List[object]
    $number = 0
    foreach ($raw in [System.IO.File]::ReadLines($Path)) {
        $number++
        $code = ($raw -replace '//.*$', '')
        if ([string]::IsNullOrWhiteSpace($code)) { continue }
        $lines.Add([pscustomobject]@{ Line = $number; Text = $code })
    }
    return $lines
}

# -- 1. DX12 platform isolation ------------------------------------------------

if (-not (Test-Path -LiteralPath $dx12Cpp)) {
    $errors.Add('MelonPrimeScreenDX12.cpp is missing: the DX12 panel body must own its translation unit.') | Out-Null
}

$screenLines = Get-CodeLines $screenCpp

foreach ($hit in ($screenLines | Where-Object { $_.Text -match 'ScreenPanelDX12::' })) {
    $errors.Add("Screen.cpp:$($hit.Line): ScreenPanelDX12 method body belongs in MelonPrimeScreenDX12.cpp: $($hit.Text.Trim())") | Out-Null
}
foreach ($hit in ($screenLines | Where-Object { $_.Text -match '\bDX12SurfaceHost\b|\bg_dx12PanelRegistry' })) {
    $errors.Add("Screen.cpp:$($hit.Line): DX12 presenter internals belong in MelonPrimeScreenDX12.cpp: $($hit.Text.Trim())") | Out-Null
}

$dx12OnlyHeaders = @('DX12Perf.h', 'GPU_DX12.h', 'MelonPrimeDX12FeatureCheck.h',
                     'MelonPrimeDX12SurfacePresenter.h')
foreach ($header in $dx12OnlyHeaders) {
    $pattern = '#\s*include\s+"' + [regex]::Escape($header) + '"'
    foreach ($hit in ($screenLines | Where-Object { $_.Text -match $pattern })) {
        $errors.Add("Screen.cpp:$($hit.Line): DX12-only header must not reach the generic screen TU: $header") | Out-Null
    }
}

# The TU must be registered only where DX12 is actually active, so a
# non-Windows or DX12-off build never compiles it.
$cmakeText = [System.IO.File]::ReadAllText($cmake)
if ($cmakeText -notmatch 'MelonPrimeScreenDX12\.cpp') {
    $errors.Add('CMakeLists.txt does not register MelonPrimeScreenDX12.cpp.') | Out-Null
}
else {
    $genericBlock = [regex]::Match($cmakeText, '(?s)set\(SOURCES_QT_SDL(.*?)\n\)')
    if ($genericBlock.Success -and $genericBlock.Groups[1].Value -match 'MelonPrimeScreenDX12\.cpp') {
        $errors.Add('MelonPrimeScreenDX12.cpp is in the generic SOURCES_QT_SDL list; it must stay inside the MELONPRIME_DX12_ACTIVE block.') | Out-Null
    }
    # There is more than one DX12-active block and they nest further if()s, so
    # track if/endif depth rather than trusting a non-greedy regex.
    $cmakeLines = $cmakeText -split "`n"
    $blockFound = $false
    $registeredInBlock = $false
    for ($i = 0; $i -lt $cmakeLines.Count; $i++) {
        if ($cmakeLines[$i] -notmatch '^\s*if\s*\(\s*MELONPRIME_DX12_ACTIVE\s*\)') { continue }
        $blockFound = $true
        $depth = 0
        for ($j = $i; $j -lt $cmakeLines.Count; $j++) {
            $line = ($cmakeLines[$j] -replace '#.*$', '')
            if ($line -match '^\s*if\s*\(') { $depth++ }
            elseif ($line -match '^\s*endif\s*\(') { $depth-- }
            if ($line -match 'MelonPrimeScreenDX12\.cpp') { $registeredInBlock = $true }
            if ($depth -le 0 -and $j -gt $i) { break }
        }
    }
    if (-not $blockFound) {
        $errors.Add('Could not find an if (MELONPRIME_DX12_ACTIVE) block in CMakeLists.txt.') | Out-Null
    }
    elseif (-not $registeredInBlock) {
        $errors.Add('MelonPrimeScreenDX12.cpp is registered outside every MELONPRIME_DX12_ACTIVE block.') | Out-Null
    }
}

# -- 2. HUD unity fragments ----------------------------------------------------

$hudFragments = @(Get-ChildItem -LiteralPath $qtSdl -File -Filter 'MelonPrimeHudScreenCpp*.inc' |
    Sort-Object Name)
if ($hudFragments.Count -gt $HudFragmentBudget) {
    $errors.Add("MelonPrimeHudScreenCpp*.inc count is $($hudFragments.Count), above the budget of $HudFragmentBudget. Extract to a real module instead of adding a fragment.") | Out-Null
}
$allowedHudFragments = @(
    'MelonPrimeHudScreenCppOverlayOfGl.inc',
    'MelonPrimeHudScreenCppOverlayOfSoftware.inc'
)
foreach ($fragment in $hudFragments) {
    if ($fragment.Name -notin $allowedHudFragments) {
        $errors.Add("New HUD screen unity fragment is forbidden: $($fragment.Name). Add a real module/function instead.") | Out-Null
    }
}

# -- 3. High-rate Qt event handlers -------------------------------------------

# Body extents of the handlers that can fire per input report.
$hotHandlers = @('void ScreenPanel::mouseMoveEvent', 'void ScreenPanel::tabletEvent')
$screenText = [System.IO.File]::ReadAllText($screenCpp)
$screenAllLines = $screenText -split "`n"

foreach ($signature in $hotHandlers) {
    $startIndex = -1
    for ($i = 0; $i -lt $screenAllLines.Count; $i++) {
        if ($screenAllLines[$i].StartsWith($signature)) { $startIndex = $i; break }
    }
    if ($startIndex -lt 0) {
        $errors.Add("Could not locate '$signature' in Screen.cpp; the hot-path rules cannot be enforced.") | Out-Null
        continue
    }

    # Walk braces from the opening line to the matching close.
    $depth = 0
    $started = $false
    $endIndex = $startIndex
    for ($i = $startIndex; $i -lt $screenAllLines.Count; $i++) {
        $code = ($screenAllLines[$i] -replace '//.*$', '')
        $depth += ([regex]::Matches($code, '\{')).Count
        $depth -= ([regex]::Matches($code, '\}')).Count
        if (-not $started -and $depth -gt 0) { $started = $true }
        if ($started -and $depth -le 0) { $endIndex = $i; break }
    }

    for ($i = $startIndex; $i -le $endIndex; $i++) {
        $code = ($screenAllLines[$i] -replace '//.*$', '')
        $lineNo = $i + 1
        if ($code -match '\.Get(Bool|Int|Double|String)\s*\(' -or $code -match 'getLocalConfig\s*\(') {
            $errors.Add("Screen.cpp:${lineNo}: per-event Config lookup in ${signature}. Cache it on the cold config path: $($code.Trim())") | Out-Null
        }
        if ($code -match '\bnew\s+[A-Z]' -or $code -match 'std::make_unique|std::make_shared') {
            $errors.Add("Screen.cpp:${lineNo}: per-event heap allocation in ${signature}: $($code.Trim())") | Out-Null
        }
        if ($code -match 'QMetaObject::invokeMethod') {
            $errors.Add("Screen.cpp:${lineNo}: per-event queued GUI invocation in ${signature}: $($code.Trim())") | Out-Null
        }
    }
}

# HUD editor moves are the one intentional exception to the ordinary
# mouseMoveEvent fast path. The gate must be the GUI-owned cached latch, and
# the helper must retain its own live CustomHud_IsEditMode() guard so a stale
# lifecycle edge cannot enter the editor operation.
$mouseBodyStart = $screenText.IndexOf('void ScreenPanel::mouseMoveEvent')
if ($mouseBodyStart -lt 0) {
    $errors.Add('Could not locate mouseMoveEvent for the HUD cached-gate check.') | Out-Null
}
else {
    $mouseBody = $screenText.Substring($mouseBodyStart)
    $mouseBodyEnd = $mouseBody.IndexOf("`n}", [System.StringComparison]::Ordinal)
    if ($mouseBodyEnd -gt 0) { $mouseBody = $mouseBody.Substring(0, $mouseBodyEnd + 2) }
    $gatePos = $mouseBody.IndexOf('Q_UNLIKELY(m_hudEditInputActive)', [System.StringComparison]::Ordinal)
    $helperPos = $mouseBody.IndexOf('handleHudMouseMove(event)', [System.StringComparison]::Ordinal)
    if ($gatePos -lt 0 -or $helperPos -lt 0 -or $gatePos -gt $helperPos) {
        $errors.Add('Screen.cpp mouseMoveEvent must gate handleHudMouseMove(event) with m_hudEditInputActive.') | Out-Null
    }
}

if (-not (Test-Path -LiteralPath $hudIntegrationCpp)) {
    $errors.Add('MelonPrimeHudScreenIntegration.cpp is missing: the HUD helper audit cannot run.') | Out-Null
}
else {
    $hudText = [System.IO.File]::ReadAllText($hudIntegrationCpp)
    $helperStart = $hudText.IndexOf('bool ScreenPanel::handleHudMouseMove')
    if ($helperStart -lt 0) {
        $errors.Add('Could not locate ScreenPanel::handleHudMouseMove for helper-aware auditing.') | Out-Null
    }
    else {
        $helperBody = $hudText.Substring($helperStart)
        $helperEnd = $helperBody.IndexOf("`n}", [System.StringComparison]::Ordinal)
        if ($helperEnd -gt 0) { $helperBody = $helperBody.Substring(0, $helperEnd + 2) }
        $editGuard = $helperBody.IndexOf('CustomHud_IsEditMode', [System.StringComparison]::Ordinal)
        $configLookup = $helperBody.IndexOf('getLocalConfig', [System.StringComparison]::Ordinal)
        if ($editGuard -lt 0) {
            $errors.Add('handleHudMouseMove must retain its CustomHud_IsEditMode() guard.') | Out-Null
        }
        elseif ($configLookup -ge 0 -and $editGuard -gt $configLookup) {
            $errors.Add('handleHudMouseMove must validate edit mode before the slow Config lookup.') | Out-Null
        }
    }
}

# Renderer transition walks may snapshot raw panel addresses under their
# registry mutex, but Quiesce is allowed to wait for GPU work and must run only
# after that mutex has been released. The emulation-thread transition barrier
# is the lifetime proof for the copied addresses; this check prevents the
# original lock-across-Quiesce regression from returning.
foreach ($registrySpec in @(
        @{ Path = $dx12Cpp; Signature = 'void ScreenPanelDX12::PrepareForInstanceRendererTransition' },
        @{ Path = $vulkanCpp; Signature = 'void ScreenPanelVulkan::PrepareForInstanceRendererTransition' })) {
    if (-not (Test-Path -LiteralPath $registrySpec.Path)) {
        $errors.Add("$($registrySpec.Path) is missing: renderer registry lock audit cannot run.") | Out-Null
        continue
    }
    $registryText = [System.IO.File]::ReadAllText($registrySpec.Path)
    $registryStart = $registryText.IndexOf($registrySpec.Signature, [System.StringComparison]::Ordinal)
    if ($registryStart -lt 0) {
        $errors.Add("Could not locate $($registrySpec.Signature).") | Out-Null
        continue
    }
    $registryBody = $registryText.Substring($registryStart)
    $registryEnd = $registryBody.IndexOf("`n}", [System.StringComparison]::Ordinal)
    if ($registryEnd -gt 0) { $registryBody = $registryBody.Substring(0, $registryEnd + 2) }
    $lockPos = $registryBody.IndexOf('QMutexLocker lock', [System.StringComparison]::Ordinal)
    $snapshotLoopPos = $registryBody.IndexOf('for (ScreenPanel', [System.StringComparison]::Ordinal)
    $callMatch = [regex]::Match($registryBody, 'panel->prepareForRendererTransition\s*\(')
    $callPos = if ($callMatch.Success) { $callMatch.Index } else { -1 }
    $unlockedLoopPos = $registryBody.IndexOf('for (ScreenPanel', $snapshotLoopPos + 1, [System.StringComparison]::Ordinal)
    if ($lockPos -lt 0 -or $snapshotLoopPos -lt 0 -or $callPos -lt 0 -or $unlockedLoopPos -lt 0 -or $unlockedLoopPos -gt $callPos) {
        $errors.Add("$($registrySpec.Signature) must snapshot matching panels and call prepareForRendererTransition() after the registry lock scope.") | Out-Null
    }
}

# -- report --------------------------------------------------------------------

if ($Json) {
    [pscustomobject]@{
        HudFragmentBudget = $HudFragmentBudget
        HudFragmentCount = $hudFragments.Count
        Errors = @($errors)
    } | ConvertTo-Json -Depth 4
}
else {
    Write-Host 'Screen panel SRP / hot-path audit'
    Write-Host "  HUD unity fragments: $($hudFragments.Count) / $HudFragmentBudget"
    Write-Host "  findings: $($errors.Count)"
    foreach ($message in $errors) { Write-Host "  $message" }
}

if ($errors.Count -ne 0) {
    Write-Error 'Screen panel SRP/hot-path regression detected.'
    exit 1
}

Write-Host 'PASS: Screen panel SRP and hot-path rules hold.'
exit 0
