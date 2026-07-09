# MelonPrime Metal Backend Plan (Phase 0–10) — macOS Native Metal Renderer

**Created:** 2026-07-09
**Branch:** `highres_fonts_v3`
**2026-07-10 update:** user-reported flicker + black-3D were audited and root-caused; the fix and the
remaining Phase 8/9 work are now driven by the dedicated phased plan
[plan/metal_flicker_black3d_full_fix_phased_plan.md](plan/metal_flicker_black3d_full_fix_phased_plan.md)
(clear-pass depth bug, final-composer routing removal, GetLine integration, GLRenderer-mirror hires).
Read that plan first; this file remains the phase-tracked history.
**Status:** In progress, executing all phases in order per explicit user direction (2026-07-09):
commit + push after each phase, progress recorded here. This session's sandbox turned out to be a
**real Intel Mac** (`Intel Iris Plus Graphics 655`, Metal 3, macOS 15.7.7 — see §5) with a display,
so Intel-side implementation, compilation, and runtime smoke testing are genuinely possible here,
not just Phase 0's config-normalization logic. **Apple Silicon parity is still unverifiable from
this session** — every phase below states explicitly what was Intel-verified vs. still needs an
Apple Silicon Mac. Do not read "Intel-verified" as "cross-platform-verified."
**Source:** this plan operationalizes the external design document
[plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md](plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md)
("MelonPrime Metal Backend Implementation Guide — Final v7"), pasted into the session on
2026-07-09. That document has the full 24-section spec (per-phase code sketches, commit sequence,
testing matrix); this file is the phase-tracked, repo-integrated summary of it, trimmed to match
this repo's planning conventions (see
[melonprime-full-refactor-plan-v7.md](melonprime-full-refactor-plan-v7.md) for the format this
follows). Read the source document before starting any phase in §3 below — the summaries here are
not a substitute for its code sketches.

**2026-07-10 Phase 0 flicker/black-3D stopgap:** implemented from
[plan/metal_flicker_black3d_full_fix_phased_plan.md](plan/metal_flicker_black3d_full_fix_phased_plan.md).
Metal 3D target clearing now relies on Metal load-action clears only, preserving depth=1.0 instead
of drawing the clear triangle that overwrote depth to 0.0. The final Metal output no longer exposes
raw native 3D in normal mode; it uploads the completed CPU-composited screens to both layers while
native 3D continues running for diagnostics. Raw native visibility is now gated by
`MELONPRIME_METAL_NATIVE_3D_VISIBLE=1`, diagnostic route/color behavior stays behind diagnostic
env flags, and the presenter skips active frames with no available source before acquiring a
drawable. Verified by `tools/macos/build_metal_test.command`, `cmake --build build-mac --parallel 4`,
default-binary strings check, and the standard config/inc/literal/scatter audits. Real MPH ROM
smoke and `MELONPRIME_METAL_DIAG=1` runtime logs were not run in this workspace because no `.nds`
ROM is present.

**2026-07-10 Phase 1 layer lifecycle hardening:** `ScreenPanelMetal` now handles Qt native view
recreation by reattaching the existing `CAMetalLayer` to the current `NSView` on `WinIdChange`,
`Show`, and `WindowStateChange`, and also during layout recalculation before drawable-size updates.
The direct layer-hosting model is unchanged; sublayer/child-NSView alternatives remain deferred
unless checker-mode/manual smoke proves direct hosting still flickers. Verified by the Metal test
build, default build, default-binary strings check, and the standard audits. Checker-mode,
fullscreen/resize/screen-move, and input smoke still need manual GUI/ROM validation.

**2026-07-10 Phase 2 GetLine integration:** normal Metal visibility now flows through the existing
soft 2D compositor instead of the temporary whole-layer final texture route. `MetalRenderer3D`
renders native 3D at 256x192, reads BGRA8 back into the `SoftRenderer3D::GetLine()` 6-bit RGB +
alpha format, applies `RenderXPos` scrolling, and returns those scanlines to `GPU2D_Soft`.
Software 3D delegate rendering is disabled in normal native mode and is used only for
`MELONPRIME_METAL_GETLINE_SOURCE=soft` or `MELONPRIME_METAL_GETLINE_DIFF=1`. `MetalRenderer::VBlank()`
no longer composes a Metal final texture, and `GetOutput()` returns the CPU BGRA frame produced by
the normal soft final-screen path with Metal 3D already integrated. The presenter treats that as
`MetalGetLineCpuComposite`, not as software fallback. Verified by the Metal test build, default
build, default-binary strings check, and standard audits. Runtime A/B diff, MPH HUD-inclusive
visual checks, and perf logs still need a ROM.

**2026-07-10 Phase 3 partial parity (3-1/3-2):** plain clear depth now uses the DS clear-plane
depth from `RenderClearAttr2` instead of always clearing Metal depth to 1.0. Opaque polygon
submission now preserves `RenderPolygonRAM` order by batching only adjacent runs with the same
W-buffer / texture / TexRepeat key, matching the GL renderer's `RenderPolygonBatch` responsibility
instead of unordered whole-frame coalescing. Clear bitmap, clear polyID/fog attributes,
translucency, shadow, toon/highlight, fog, and edge marking remain open Phase 3 work. Verified by
the Metal test build and default build; standard audits and ROM A/B diff should be rerun after the
next Phase 3 parity slice.

**2026-07-10 Phase 3 partial parity (3-3 first slice):** Metal fragment output now preserves
polygon alpha instead of forcing alpha to 1.0. Non-shadow translucent triangle polygons are included
in the native pass, with separate blended render pipelines for Z/W-buffer variants and depth states
covering less vs. less-equal plus write vs. no-write. This is a parity step toward GL's
translucent path, but stencil/polyID rules for same-ID overwrite suppression and shadows are still
open. Verified by the Metal test build, default build, default-binary strings check, and standard
audits; ROM A/B diff remains pending.

**2026-07-10 Phase 3 partial parity (3-3 stencil slice):** opaque Metal polygons now replace
stencil with their polyID, while translucent polygons use a `0x40|polyID` not-equal stencil test
and replace on pass. This moves the native pass toward GL's same-translucent-ID overwrite
suppression. Verified by the Metal test build, default build, default-binary strings check, and
standard audits.

**2026-07-10 Phase 3 partial parity (3-5 toon/highlight slice):** Metal 3D now passes
`RenderDispCnt` and the 32-entry toon table into the native shader and mirrors `3DRenderFS.glsl`
for `blendmode==2`: toon mode replaces vertex RGB from the toon table, while highlight mode folds
vertex RGB to red and adds the toon color after texture/no-texture blending. Shadow-specific
stencil passes and the remaining clear-bitmap/fog/edge work are still open. Verified by the Metal
test build, default build, default-binary strings check, and standard audits.

**2026-07-10 Phase 3 partial parity (3-1/3-6 attr target slice):** the plain clear path now clears
the Metal attr target with `RenderClearAttr1`'s clear polyID and fog flag, matching `3DClearFS.glsl`
for the non-bitmap clear plane. Opaque Metal fragments now output GL-style attr data
(`polyID`, edge marker placeholder 0, fog flag, alpha 1), giving the later fog/edge post-pass the
same source fields GL reads from `AttrBuffer`. Translucent attr output intentionally stays on the
old zeroed placeholder until its GL color-mask behavior is ported. Clear bitmap, shadow-specific
stencil passes, and final fog/edge post-processing are still open. Verified by the Metal test
build, default build, default-binary strings check, and standard audits.

**2026-07-10 Phase 3 partial parity (3-1 clear bitmap slice):** clear bitmap mode
(`RenderDispCnt` bit14) now uploads VRAM texture slots 2/3 into Metal `RGBA8Uint` and `R32Uint`
textures after the shared Texcache dirty check. `ClearNativeTarget()` draws a fullscreen clear pass
that repeats those 256x256 textures with the DS clear-bitmap offset and writes color, attr polyID,
fog flag, and depth like `3DClearBitmapFS.glsl`. Shadow-specific stencil passes and final fog/edge
post-processing are still open. Verified by the Metal test build, default build, default-binary
strings check, and standard audits.

**2026-07-10 Phase 3 partial parity (3-6 fog final pass slice):** Metal now runs a fullscreen fog
post-pass after native polygon rendering and before GetLine readback when `RenderDispCnt` bit7 is
set. The shader reads the native depth/attr targets, uses `RenderFogOffset`, `RenderFogShift`, and
`RenderFogDensityTable` for GL-style density, and applies either color fog or alpha-only fog through
Metal blend states keyed by `RenderDispCnt` bit6. Edge marking and shadow-specific stencil passes
remain open. Verified by the Metal test build, default build, default-binary strings check, and
standard audits.

**2026-07-10 Phase 3 partial parity (3-6 edge final pass slice):** opaque Metal fragments now mark
attr.g when edge marking is enabled, and the final post-pass runs an edge shader before fog. The
shader compares adjacent depth/polyID samples from the native depth/attr targets and blends the
appropriate `RenderEdgeTable` color using the same half-alpha rule as GL when antialiasing is
enabled. Shadow-specific stencil passes and translucent attr color-mask parity remain open.
Verified by the Metal test build, default build, default-binary strings check, and standard audits.

**2026-07-10 Phase 3 partial parity (3-7 line polygon slice):** Metal now accepts
`Polygon::Type==1` instead of dropping line polygons. It follows GL's line setup by selecting the
first two non-duplicate final-position vertices, batching line and triangle primitives separately,
and submitting line groups with `MTLPrimitiveTypeLine` through the existing native shader/depth
state path. Shadow-specific stencil passes, VRAM display-capture textures, translucent attr
color-mask parity, and BetterPolygons remain open. Verified by the Metal test build, default build,
default-binary strings check, and standard audits.

**2026-07-10 Phase 3 partial parity (3-4 shadow mask stencil foundation):** Metal now submits DS
shadow-mask polygons through stencil-only Z/W pipelines instead of dropping them at the native pass
filter. Before mask submission it clears stencil bit 7 while preserving the existing lower polyID
bits, then marks bit 7 on depth-fail to mirror the foundation of GL's shadow-mask pass. Actual
shadow polygons are still intentionally skipped, so visible shadow parity remains open. Verified by
the Metal test build, default build, default-binary strings check, `git diff --check`, and standard
audits.

**2026-07-10 Phase 3 partial parity (3-4 visible shadow polygon slice):** Metal now submits actual
`IsShadow` polygons through a GL-style two-step stencil path. The first draw runs the regular
texture/alpha discard logic with color writes disabled and clears shadow-mask bit 7 where the
shadow polygon overlaps the same lower polyID; the second draw blends the shadow only where bit 7
remains set and replaces the lower stencil bits with `0x40|polyID`. Shadow visual exactness,
especially ROM A/B parity and the still-open translucent attr color-mask behavior, remains pending.
Verified by the Metal test build, default build, default-binary strings check, `git diff --check`,
and standard audits.

**2026-07-10 Phase 3 partial parity (3-3 translucent attr color-mask slice):** Metal translucent
draws now mirror GL's attr-target color mask for fog participation. Normal translucent/shadow draws
preserve the existing attr target, while fog-participating translucent draws use dedicated pipeline
variants that write only attr.b to clear the fog flag, matching `glColorMaski(1, false, false,
transfog, false)`. Verified by the Metal test build, default build, default-binary strings check,
`git diff --check`, and standard audits.

**2026-07-10 Phase 3 partial parity (3-7 VRAM display-capture texture slice):** the Metal native
3D path now explicitly detects direct-color textures backed by DS display-capture VRAM blocks and
keeps them on the shared `Texcache<>` path. Unlike GL's GPU-only capture-output arrays, this
`SoftRenderer`-derived Metal integration receives display captures after `SoftRenderer::DoCapture()`
writes them into emulated VRAM, so normal Texcache decoding is the correct Metal sampling path.
The first-pass diagnostics now report `captureTextured` counts for ROM validation. Verified by the
Metal test build, default build, default-binary strings check, `git diff --check`, and standard
audits.

**2026-07-10 Phase 3 partial parity (3-7 BetterPolygons slice):** Metal now honors the
`BetterPolygons` renderer setting. `MetalRenderer::SetRenderSettings()` forwards the setting to
`MetalRenderer3D`, and native polygon upload mirrors GL's center-vertex triangulation for polygons
with more than three vertices while keeping regular fan splitting when the option is disabled.
Verified by the Metal test build, default build, default-binary strings check, `git diff --check`,
and standard audits.

---

## 0. Goal and constraints

**Goal:** an optional native Metal presentation/rendering path for macOS, without changing what
the existing `High2` preset means anywhere (it stays "OpenGL compute", including on macOS where
it is UI-disabled — see [melonprime_macos_compute_renderer_restriction.md](melonprime_macos_compute_renderer_restriction.md)).

**Non-negotiable safety property:** every Metal-related file, enum value, config read, and UI
element must be removable by flipping one CMake flag
(`-DMELONPRIME_FORCE_DISABLE_METAL=ON`) with **zero source edits**. If disabling that flag still
leaves any observable Metal behavior, the phase that introduced it is too invasive and must be
tightened before merging.

**Non-goal (first cut):** porting `GPU3D_Compute.cpp` (the OpenGL compute renderer) directly to
Metal. See [compute-renderer-mosaic-bug.md](compute-renderer-mosaic-bug.md) for why that renderer
class is fragile even on its native OpenGL path; a Metal compute renderer is Phase 10 (stretch),
after the regular Metal 3D renderer (ported from `GLRenderer3D`, not `ComputeRenderer3D`) is
correct on both Intel and Apple Silicon.

**Why this is still phased even with real Intel hardware available:** the design document gates
most phases behind acceptance criteria that need **both** Intel and Apple Silicon confirmation
(gameplay parity across architectures, Xcode Metal API Validation, Instruments frame traces,
multi-window `CAMetalLayer` lifecycle testing). This session can satisfy the Intel half of that —
real build, real launch, real GPU (`Intel Iris Plus Graphics 655`, Metal 3) — but never the Apple
Silicon half. Each phase below is explicit about which half it covers. Treat any phase marked
"Intel-verified only" as unreleased/unexposed until an Apple Silicon session confirms parity, even
though the code compiles and the kill switch keeps it out of default builds either way.

---

## 1. Master kill switch (implemented, 2026-07-09)

CMake options in [src/frontend/qt_sdl/CMakeLists.txt](../../src/frontend/qt_sdl/CMakeLists.txt),
placed immediately after `MELONPRIME_ENABLE_DEVELOPER_FEATURES`:

```cmake
option(MELONPRIME_ENABLE_METAL
    "Enable experimental MelonPrime native Metal presenter/backend on macOS (not yet implemented)"
    OFF)
option(MELONPRIME_FORCE_DISABLE_METAL
    "Force-disable all MelonPrime Metal code, even if MELONPRIME_ENABLE_METAL is ON"
    OFF)

set(MELONPRIME_METAL_ACTIVE OFF)
if (MELONPRIME_FORCE_DISABLE_METAL)
    set(MELONPRIME_METAL_ACTIVE OFF)
elseif (MELONPRIME_ENABLE_METAL)
    if (NOT APPLE)
        message(FATAL_ERROR "MELONPRIME_ENABLE_METAL is only supported on Apple platforms")
    endif()
    set(MELONPRIME_METAL_ACTIVE ON)
endif()
```

`MELONPRIME_ENABLE_METAL=1` is only defined for the `melonDS` target when `MELONPRIME_METAL_ACTIVE`
is true. As of this commit **nothing consumes that macro yet** — no `.mm` file, no framework link,
no `renderer3D_Metal` enum value, no UI element. Future phases must add their
`target_sources()` / `find_library()` calls inside the same `if (MELONPRIME_METAL_ACTIVE)` guard,
never behind `#ifdef __APPLE__` alone (see the design document's "Forbidden patterns" section —
`__APPLE__`-only gating lets Metal code/config leak into a disabled build).

**Verified 2026-07-09 (local macOS build, `build-mac`):**
- Default config (`MELONPRIME_ENABLE_METAL=OFF`, `MELONPRIME_FORCE_DISABLE_METAL=OFF`) builds clean.
- `-DMELONPRIME_FORCE_DISABLE_METAL=ON` configures and builds clean (no-op relative to default,
  as expected since nothing is guarded by the active flag yet).
- `-DMELONPRIME_ENABLE_METAL=ON` (non-Apple guard not triggered since building on macOS)
  configures and builds clean, defining `MELONPRIME_ENABLE_METAL=1` with no code change since
  nothing reads it.
- `strings`/`nm -C` on the built binary show no `MELONPRIME_ENABLE_METAL`, `MelonPrimeScreenMetal`,
  or `MelonPrimeMetalFeatureCheck` symbols/strings in the default (disabled) configuration.
- Reset back to default config after verification; the tree currently in the repo builds with
  Metal fully disabled.

---

## 2. Phase 0 — renderer-selection safety hardening (implemented, 2026-07-09)

**Problem this closes:** [melonprime_macos_compute_renderer_restriction.md](melonprime_macos_compute_renderer_restriction.md)
already disables the `High2` preset **button** on macOS, but explicitly flags as unresolved:
> No normalization of an already-saved `3D.Renderer = renderer3D_OpenGLCompute` config value on
> macOS... If this needs closing, do it as a config-normalization change (e.g. near renderer
> creation / `onUpdateVideoSettings`) in a separate commit.

This phase is exactly that separate commit. It does not touch UI, does not add Metal, and does
not change behavior on Windows/Linux or on macOS with a valid config — it only prevents a stale,
imported, or hand-edited `3D.Renderer=renderer3D_OpenGLCompute` (or any out-of-range integer) from
reaching `EmuThread::updateRenderer()`'s `nds->SetRenderer(...)` switch on macOS.

### 2.1 New files

- [src/frontend/qt_sdl/MelonPrimeVideoBackend.h](../../src/frontend/qt_sdl/MelonPrimeVideoBackend.h)
- [src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp](../../src/frontend/qt_sdl/MelonPrimeVideoBackend.cpp)

```cpp
namespace MelonPrime::VideoBackend {
    int NormalizeRendererForPlatform(int requested);
    bool RendererRequiresOpenGLContext(int renderer);
}
```

`NormalizeRendererForPlatform`: on `__APPLE__` builds with `OGLRENDERER_ENABLED`, maps
`renderer3D_OpenGLCompute` → `renderer3D_OpenGL`; on all platforms, clamps any value outside the
known `renderer3D_*` enum to `renderer3D_Software`. Everything else passes through unchanged
(this is currently a no-op on Windows/Linux and on macOS with `Software`/`OpenGL`/an already-valid
config). `RendererRequiresOpenGLContext`: true only for `OpenGL`/`OpenGLCompute`, kept as its own
predicate (rather than "not Software") specifically so a future `renderer3D_Metal` value is not
mistaken for needing a GL context.

### 2.2 Call sites

- [MelonPrimeEmuThreadUpdateRendererBefore.inc](../../src/frontend/qt_sdl/MelonPrimeEmuThreadUpdateRendererBefore.inc)
  (existing MelonPrime hook fragment included by `EmuThread::updateRenderer()` right before the
  `switch (videoRenderer) { ... }` that calls `nds->SetRenderer(...)`): normalizes `videoRenderer`
  in place before the switch runs. `EmuThread.cpp` itself is untouched — the fix lives entirely in
  the existing fork-owned `.inc` fragment, so this closes the crash path with zero upstream diff
  and does not need to touch the `default: __builtin_unreachable();` case in the upstream switch
  (normalization is exhaustive over the known enum, so that case remains genuinely unreachable).
- [EmuInstance.cpp](../../src/frontend/qt_sdl/EmuInstance.cpp) `usesOpenGL()`: routed through the
  same normalize + `RendererRequiresOpenGLContext` pair, wrapped in
  `#ifdef MELONPRIME_DS ... #else ... #endif` per
  [non-melonprime-upstream-diff.md](non-melonprime-upstream-diff.md) (the `#else` branch preserves
  the exact pre-existing upstream expression). This is a no-op today on every platform (OpenGL and
  OpenGLCompute both require a GL context, so normalizing Compute→OpenGL doesn't change the
  boolean), but is now future-proofed: a later `renderer3D_Metal` value will correctly report
  `false` here instead of forcing an OpenGL context it doesn't need.
- `#include "MelonPrimeVideoBackend.h"` added to
  [MelonPrimeEmuThreadIncludes.inc](../../src/frontend/qt_sdl/MelonPrimeEmuThreadIncludes.inc) and
  directly in `EmuInstance.cpp`'s existing `#ifdef MELONPRIME_DS` include block.
- `MelonPrimeVideoBackend.cpp` registered in `SOURCES_QT_SDL` in
  [CMakeLists.txt](../../src/frontend/qt_sdl/CMakeLists.txt), next to `MelonPrimeRuntimeConfig.cpp`
  (same "pure policy/config-normalization" SRP category — see
  [repo-architecture.md](repo-architecture.md) "SRP v3 Unit Ownership").

**Deliberately not touched:** `MainWindow::createScreenPanel()`'s `hasOGL` computation in
`Window.cpp`. That decides which Qt panel class to construct (`ScreenPanelGL` vs
`ScreenPanelNative`), which is a separate concern from which 3D renderer backend gets attached to
`NDS` — the actual macOS compute-renderer crash traces to `GLRenderer(*nds, true)` construction in
`updateRenderer()`, not to panel selection. `createScreenPanel()`'s presentation-backend split is
Phase 1 material (design doc §7) and is deferred — it touches window creation on all three
platforms and needs its own review, not bundled into a safety-hardening commit.

### 2.3 Verification (2026-07-09, local macOS build only)

- `cmake -B build-mac && cmake --build build-mac --parallel 4` — clean build, no new warnings.
- `pwsh .claude/skills/audit-config-defaults.ps1` — PASS.
- `pwsh .claude/skills/check-inc-ownership.ps1` — PASS (56 `.inc` files, `MelonPrimeVideoBackend.h`
  is a normal header, not a unity fragment, so it is not part of this count).
- Object-file symbol check: `MelonPrime::VideoBackend::NormalizeRendererForPlatform(int)` and
  `RendererRequiresOpenGLContext(int)` both present as external `T` symbols in the intermediate
  `.o`; both fully inlined away in the final Release-linked binary (expected — two tiny functions,
  two call sites, no reason for the linker to keep them out-of-line).
- **Not verified:** actually loading a stale `3D.Renderer=renderer3D_OpenGLCompute` TOML on macOS
  and confirming no crash at runtime (needs manual TOML edit + app launch — not done this session).
  This is a straightforward manual check for a maintainer: edit `~/Library/.../melonDS.ini` (or the
  portable config) to set `3D.Renderer=2` on macOS, launch, confirm regular OpenGL renders instead
  of crashing.
- **Not verified:** Windows/Linux CI (this session has no access to those runners). The change is
  a no-op on those platforms by construction (`__APPLE__`-gated compute remap; the enum-range
  clamp is the only other branch, and out-of-range values were already going through
  `default: __builtin_unreachable()` there too — i.e. this phase can only make Windows/Linux
  *safer*, never behaviorally different for valid configs).

---

## 3. Phase 1 — presentation backend split (implemented, 2026-07-09)

Design doc §7 scope, trimmed to a minimal-diff version once the actual call-site inventory was
checked: `hasOGL` (a `MainWindow` bool) turned out to have exactly **one** place that computes it
(`MainWindow::createScreenPanel()`) and many places that only *read* it as a guard
(`getOGLContext()`, `initOpenGL()`, `deinitOpenGL()`, `setGLSwapInterval()`, `makeCurrentGL()`,
`releaseGL()`, `onUpdateVideoSettings()`). Those reader guards stay correct forever under a future
Metal panel too — a Metal panel would want `hasOGL == false` — so they were **not** touched. Only
the single computation site was routed through the new shared resolver.

### 3.1 New API

Added to [MelonPrimeVideoBackend.h/.cpp](../../src/frontend/qt_sdl/MelonPrimeVideoBackend.h):

```cpp
enum class PresentationBackend { NativeQt, OpenGL, /* Metal, only when MELONPRIME_ENABLE_METAL */ };
PresentationBackend ResolvePresentationBackend(bool useGLConfig, int requestedRenderer);
bool IsOpenGLPresentation(PresentationBackend backend);
```

`ResolvePresentationBackend` normalizes `requestedRenderer` via Phase 0's
`NormalizeRendererForPlatform` first, so this automatically inherits the same macOS
compute-renderer safety fix. Deliberately takes raw primitives (not `Config::Table&`), mirroring
`NormalizeRendererForPlatform`'s existing shape, so this header stays independent of `Config.h`.

### 3.2 Call site

[Window.cpp](../../src/frontend/qt_sdl/Window.cpp) `MainWindow::createScreenPanel()`:

```cpp
hasOGL = MelonPrime::VideoBackend::IsOpenGLPresentation(
    MelonPrime::VideoBackend::ResolvePresentationBackend(
        globalCfg.GetBool("Screen.UseGL"), globalCfg.GetInt("3D.Renderer")));
```

wrapped in `#ifdef MELONPRIME_DS ... #else` with the original expression preserved in the `#else`
branch (upstream-diff-hygiene convention).

**Not a byte-for-byte no-op:** for the previously-undefined case of an out-of-range `3D.Renderer`
value (corrupted config, or a value from a newer build this one doesn't know), old code computed
`hasOGL = true` (since `invalid != Software` is vacuously true) even though the actual 3D renderer
would fall back to Software regardless. New code normalizes the out-of-range value to Software
*before* the OR, so `hasOGL` correctly becomes `false` too. This is a strict safety improvement
(a `Software`-backed window no longer spuriously requests a GL context/surface) and mirrors Phase
0's own "invalid renderer value -> Software fallback" acceptance criterion — documented here
rather than silently claimed as "zero behavior change."

### 3.3 Verification (2026-07-09, Intel Mac, local build)

- `cmake --build build-mac --parallel 4` — clean, no new warnings, only the touched TUs recompiled.
- Launched `build-mac/melonPrimeDS.app` (`open` + `pgrep`), confirmed the process stays up (no-ROM
  splash path reachable, no immediate crash), then quit it (`pkill`).
- `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files) — PASS.
- `audit-metroid-literal-budget.ps1 -Budget 1` — PASS (unaffected by this phase).
- `audit-platform-scatter-budget.ps1 -Budget 22`: the new `#if defined(__APPLE__) &&
  defined(OGLRENDERER_ENABLED)` in `MelonPrimeVideoBackend.cpp` initially pushed the count to
  23/22. Fixed by adding the repo's existing `scatter-budget-exempt: <reason>` inline-comment
  marker (same mechanism V7 Phase 1 introduced for `InputConfig.cpp`'s macOS compute-renderer UI
  gate) rather than raising the ratchet — this marker is a renderer-selection normalization gate,
  not input dispatch, matching the marker's documented scope. Back to 22/22 after the fix.
- **Not verified:** interactive GUI regression of the panel-swap path (`onUpdateVideoSettings` /
  toggling `Screen.UseGL` at runtime) — needs a human clicking through Settings, not just a launch
  smoke test.

---

## 3a. Phase 2 — Metal feature probe (implemented and *runtime-verified*, 2026-07-09)

Design doc §8 scope. New files
[MelonPrimeMetalFeatureCheck.h](../../src/frontend/qt_sdl/MelonPrimeMetalFeatureCheck.h) /
[.mm](../../src/frontend/qt_sdl/MelonPrimeMetalFeatureCheck.mm), compiled only inside
`if (MELONPRIME_METAL_ACTIVE)` in CMakeLists.txt (own `find_library(METAL_LIB Metal)` +
`-fobjc-arc`, matching the existing `MelonPrimeRawInputMacFilter.mm` ARC convention).

```cpp
namespace MelonPrime::Metal {
    struct FeatureInfo { bool hasDevice; bool supportsRequiredBaseline; bool isLowPower;
        bool isRemovable; bool hasUnifiedMemory; uint64_t recommendedMaxWorkingSetSize;
        std::string deviceName; std::string unavailableReason; };
    const FeatureInfo& CachedFeatureInfo();  // std::call_once probe + cache
    bool IsRuntimeAvailable();               // hasDevice
    bool SupportsRequiredBaseline();         // the gate Phase 4+ must check
    void LogFeatureInfoOnce();               // stderr, matches the mac-input log convention
}
```

`ProbeFeatureInfo()` does exactly what the design doc's baseline check requires and nothing more:
`MTLCreateSystemDefaultDevice()` → `newCommandQueue` → compile a trivial passthrough vertex/
fragment `MTLLibrary` from an inline source string → `newRenderPipelineStateWithDescriptor`
targeting `MTLPixelFormatBGRA8Unorm` (the format the presenter will use). No Apple-GPU-family
checks, no `MTLArgumentBuffersTier2`, no `hasUnifiedMemory` requirement — Intel must pass this.

A single startup call site was added, gated the same way:
[main.cpp](../../src/frontend/qt_sdl/main.cpp), right after the version banner print:
`MelonPrime::Metal::LogFeatureInfoOnce();`. This exists purely as a Phase 2 diagnostic (nothing
gates renderer/presenter creation on it yet — that starts at Phase 4); it is harmless dead-ish
code once Phase 4 adds a real caller of `SupportsRequiredBaseline()`.

### Runtime verification (2026-07-09, real hardware — not just a compile check)

Built a separate tree, `build-mac-metal-test`, configured with `-DMELONPRIME_ENABLE_METAL=ON
-DMELONPRIME_ENABLE_DEVELOPER_FEATURES=ON` (the shipped default tree, `build-mac`, stays
Metal-disabled). Launched the resulting binary and captured stderr:

```text
[MelonPrime] metal probe: device='Intel(R) Iris(TM) Plus Graphics 655' lowPower=1 removable=0 unifiedMemory=1 recommendedMaxWorkingSetSize=1610612736
```

This is a genuine, real end-to-end pass on this session's Intel Iris Plus 655 (Metal 3): device
query succeeded, command queue created, an actual Metal shader source string compiled through
`MTLCompiler`, both functions resolved, and a `BGRA8Unorm` render pipeline state constructed —
i.e. `supportsRequiredBaseline == true` on this machine, not merely "the code compiles."
`hasUnifiedMemory=1` is correct/expected here even though this GPU has dedicated VRAM listed in
`system_profiler` — Metal reports Intel integrated GPUs as unified-memory on macOS, unlike
discrete GPUs; this was **not** treated as a bug in the probe.

Also verified:
- Default (`build-mac`, Metal disabled) tree rebuilds clean after the shared `main.cpp` edit —
  `ninja: no work to do` for the unrelated ObjC guard, only `main.cpp` needed recompiling and did
  so with zero new warnings; `strings` on the binary shows zero `metal probe` / feature-check
  symbols/strings.
- `audit-platform-scatter-budget.ps1 -Budget 22`: two new `MelonPrime*.h`-glob-matched `__APPLE__`
  guards (`.h`'s opening `#if`; its closing `#endif` comment also originally repeated the
  `__APPLE__` token as plain text and got counted as a *second* hit by the same regex — fixed by
  rewording the comment rather than adding a second exempt marker) pushed the count to 24, then
  23 after the first exempt marker. Applied the same `scatter-budget-exempt:` marker used in
  Phase 1 to the header's `#if` line and reworded the endif comment; back to 22/22. (`.mm` files
  are not scanned by this ratchet at all — only `MelonPrime*.cpp`/`MelonPrime*.h` — so
  `MelonPrimeMetalFeatureCheck.mm`'s own `__APPLE__` mentions never counted.)
- `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files), `audit-metroid-literal-budget.ps1
  -Budget 1`, `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1` — all PASS
  (the two Screen.cpp "requires manual platform-guard review" lines from the SRP audit are
  pre-existing known items, unrelated to this phase).
- **Not verified:** Apple Silicon. This exact probe should behave identically there (no
  Apple-GPU-family-only checks were used), but that is an expectation, not a confirmed result.

---

## 3b. Phase 3 — EmuThread backend tracking (implemented, 2026-07-09, deliberately partial)

Design doc §9 scope, deliberately narrowed. `EmuThread::useOpenGL` (a bool) turned out to have
only **4 write sites** (`run()` startup, `msg_InitGL`, `msg_DeInitGL`) and **5 read sites**
(context-current gating around lines 317/324/470, the repaint-signal gate at line 437, and the
`videoSettingsDirty` VSync branch). Since `PresentationBackend::Metal` cannot exist as a
reachable value yet (no `ScreenPanelMetal`, no way to select it), none of the 5 *read* sites can
currently observe a third state — rewriting them now would be speculative, so **only the 4 write
sites were touched**, adding a new `videoBackend` member tracked in lockstep with `useOpenGL`.
The 5 read sites are explicitly deferred to Phase 4, which is the phase that actually needs them
to branch three ways.

```cpp
// EmuThread.h, added inside #ifdef MELONPRIME_DS
MelonPrime::VideoBackend::PresentationBackend videoBackend =
    MelonPrime::VideoBackend::PresentationBackend::NativeQt;
```

```cpp
// MelonPrimeVideoBackend.h/.cpp, added
PresentationBackend FromLegacyOpenGLFlag(bool useOpenGL); // OpenGL or NativeQt only
```

Each of the 4 `useOpenGL = ...;` assignments in [EmuThread.cpp](../../src/frontend/qt_sdl/EmuThread.cpp)
gained a companion `videoBackend = MelonPrime::VideoBackend::FromLegacyOpenGLFlag(useOpenGL);`
right after it, guarded by `#ifdef MELONPRIME_DS`. The `msg_DeInitGL` case's single-line
`if (msg.param.value<int>() == 0) useOpenGL = false;` was reshaped into a braced block (still the
exact same runtime behavior) so the companion line shares the same condition without re-evaluating
`msg.param.value<int>()` a second time.

`#include "MelonPrimeVideoBackend.h"` was added to `EmuThread.h` itself (inside the existing
`#ifdef MELONPRIME_DS` forward-declare block), following the precedent already set by `Screen.h`
including `MelonPrimeHudConfigOnScreenEdit.h` under `#ifdef MELONPRIME_CUSTOM_HUD` — pulling a
fork-owned header into an upstream header, properly guarded, is an accepted pattern in this repo.

### Verification (2026-07-09, Intel Mac)

- `cmake --build build-mac --parallel 4` — clean (the `EmuThread.h` include change cascaded into a
  wider-than-usual rebuild, as expected for a widely-included header; zero new warnings beyond the
  one pre-existing, unrelated `EditPropType::Color` switch warning).
- Launch/quit smoke test on `build-mac` (default, Metal disabled) — no crash; `strings` shows zero
  Metal symbols in the binary.
- Rebuilt and re-ran `build-mac-metal-test` (`MELONPRIME_ENABLE_METAL=ON`) — compiles clean with
  the 3-value enum now actually in scope for that config, feature probe still logs correctly.
- `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files), `audit-metroid-literal-budget.ps1
  -Budget 1`, `audit-platform-scatter-budget.ps1 -Budget 22` (unaffected — `EmuThread.h`/`.cpp` use
  `MELONPRIME_DS`, not `__APPLE__`/`__linux__`, so this ratchet doesn't see them at all),
  `audit-melonprime-srp-performance.ps1` — all PASS.
- **Not verified:** the 5 read sites' eventual 3-way branching (that's Phase 4's job, not this
  phase's). **Not verified:** Apple Silicon.

---

## 3c. Phase 4 — `ScreenPanelMetal` presenter (implemented and *runtime-verified on Intel*, 2026-07-09)

Design doc §10 scope, with the deliberate Phase 4 limitation intact: this is a
**presentation-only** `CAMetalLayer` path for the existing CPU BGRA framebuffers.
It does not add `renderer3D_Metal`, does not port any `GPU3D` renderer, and does
not redirect `High2`. While the Metal presenter is force-selected, requested
hardware 3D renderers are normalized to Software so `EmuThread::updateRenderer()`
never tries to construct a GL renderer for a window that has no GL surface.

### 3c.1 New files

- [MelonPrimeScreenMetal.h](../../src/frontend/qt_sdl/MelonPrimeScreenMetal.h)
- [MelonPrimeScreenMetal.mm](../../src/frontend/qt_sdl/MelonPrimeScreenMetal.mm)

`ScreenPanelMetal` subclasses `ScreenPanel`, attaches a `CAMetalLayer` directly
to the widget's native `NSView`, compiles a small Metal vertex/fragment pipeline
that mirrors the existing screen quad transform math, uploads the top/bottom
CPU framebuffers into two persistent `BGRA8Unorm` textures, and presents from
`drawScreen()` on the emu thread (matching the existing GL presenter call path).
Drawable size and scale are updated on GUI-thread layout changes and shared with
the emu thread through a small mutex-protected state block.

Phase 4 intentionally does **not** draw the no-ROM splash, OSD, or Custom HUD on
Metal yet. It calls `osdUpdate()` so OSD item bookkeeping does not leak, but
actual OSD/HUD/splash parity is Phase 5.

### 3c.2 Selection and lifecycle wiring

- `CMakeLists.txt`: when `MELONPRIME_METAL_ACTIVE` is true, links QuartzCore in
  addition to Metal, compiles `MelonPrimeScreenMetal.mm`, and applies `-fobjc-arc`
  to both Metal `.mm` files. The default/force-disabled build still sees no
  Metal presenter source or QuartzCore link.
- `MelonPrimeVideoBackend.h/.cpp`: adds the temporary developer bootstrap
  `MELONPRIME_FORCE_METAL_PRESENTER=1`. There is still no persisted config key
  and no UI; Phase 9 owns user exposure. The same resolver is now used by
  `EmuInstance::usesOpenGL()`, so `Screen.UseGL=true` cannot accidentally request
  a GL context when the env-forced Metal presenter owns presentation.
- `Window.cpp`: `MainWindow::createScreenPanel()` constructs `ScreenPanelMetal`
  when the resolver returns `PresentationBackend::Metal`, falling back to the
  existing GL/NativeQt path if `initMetal()` fails.
- `EmuThread.cpp`: Phase 3's `videoBackend` member now actually distinguishes
  `NativeQt` from `Metal` when no GL context is live. Existing GL context calls
  still key off `useOpenGL`; only the repaint-signal path now checks for
  `NativeQt` specifically, so Metal presents from `drawScreen()` instead of also
  scheduling Qt paint updates.

### 3c.3 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean after adding the
  presenter and QuartzCore link.
- `cmake --build build-mac --parallel 4` — default Metal-disabled build remains
  clean (only pre-existing `sprintf` and HUD `EditPropType::Color` warnings).
- Default binary string check: no `metal presenter`, `metal probe`,
  `MelonPrimeScreenMetal`, or `MELONPRIME_FORCE_METAL` strings in
  `build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS`.
- Runtime smoke with `MELONPRIME_FORCE_METAL_PRESENTER=1` on this session's real
  Intel Iris Plus 655 / Metal 3 machine: process stayed alive for 6 seconds and
  logged:

```text
[MelonPrime] metal probe: device='Intel(R) Iris(TM) Plus Graphics 655' lowPower=1 removable=0 unifiedMemory=1 recommendedMaxWorkingSetSize=1610612736
[MelonPrime] metal presenter: initialized drawable=512x768 scale=2.00
```

- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1`, and
  `generate-hud-prop-schema.py` + generated-file diff all pass. The SRP audit's
  two `Screen.cpp` manual-review lines are pre-existing raw-aim items, unchanged
  by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity,
  resize/fullscreen/manual HiDPI inspection, or Phase 5 OSD/HUD/splash parity.

---

## 3d. Phase 5 — Metal OSD + Custom HUD overlay parity (implemented and *runtime-smoked on Intel*, 2026-07-09)

Design doc §11 scope. `ScreenPanelMetal` now has a second premultiplied-alpha
UI pipeline for the overlays that the Phase 4 presenter intentionally skipped:
no-ROM splash, OSD messages, and MelonPrime Custom HUD. This stays within the
Phase 4/5 CPU-presenter model: the UI is rendered with existing Qt/QPainter HUD
code into a full logical-window `QImage`, uploaded as `BGRA8Unorm`, and blended
over the Metal-presented screens with `source=One`, `dest=OneMinusSourceAlpha`.

### 3d.1 Implementation notes

- Reuses `ScreenPanel::osdUpdate()` and `ScreenPanel::osdRenderItem()` for OSD
  and no-ROM text bitmap generation.
- Draws the no-ROM splash logo/text and live OSD messages into the Metal UI
  overlay instead of relying on Qt paint events.
- Calls `MelonPrime::CustomHud_Render()` directly for the Metal presenter,
  including the QPainter radar-frame/bottom-screen crop path used by the
  software presenter. The bottom DS framebuffer fetched for normal screen
  presentation is reused for the radar crop, avoiding a second
  `GPU.GetFramebuffers()` call.
- Leaves the GL-specific radar shader path alone. Metal's first-cut HUD parity
  follows the existing software/QPainter semantics; a later optimization can
  add a Metal-native radar shader if profiling proves this overlay upload is a
  bottleneck.
- Adds one-time presenter diagnostics for first draw and first UI overlay, so
  env-forced Metal sessions can confirm that the overlay path is actually
  executing even before ROM gameplay testing is available.

### 3d.2 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean after adding the UI
  pipeline and QPainter overlay upload path.
- `cmake --build build-mac --parallel 4` — default Metal-disabled build remains
  clean (only pre-existing `sprintf` and HUD `EditPropType::Color` warnings).
- Default binary string check: no `metal presenter`, `metal probe`,
  `MelonPrimeScreenMetal`, or `MELONPRIME_FORCE_METAL` strings in
  `build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS`.
- Runtime smoke with `MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive
  and logged the Metal presenter plus Phase 5 overlay path:

```text
[MelonPrime] metal presenter: initialized drawable=512x768 scale=2.00
[MelonPrime] metal presenter: first draw drawable=1678x1638 scale=2.00 active=0
[MelonPrime] metal presenter: first UI overlay 839x819 active=0
```

- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1`, and
  `generate-hud-prop-schema.py` + generated-file diff all pass. The SRP audit's
  two `Screen.cpp` manual-review lines are pre-existing raw-aim items, unchanged
  by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity
  for Custom HUD, live OSD messages, radar overlay, resize/fullscreen/manual
  HiDPI inspection.

---

## 3e. Phase 6 — `RendererOutput` abstraction (implemented, 2026-07-09)

Design doc §12 scope. The old `GPU::GetFramebuffers(void**, void**) -> bool`
contract is preserved for upstream compatibility, but frontend presenters no
longer need to infer "CPU vs GL texture" from a boolean plus loosely-typed
`void*` payload. New core types in [GPU.h](../../src/GPU.h):

```cpp
enum class RendererOutputKind {
    CpuBgra,
    OpenGLTextureArray,
#if defined(MELONPRIME_ENABLE_METAL)
    MetalTexture,
#endif
    None,
};

struct RendererOutput {
    RendererOutputKind Kind;
    void* Top;
    void* Bottom;
};
```

`Renderer::GetOutput()` defaults to wrapping the legacy virtual
`GetFramebuffers()` result: CPU renderers become `CpuBgra`; the existing GL
renderer's texture-array payload becomes `OpenGLTextureArray`. Future Metal
renderers can override `GetOutput()` and return `MetalTexture` explicitly.
`GPU::GetRendererOutput()` exposes the typed wrapper to frontend presenters.

Important kill-switch detail: `RendererOutputKind::MetalTexture` and
`RendererOutput::MetalTexture()` only exist when `MELONPRIME_ENABLE_METAL` is
defined. `CMakeLists.txt` now propagates that define to `core` only inside
`MELONPRIME_METAL_ACTIVE`, so the default/force-disabled build still has no
Metal output enum value in core.

### 3e.1 Call sites migrated

- `ScreenPanelNative::drawScreen()` now accepts only `CpuBgra`.
- `ScreenPanelGL::drawScreen()` branches explicitly on `CpuBgra` vs
  `OpenGLTextureArray` instead of using `GetFramebuffers()`'s bool result.
- `ScreenPanelMetal::drawScreen()` accepts only `CpuBgra` for the current Phase
  4/5 CPU presenter, making future `MetalTexture` output impossible to
  accidentally reinterpret as CPU bytes.

### 3e.2 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean after CMake
  reconfiguration; core was rebuilt with `MELONPRIME_ENABLE_METAL=1`.
- `cmake --build build-mac --parallel 4` — clean after CMake reconfiguration;
  default core built without the Metal output enum value (only pre-existing
  `sprintf` and HUD `EditPropType::Color` warnings).
- Runtime smoke with `MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive
  and logged Metal presenter first draw + first UI overlay, confirming the Metal
  presenter still receives CPU BGRA output via `RendererOutput`.
- Default binary string check: no `metal presenter`, `metal probe`,
  `MelonPrimeScreenMetal`, or `MELONPRIME_FORCE_METAL` strings in
  `build-mac/melonPrimeDS.app/Contents/MacOS/melonPrimeDS`.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1`, and
  `generate-hud-prop-schema.py` + generated-file diff all pass. The SRP audit's
  two `Screen.cpp` manual-review lines are pre-existing raw-aim items, unchanged
  by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity.

---

## 3f. Phase 7 completion — renderer shell + gated enum

Phase 7 adds the first Metal renderer identity without shipping a real Metal GPU3D implementation.
The new `renderer3D_Metal` enum value is compiled only when `MELONPRIME_ENABLE_METAL` is active, and
`EmuThread::updateRenderer()` can now construct a `melonDS::MetalRenderer` shell for that value.

The shell is intentionally non-functional: `MetalRenderer::Init()` logs that 2D/3D rendering is not
implemented yet and returns `false`, letting the existing `GPU::SetRenderer()` failure path fall back
to Software instead of crashing. `MetalRenderer::GetOutput()` currently returns `None`, and the
presenter therefore still relies on CPU BGRA output from Software for the Phase 4/5 Metal presenter.

Bootstrap selection remains developer-only:

- `MELONPRIME_FORCE_METAL_RENDERER=1` requests `renderer3D_Metal` in Metal-enabled builds.
- `MELONPRIME_FORCE_METAL_PRESENTER=1` still forces the CAMetalLayer presenter while normalizing
  non-Metal hardware renderers back to Software because no GL context exists in that path.
- No user-facing settings button was added; `High2` remains OpenGL-compute-only and remains disabled
  on macOS.

### 3f.1 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean; `GPU_Metal.cpp` is included only under
  `MELONPRIME_METAL_ACTIVE`.
- `cmake --build build-mac --parallel 4` — clean; default build has no Metal renderer strings or
  force-env strings.
- Runtime smoke with
  `MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive on
  Intel Iris Plus 655 and logged Metal probe, CAMetalLayer presenter initialization, first draw, and
  first UI overlay. No-ROM startup does not enter `EmuThread::updateRenderer()`, so the shell's
  fallback log was not runtime-exercised without a ROM.
- Metal-enabled binary symbol/string check confirms `melonDS::MetalRenderer` and
  `MELONPRIME_FORCE_METAL_RENDERER` are present only in `build-mac-metal-test`.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`, `audit-color-dialog-prefs.ps1`,
  `audit-melonprime-srp-performance.ps1`, and `generate-hud-prop-schema.py` + generated-file diff
  all pass. The SRP audit's two `Screen.cpp` manual-review lines are pre-existing raw-aim items,
  unchanged by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity or a real Metal GPU3D
  draw path.

---

## 3g. Phase 8 partial — Metal renderer correctness baseline

This first Phase 8 commit makes the Metal renderer identity produce real, correct frame output
instead of deliberately failing `Init()`. The implementation is conservative: `MetalRenderer`
derives from
`SoftRenderer`, so existing Software 2D/3D rendering, capture handling, savestate hooks, and CPU BGRA
framebuffers remain the source of truth while Phase 4/5's CAMetalLayer presenter handles macOS
presentation.

This gives a working Metal presentation path without constructing any OpenGL context and without
touching `GLRenderer3D` or `ComputeRenderer3D`. It is a correctness-first baseline, not a native
Metal GPU3D port. The native `GLRenderer3D`-to-Metal shader/texture/depth-stencil port remains
unimplemented and must not be implied by this commit. In completion-audit terms, this is not enough
to close Phase 8.

### 3g.1 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean; `GPU_Metal.cpp` rebuilt and linked
  against the existing software renderer path.
- `cmake --build build-mac --parallel 4` — clean; default build still excludes Metal renderer code.
- Runtime smoke with
  `MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive on
  Intel Iris Plus 655 and logged Metal probe, CAMetalLayer presenter initialization, first draw, and
  first UI overlay. No-ROM startup still does not enter `EmuThread::updateRenderer()`, so the new
  `MetalRenderer::Init()` log was not runtime-exercised in this session.
- Metal-enabled binary string/symbol check confirms `MetalRenderer`, `SoftRenderer` symbols, and
  the Phase 8 log string are present in `build-mac-metal-test`; default binary still has no `metal
  presenter`, `metal probe`, `metal renderer`, `MelonPrimeScreenMetal`, or
  `MELONPRIME_FORCE_METAL` strings.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`, `audit-color-dialog-prefs.ps1`,
  `audit-melonprime-srp-performance.ps1`, and `generate-hud-prop-schema.py` + generated-file diff
  all pass. The SRP audit's two `Screen.cpp` manual-review lines are pre-existing raw-aim items,
  unchanged by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity. **Not implemented:**
  native Metal `GLRenderer3D` port.

---

## 3h. Phase 8 follow-up — native Metal 3D resource scaffold

This follow-up moves Phase 8 closer to the original `GLRenderer3D` port target by adding a real
`MetalRenderer3D` class in core (`GPU3D_Metal.h/.mm`) and installing it into `MetalRenderer`'s
`Rend3D` slot. The class currently keeps a `SoftRenderer3D` delegate for raster correctness, but it
now owns the native Metal objects the eventual port needs:

- `MTLDevice`
- `MTLCommandQueue`
- BGRA8 color render target
- Depth32Float_Stencil8 depth/stencil target
- RGBA8 attribute target

Initialization allocates those resources and submits a real Metal render pass that clears the
native targets. `SetScaleFactor()` resizes the Metal targets; `RenderFrame()`/`GetLine()` still
delegate to Software until the shader, texture-cache, depth/stencil, fog/edge, and final-pass paths
are ported from `GLRenderer3D`.

The next follow-up adds the first MSL shader/pipeline scaffold inside `MetalRenderer3D`: a runtime
compiled clear shader library (`mp3d_clear_vs`/`mp3d_clear_fs`), `MTLRenderPipelineState`, and
`MTLDepthStencilState`. The native clear pass now binds that pipeline and issues a full-screen
triangle draw into the Metal color/attribute/depth-stencil targets, rather than relying only on
render-pass load actions. This is still not the DS polygon renderer, but it establishes the
`GLRenderer3D::BuildRenderShader()`/pipeline-state equivalent that later draw paths can extend.

### 3h.1 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean after reconfiguration; core compiled
  `GPU3D_Metal.mm` under the `MELONPRIME_METAL_ACTIVE` guard.
- `cmake --build build-mac --parallel 4` — clean after reconfiguration; default build still
  excludes `GPU3D_Metal.mm`.
- Metal-enabled binary symbol/string check confirms `melonDS::MetalRenderer3D` and its
  initialization strings exist only in `build-mac-metal-test`; the follow-up also confirms the MSL
  clear shader entry points and "MelonPrime Metal 3D Clear Pipeline" label exist only in the
  Metal-enabled binary. The default binary still has no `metal
  presenter`, `metal probe`, `metal renderer`, `MetalRenderer3D`, `GPU3D_Metal`,
  `MelonPrimeScreenMetal`, or `MELONPRIME_FORCE_METAL` strings.
- Runtime smoke with
  `MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive on
  Intel Iris Plus 655 and logged Metal probe, CAMetalLayer presenter initialization, first draw, and
  first UI overlay. No-ROM startup still does not enter `EmuThread::updateRenderer()`, so
  `MetalRenderer3D::Init()` was not runtime-exercised without a ROM.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1`,
  `audit-metroid-literal-budget.ps1 -Budget 1`,
  `audit-platform-scatter-budget.ps1 -Budget 22`, `audit-color-dialog-prefs.ps1`,
  `audit-melonprime-srp-performance.ps1`, and `generate-hud-prop-schema.py` + generated-file diff
  all pass. The SRP audit's two `Screen.cpp` manual-review lines are pre-existing raw-aim items,
  unchanged by this phase.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay visual parity. **Still not
  complete:** native Metal replacement for `GLRenderer3D`'s draw/shader/texture/depth-stencil
  implementation.

## 3i. Phase 8 audit fixes — renderer routing bug, dialog crash guard, resource hardening, probe strengthening

An external audit of `ea36e23c`/`92c9f752` (recorded verbatim in the session that produced this
commit) found one real correctness bug and several defense-in-depth gaps that had to be closed
before Phase 9 could safely build on top of the scaffold. All five items below were fixed and
verified together.

**1. `EmuThread` renderer routing bug (the audit's top-priority finding).** Both
`EmuThread::run()` and the `videoSettingsDirty` handling block computed `videoRenderer` as
`globalCfg.GetInt("3D.Renderer")` only when `useOpenGL` was true, and hardcoded `videoRenderer = 0`
(Software) otherwise. `useOpenGL` reflects whether the *presentation* backend needs a live OpenGL
context — it has nothing to do with which 3D renderer identity the user actually requested. Since
`PresentationBackend::Metal` (Phase 4) correctly resolves `useOpenGL == false` (Metal doesn't need
GL), a real (non-env-forced) `renderer3D_Metal` selection would have been silently discarded and
replaced with Software the moment Phase 9 gave users a way to pick it without going through the
`MELONPRIME_FORCE_METAL_RENDERER` bootstrap env var (which works today only because it overrides
`NormalizeRendererForPlatform`'s return value unconditionally, independent of the input). Fixed in
both call sites: `videoRenderer` is now always `MelonPrime::VideoBackend::NormalizeRendererForPlatform(...)`
of the requested config value; `useOpenGL`/context creation is decided separately by
`ResolvePresentationBackend`. The `videoSettingsDirty` block additionally has to account for
"no live GL context *this iteration*" (`useOpenGL` there only flips via the `msg_InitGL`/
`msg_DeInitGL` message handlers, not by this block) — so when `!useOpenGL`, the requested renderer
is clamped to Software *only if* `RendererRequiresOpenGLContext()` says it actually needs a
context; Metal (or Software) pass through unmodified. The non-`MELONPRIME_DS` (`#else`) paths in
both call sites are untouched, byte-identical to the pre-fix code.

**2. `VideoSettingsDialog` crash risk.** `grp3DRenderer->button(oldRenderer)->setChecked(true)` in
the constructor dereferences whatever `QButtonGroup::button()` returns for the saved
`3D.Renderer` config value — but only 3 radio buttons are registered (Software/OpenGL/Compute).
Metal doesn't have a button yet (Phase 9), so a config value of `renderer3D_Metal` (or any
unrecognized int, even in a non-Metal build, e.g. a stray value left over from a build with a
different renderer set compiled in) makes `button()` return `nullptr`, and the unconditional
`->setChecked(true)` call would crash. Fixed with a null check under `#ifdef MELONPRIME_DS`
(`#else` keeps the original one-liner byte-identical); `oldRenderer` itself is left untouched so
Cancel still restores the original value exactly, it just doesn't get displayed as checked.
`VideoSettingsDialog::UsesGL()` had the parallel problem: `renderer != renderer3D_Software` treats
Metal as "needs GL", which would wrongly enable this dialog's VSync-via-GL controls and treat any
change while a Metal renderer is configured as needing a GL context reinit. Routed through
`MelonPrime::VideoBackend::IsOpenGLPresentation(ResolvePresentationBackend(...))` instead
(`#else` keeps the original expression byte-identical).

**3. `MetalRenderer3D::ResizeTargets()` partial-failure safety.** Previously assigned each new
texture directly to `State->ColorTarget`/`DepthStencilTarget`/`AttrTarget` as it was created; if
(for example) the third allocation failed, `State` was left holding two new-size textures and one
stale old-size (or null) one. Now builds all three into local `id<MTLTexture>` variables first and
only commits them to `State` once all three succeeded — a failure leaves whatever was already in
`State` completely untouched instead of a mixed/inconsistent set.

**4. `MetalRenderer3D::ClearNativeTarget()` null guards.** Previously checked only that
`commandBuffer`/`encoder` creation succeeded; it read `State->ColorTarget` etc. unconditionally
before that, so a call before `CreateDeviceObjects()`/`ResizeTargets()` finished (or after a
`ResizeTargets()` failure this method didn't police itself) could dereference a null texture. Now
checks `State`, `CommandQueue`, `ClearPipeline`, `ClearDepthStencil`, and all three targets are
non-null up front and logs + returns `false` instead.

**5. Feature probe strengthened to match the actual Phase 8 pipeline shape.** The Phase 2 probe
(`MelonPrimeMetalFeatureCheck.mm`) only ever built a single-BGRA8-attachment pipeline, so
`SupportsRequiredBaseline()` could report success on a device/driver combination that then failed
inside `MetalRenderer3D::BuildClearPipeline()`'s actual shape (`color0=BGRA8Unorm`,
`color1=RGBA8Unorm`, `depth/stencil=Depth32Float_Stencil8`). The probe's shader and pipeline
descriptor now match that exact shape, and it additionally builds a real
`MTLDepthStencilState` and allocates a 1×1 private-storage `Depth32Float_Stencil8` texture — the
pipeline-descriptor check alone only proves the format combination is *legal to declare* for a
render pipeline, not that the device can actually *allocate* a texture in that combined format.

### 3i.1 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean, no new warnings.
- `cmake --build build-mac --parallel 4` — clean, no new warnings (only the two pre-existing
  unrelated warnings: `EmuInstance.cpp` `sprintf` deprecation, HUD `EditPropType::Color` switch).
- Runtime smoke, Metal-enabled binary with
  `MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive 5s
  on Intel Iris Plus 655 and logged the strengthened probe succeeding plus presenter init/first
  draw/first UI overlay, confirming the fixes did not regress the existing forced-Metal path:

```text
[MelonPrime] metal probe: device='Intel(R) Iris(TM) Plus Graphics 655' lowPower=1 removable=0 unifiedMemory=1 recommendedMaxWorkingSetSize=1610612736
[MelonPrime] metal presenter: initialized drawable=512x768 scale=2.00
[MelonPrime] metal presenter: first draw drawable=512x768 scale=2.00 active=0
[MelonPrime] metal presenter: first UI overlay 256x384 active=0
```

- Runtime smoke, default (Metal-disabled) binary: launched and quit cleanly; log shows the normal
  OpenGL context path (`Created a OpenGL context`, `GL_RENDERER: Intel(R) Iris(TM) Plus Graphics
  655`), confirming the `EmuThread` routing fix did not change default-build behavior.
- Default binary string check: still no `metal presenter`, `metal probe`, `metal renderer`,
  `MelonPrimeScreenMetal`, or `MELONPRIME_FORCE_METAL` strings.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files),
  `audit-metroid-literal-budget.ps1 -Budget 1`, `audit-platform-scatter-budget.ps1 -Budget 22`
  (unaffected — these fixes use `MELONPRIME_DS`, not `__APPLE__`/`__linux__` markers),
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1` — all pass, same as
  before this fix set. `generate-hud-prop-schema.py` regeneration diff is empty.
- **Not verified:** Apple Silicon. **Not verified:** ROM gameplay (still no ROM available in this
  session — confirmed again this session via `find` for `*.nds`, none found).

## 3j. Phase 8 continuation — real opaque-polygon vertex upload + rasterization, hardware-verified independent of a ROM

Design doc S14's suggested port order lists ten sub-steps for the regular (non-compute) 3D
renderer; step 1 (clear pass) landed in §3g/3h. This section covers steps 2-3
("vertex/index upload" then "opaque polygons"): `MetalRenderer3D` now builds real vertex and
index buffers from `GPU3D::RenderPolygonRAM` every frame and issues genuine `drawIndexedPrimitives`
calls into `ColorTarget`/`AttrTarget`/`DepthStencilTarget`, for opaque, untextured (vertex-color
only), non-shadow, non-line polygons.

**Scope, stated precisely (see the itemized comment above `RenderNativeOpaquePolygons()` in
`GPU3D_Metal.mm` for the canonical list):** implemented -- packed vertex-word layout and fan
triangulation matching `GLRenderer3D::SetupVertex()`/`BuildPolygons()` exactly
(`GPU3D_OpenGL.cpp`), two full pipeline variants (Z-buffer via rasterizer depth, W-buffer via
explicit `[[depth(any)]]` fragment output, replicating `3DRenderVS.glsl`/`3DRenderFS.glsl`'s
depth math translated for Metal's `[0,1]` NDC-z convention instead of GL's `[-1,1]` + separate
depth-range remap), opaque-alpha discard threshold. **Not implemented:** texturing (the texture
cache, `GPU3D_TexcacheOpenGL.h`, is a large separate subsystem -- every polygon here renders with
vertex color only, visibly wrong for most real DS 3D content), translucent polygons, shadow
masks/shadows, line-type polygons, the depth-func-equal attribute bit, `BetterPolygons`
triangulation, hi-res scale factor (always native 256x192), edge marking, fog, the final composite
pass, and -- critically -- feeding this output to `GetLine()`/the presenter at all. This is a real,
GPU-executed "shadow" pass that cannot regress what a user sees, because nothing downstream reads
its output yet.

### 3j.1 Hardware verification independent of the ROM constraint

Every prior Phase 8 commit could only state "not runtime-exercised without a ROM" for its 3D
renderer code, because `MetalRenderer3D::Init()` is reachable only through
`EmuThread::updateRenderer()`, which only runs once `emuStatus == emuStatus_Running`, which
requires a ROM. That constraint still holds for the *integrated* code path. But the embedded MSL
shader source itself (`kMetal3DOpaqueShaderSource`) is a self-contained unit that can be compiled
and exercised independent of the full app, so this session built a standalone Objective-C++
harness (`clang++ -framework Metal -framework Foundation`, not part of the repo, scratchpad-only)
that:

1. Extracts the exact shader source string from `GPU3D_Metal.mm` and compiles it via
   `newLibraryWithSource:` on this session's real Intel Iris Plus 655 -- confirming actual MSL
   syntax correctness, not just "the surrounding Objective-C++ builds."
2. Resolves all 4 entry points (`mp3d_opaque_vs_z/_w`, `mp3d_opaque_fs_z/_w`) and creates both
   pipeline states against the exact attachment shape used in production
   (`BGRA8Unorm`/`RGBA8Unorm`/`Depth32Float_Stencil8`).
3. A separate compute-shader harness ran the position/depth math standalone and dumped the
   resulting clip-space values for hand-verification against the formula derivation (confirmed
   exact match, e.g. vertex at screen `(0,0)` with near-1.0 W produced clip-space `(-0.99998,
   -0.99998, 0.0, 0.99998)` as expected).
4. A full render-pass test uploaded synthetic vertex/index buffers using the identical packed
   7-word-per-vertex layout `RenderNativeOpaquePolygons()` produces, drew a triangle through
   **both** the Z-buffer and W-buffer pipeline variants, and read back individual pixels via
   `getBytes:fromRegion:` to confirm the triangle rasterized in the geometrically correct location
   (inside-triangle sample point came back the expected solid color; outside-triangle sample point
   stayed the clear color) for both depth-path variants independently.
5. A fourth run with alpha deliberately set below the opaque threshold (10/31 instead of 31/31)
   confirmed `discard_fragment()` fires correctly (the pixel that would otherwise be inside the
   triangle stayed the clear color).

One real debugging detour during this: the first few draw-based checks appeared to fail (sample
point stayed clear-colored) even after independently confirming the vertex math was correct via
the compute-shader dump. Root cause was the test harness's own sample-point choice, not the
shader: the synthetic triangle's hypotenuse passes exactly through the diagonal `x==y`, and the
originally-chosen sample point `(10,10)` sat exactly on that boundary (a test-harness bug, not a
production one). Re-picking a sample point unambiguously inside the triangle's interior resolved
it and confirmed correct rasterization on the first correctly-posed attempt for both depth-path
variants. This is called out explicitly because it is exactly the kind of self-inflicted false
negative that would be easy to misreport as "the shader is broken" without isolating the actual
variable -- the minimal full-screen-triangle control case (matching the existing Phase 4
CAMetalLayer presenter's own clear-pass shader shape) passed on the very first try throughout,
which is what motivated bisecting the difference down to the sample-point geometry rather than
the shader logic.

This is meaningfully stronger verification than "compiles and doesn't crash" for a program in this
Metal-plan document, though it is still **not** the same as running the integrated code path
against a real ROM -- it validates the shader logic and packing format in isolation, not
`RenderNativeOpaquePolygons()`'s CPU-side polygon-list traversal against actual `GPU3D` state, nor
anything about how this interacts with the rest of `MetalRenderer3D`'s lifecycle (`Reset()`,
`SetScaleFactor()`, savestate hooks) under real emulation.

### 3j.2 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean, no new warnings.
- `cmake --build build-mac --parallel 4` — clean, no new warnings (default tree unaffected, as
  `GPU3D_Metal.mm` is not part of it).
- Standalone Metal harness (see §3j.1): shader compiles, both pipelines create, position math
  matches hand-derivation exactly, both Z-buffer and W-buffer draws rasterize correctly, opaque-
  alpha discard fires correctly. Run on this session's real Intel Iris Plus 655 (Metal 3).
- Runtime smoke, Metal-enabled binary with
  `MELONPRIME_FORCE_METAL_RENDERER=1 MELONPRIME_FORCE_METAL_PRESENTER=1`: process stayed alive 5s,
  logged probe/presenter/first-draw/first-UI-overlay exactly as before (no-ROM startup still never
  reaches `MetalRenderer3D::Init()`, so this smoke confirms no regression to the presenter path,
  not the new opaque-polygon code).
- Runtime smoke, default binary: launched and quit cleanly, normal OpenGL context path unchanged.
- Default binary string check: still zero Metal strings (`metal renderer3d`, `opaque pipeline`,
  `opaque_vs`/`opaque_fs`, `MetalRenderer3D` all absent); Metal-enabled binary confirmed to contain
  all 4 new shader entry-point names and both pipeline labels.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files),
  `audit-metroid-literal-budget.ps1 -Budget 1`, `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1` — all pass, unaffected
  (this code is pure core-library C++/Objective-C++, no `qt_sdl` platform markers touched).
  `generate-hud-prop-schema.py` regeneration diff is empty.
- **Not verified:** Apple Silicon. **Not verified:** the integrated code path
  (`RenderNativeOpaquePolygons()` traversing real `GPU3D::RenderPolygonRAM` state from an actual
  running ROM) — only the shader/pipeline logic in isolation, per §3j.1. **Not implemented:**
  everything listed in the "Not implemented" paragraph above (texturing chief among them) --
  Phase 8 remains genuinely multi-session work; this is one honest increment along design doc
  S14's suggested order, not a claim of parity with `GLRenderer3D`.

## 3k. Phase 8 continuation — texturing via the shared Texcache<> template

§3j deliberately deferred texturing as "the largest remaining piece," on the assumption (matching
the design doc's own framing) that a DS texture-format decoder is a large separate subsystem. On
closer reading of `GPU3D_TexcacheOpenGL.h` (29 lines) that assumption turned out to be wrong in a
way that made this session's texturing increment much more tractable than expected: **all** DS
texture format decoding (direct color, 4x4-compressed, A3I5/A5I3, 2/4/8-bit paletted) lives in the
backend-agnostic `Texcache<TexLoaderT, TexHandleT>` template (`GPU3D_Texcache.h`/`.cpp`, already
unconditionally compiled into `core` regardless of `OGLRENDERER_ENABLED`). The GL backend's own
loader (`TexcacheOpenGLLoader`) is only 3 methods -- `GenerateTexture`/`UploadTexture`/
`DeleteTexture` -- because the template does 100% of the hard work and hands the loader
already-decoded RGB6A5-packed-as-4-raw-bytes texel data. This meant Metal texturing did not need a
from-scratch DS format decoder at all: a `TexcacheMetalLoader` with the same 3-method shape, backed
by `id<MTLTexture>` (`MTLTextureType2DArray`, `MTLPixelFormatRGBA8Uint`) instead of `GLuint`, reuses
the entire existing decode pipeline by instantiating `Texcache<TexcacheMetalLoader, id<MTLTexture>>`
directly.

**What changed:**

- `TexcacheMetalLoader` (new, in `GPU3D_Metal.mm`): `GenerateTexture` allocates an `MTLTextureType2DArray`
  in `MTLPixelFormatRGBA8Uint`/`MTLStorageModeShared`; `UploadTexture` calls `replaceRegion:...slice:...`
  per layer; `DeleteTexture` is a no-op (ARC releases the strong `id<MTLTexture>` refs the `Texcache<>`
  template's own `TexArrays`/`FreeTextures` vectors hold when cleared/erased -- there is no explicit
  destroy call in the Metal API, unlike `glDeleteTextures`).
- `MetalState` gained a `std::unique_ptr<TexcacheMetal>` (constructed once `Device` exists, in
  `CreateDeviceObjects()`, via `std::make_unique<TexcacheMetal>(GPU, TexcacheMetalLoader(State->Device))`),
  an `OpaqueTextureSampler` (nearest, clamp-to-edge -- see the wrap-mode gap below), and a 1x1
  `DummyTexture` (Metal requires a texture argument to be bound at draw time even when the shader's
  runtime branch never samples it, so untextured draws still need something valid in that slot).
  `Reset()` now also calls `Texcache->Reset()`.
- The opaque vertex/fragment shaders (`kMetal3DOpaqueShaderSource`) gained real `texcoord`
  (sign-extended 4-bit-fixed DS units, normalized by texture width/height exactly like
  `3DRenderVS.glsl`), `texLayer` (flat, `0xFFFF` sentinel = no texture), and `blendMode` (flat,
  polygon attribute bits 4-5) varyings, plus a shared `OpaqueFinalColor()` helper mirroring
  `3DRenderFS.glsl::FinalColor()`'s modulate/decal branch. Every DS texture format arrives at the
  shader as the same `texture2d_array<uint>` regardless of source format, since `Texcache<>` already
  normalized it -- there is no per-format branching in the shader, matching GL.
- `RenderNativeOpaquePolygons()` now calls `Texcache->Update()` once per frame (VRAM-dirty
  invalidation, same entry point `GLRenderer3D::RenderFrame()` uses), resolves each textured
  polygon's texture+layer via `Texcache->GetTexture(poly->TexParam, poly->TexPalette, ...)` (the
  same call `GLRenderer3D::BuildPolygons()` makes), and groups polygons by `(WBuffer, texture
  identity)` -- an unordered accumulation rather than GL's adjacency-based `RenderPolygonBatch()`,
  since `RenderPolygonRAM` order is not guaranteed to already cluster same-texture polygons, and an
  unordered grouping is simpler to get right than replicating GL's contiguous-run scan. This mirrors
  `SetupPolygon()`'s `RenderKey`, which GL's own `RenderPolygonBatch()` additionally gates on
  `TexID` equality (confirmed by reading `GPU3D_OpenGL.cpp:835`) -- i.e. this is the same grouping
  granularity GL itself uses, just computed differently.

**Explicitly not implemented (see the scope comment above `kMetal3DOpaqueShaderSource` and above
`RenderNativeOpaquePolygons()` in `GPU3D_Metal.mm` for the canonical, most-detailed list):**
toon/highlight color substitution for blendmode 2 (needs the per-frame toon-color-table/`DispCnt`
uniforms, not yet plumbed to this pass -- a blendmode-2 polygon still selects the geometrically
correct modulate/decal branch, just with its raw, non-toon-substituted vertex color, which is wrong
specifically for toon-shaded/cel-shaded content); `TexRepeat` wrap/mirror modes (every sample uses
clamp-to-edge regardless of the polygon's repeat/mirror flags, confirmed by reading
`GLRenderer3D::SetupPolygonTexture()`'s `GL_REPEAT`/`GL_MIRRORED_REPEAT`/`GL_CLAMP_TO_EDGE`
derivation from the 4-bit `TexRepeat` value -- wrong for textures that rely on tiling/scrolling, fine
for the common non-tiling case); VRAM-display-capture-as-texture (a niche feature where a
direct-color texture happens to alias a live capture region -- `GLRenderer3D::BuildPolygons()` has a
`capblock`/`captureinfo` special case for this that samples the frame being captured directly rather
than re-decoding VRAM; this pass has no equivalent, so it will simply decode whatever is currently
in VRAM through the generic path instead).

### 3k.1 Hardware verification independent of the ROM constraint (continuing §3j.1's approach)

The same standalone-harness technique from §3j.1 was extended to the texturing path specifically,
since (as established there) no ROM is available to exercise `RenderNativeOpaquePolygons()`'s real
per-polygon texture resolution through the actual frame loop:

1. Re-extracted the current (texturing-inclusive) shader source and confirmed it still compiles via
   `newLibraryWithSource:` and both pipeline variants still create with the extended vertex-output
   struct (adds `texcoord`/`texLayer`/`blendMode`).
2. Built a synthetic 2x2 `RGBA8Uint` texture array (1 layer) with a known texel value
   (R=32/63, G=63/63, B=0/63, A=31/31, i.e. deliberately not white/black so a math error would be
   visible), sampled it through the exact `mp3d_opaque_vs_z`/`mp3d_opaque_fs_z` pair with a nearest,
   clamp-to-edge sampler bound at index 0, and confirmed the rendered pixel exactly matched the hand-
   computed modulate-blend expectation (`vcol(white) * tcol` where `tcol = texel/(63,63,63,31)`):
   rendered `BGRA = (0, 255, 130, 255)` against a computed expectation of `(0, 255, ~129, 255)`.
3. Re-ran with `blendMode` bits set to decal (1) and a half-alpha texel (16/31) plus a distinct
   (red) vertex color specifically chosen so modulate and decal diverge, confirming the decal
   formula `tcol.rgb*tcol.a + vcol.rgb*(1-tcol.a)` matched exactly: rendered `BGRA = (0, 132, 190,
   255)` against a computed expectation of the same values.
4. Re-ran with `texLayer` set to the `0xFFFF` "no texture" sentinel (with a real texture still
   bound in the argument table, matching what `RenderNativeOpaquePolygons()` does for the "no
   texture" group via `DummyTexture`) and confirmed the output was exactly the untouched white
   vertex color, proving the sentinel branch correctly ignores whatever is bound when a polygon
   has no texture.

All three cases passed on the first attempt once written correctly (no repeat of §3j.1's sample-
point debugging detour), because each test's expected value was computed independently by hand
*before* running, from the same formulas documented in the shader's own scope comment, rather than
inferred after the fact from whatever the shader happened to produce.

### 3k.2 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean, no new warnings, including
  instantiating `Texcache<TexcacheMetalLoader, id<MTLTexture>>` (an ARC-managed object pointer as an
  STL/`unordered_map` template parameter) without issue.
- `cmake --build build-mac --parallel 4` — clean, no new warnings, default tree unaffected
  (`GPU3D_Texcache.h`/`.cpp` were already compiled into `core` unconditionally before this change;
  only `GPU3D_Metal.mm`, which is not part of the default tree, now includes it).
- Standalone Metal harness (§3k.1): shader compiles with texturing, modulate blend verified exact,
  decal blend verified exact, untextured-sentinel-with-texture-bound verified exact. All on this
  session's real Intel Iris Plus 655.
- Runtime smoke, Metal-enabled and default binaries: both stayed alive / launched-and-quit cleanly,
  logs unchanged from §3j.2 (no-ROM startup still never reaches the new texturing code path).
- Default binary string check: still zero Metal strings (`TexcacheMetal`, `mp3d_opaque`,
  `MetalRenderer3D` all absent); Metal-enabled binary confirmed to contain `TexcacheMetalLoader`
  and `OpaqueFinalColor` symbols.
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files),
  `audit-metroid-literal-budget.ps1 -Budget 1`, `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1` — all pass, unaffected.
  `generate-hud-prop-schema.py` regeneration diff is empty.
- **Not verified:** Apple Silicon. **Not verified:** the integrated code path against a real ROM
  (only the shader/pipeline/blend logic in isolation, per §3k.1). **Not implemented:** everything
  in the "Explicitly not implemented" list above, plus everything §3j already deferred
  (translucent polygons, shadow masks, line polygons, depth-func-equal, `BetterPolygons`, hi-res
  scale, edge marking, fog, final composite, `GetLine()`/display integration). Phase 8 remains
  genuinely multi-session work.

## 3l. Phase 8 continuation — feature probe strengthened for texturing

Following [plan/metal_phase8_execution_instructions.md](plan/metal_phase8_execution_instructions.md)
Priority 1: the Phase 2 feature probe (`MelonPrimeMetalFeatureCheck.mm`) previously only validated
the clear-pipeline attachment shape (§3i.4), which never exercised `MTLTextureType2DArray` +
`MTLPixelFormatRGBA8Uint` allocation or `texture2d_array<uint>` sampling -- exactly what
`TexcacheMetalLoader` and the opaque textured pass (§3k) actually depend on. The probe now allocates
a 2x2 array texture, uploads a known non-white/non-black RGB6A5 texel pattern, samples it in a real
draw through a nearest/clamp sampler, and verifies the read-back pixel matches -- a full round trip,
not just pipeline descriptor validation, added as `FeatureInfo::supportsTextureArraySampling`
(folded into `supportsRequiredBaseline`).

**Verified (2026-07-09, real Intel Mac):** both trees build clean; forced-Metal no-ROM smoke now
logs `textureArraySampling=1` on this session's Intel Iris Plus 655; default binary has zero new
strings; all audits green. **Not applicable:** ROM/Apple Silicon (probe-only change, no renderer
behavior touched).

## 3m. Phase 8 continuation — first integrated ROM verification (Priority 2 closed)

Every verification claim before this point in the plan (§3d-§3l) was explicit that no ROM was
available in this session, so the integrated code path (a real ROM driving `EmuThread` ->
`updateRenderer()` -> `GPU3D::RenderFrame()` -> `MetalRenderer3D::RenderFrame()` ->
`RenderNativeOpaquePolygons()` -> `Texcache<>::GetTexture()`/`TexcacheMetalLoader`) had never
actually run -- only the shader/pipeline logic in isolation, via standalone harnesses. Real MPH ROM
files became available partway through this session
(`/Users/admin/Downloads/_Documents/Metroid Prime - Hunters (Japan).nds` and others), closing
[plan/metal_phase8_execution_instructions.md](plan/metal_phase8_execution_instructions.md)'s
Priority 2 gate.

**One-shot integration-proof logging added** (`GPU3D_Metal.mm`, `MetalRenderer::Init()` and
`MetalRenderer3D::Init()` already had logs from earlier phases):

- `MetalRenderer3D::RenderFrame()`: `first integrated RenderFrame`.
- `RenderNativeOpaquePolygons()`: `first opaque pass polygons=<n> considered=<n> textured=<n>
  groups=<n>` (fires on the very first call, whatever it contains), plus a second one-shot
  `first non-empty opaque pass ...` that only fires once `considered>0`, since the very first
  `RenderFrame()` call is typically a firmware boot/logo screen built from translucent fade
  polygons this pass filters out entirely -- `considered=0` there is expected, not a bug, and a
  single log line could not distinguish "filtered as designed" from "nothing is working".
- `TexcacheMetalLoader::GenerateTexture()`/`UploadTexture()`: one-shot logs with real
  width/height/layer counts.

**Result, launching the Metal-forced test build with the real ROM** (`MELONPRIME_FORCE_METAL_RENDERER=1
MELONPRIME_FORCE_METAL_PRESENTER=1`, `--` positional arg = the `.nds` path, auto-boots per
`CLI.cpp`'s `--boot auto` default):

```text
[MelonPrime] metal renderer3D: native Metal device/queue/targets initialized; software raster delegate still active
[MelonPrime] metal renderer3D: first integrated RenderFrame
... (firmware boot: cart insert, secure area decrypt, "Game is now booting") ...
[MelonPrime] metal renderer3D: first opaque pass polygons=165 considered=0 textured=0 groups=0
[MelonPrime] metal texcache: first texture array allocation width=256 height=64 layers=64
[MelonPrime] metal texcache: first texture upload width=256 height=64 layer=63
```

A second run (same ROM, longer soak) reached a frame with real opaque content:

```text
[MelonPrime] metal renderer3D: first non-empty opaque pass polygons=88 considered=6 textured=0 groups=1
```

Both runs progressed well past firmware boot into the game's own boot sequence (secure-area
decryption, `Game is now booting`, WIFI hardware init sequence, the game's `.ml1` license-data file
opened, and a clean `SaveManager: Wrote 262144 bytes` flush + graceful process exit on quit) --
i.e. this is not a splash-screen-only test, the DS is actually executing the cartridge's own code
with the native Metal pass running every single frame alongside it. Process ran for 70s and 113s in
the two runs respectively (`ps` confirmed steady CPU usage throughout, no crash, no Objective-C
exception, no `SIGABRT`/`SIGSEGV` in the log) before being sent `SIGTERM` and exiting cleanly.

**What this confirms:** the entire pipeline this session built --
`TexcacheMetalLoader`/`Texcache<>::GetTexture()`/`RenderNativeOpaquePolygons()`'s polygon
traversal, filtering, and per-(WBuffer,texture) grouping -- runs correctly against real
`GPU3D::RenderPolygonRAM`/VRAM state from an actual booting ROM, continuously, without crashing.
The `256x64/64 layers` texture-array allocation is a real, sensible size (matches the
`Texcache<>` template's own layer-budget formula, `min(8MB/(width*height*4), 64)` = exactly 64 for
256x64), not a placeholder or degenerate value.

**What this does NOT confirm** (per the execution instructions' own distinction between
standalone-harness and integrated-ROM verification, §4.3): visual parity (output is still not wired
to `GetLine()`/display, so nothing about this run's on-screen pixels were Metal-rendered -- the
window showed the normal OpenGL/Software path throughout, per Priority 3's explicit "do not connect
final display output yet" instruction, which was already true before this and remains unchanged);
whether the frame with `textured=0` reflects that specific scene genuinely having no textured
opaque polygons, or a smaller subset than a full title/gameplay scene would have (both samples came
from early boot, not a stable title-screen or gameplay frame); Apple Silicon (still unavailable in
this session); and correctness of texture *content* against real VRAM data (the earlier standalone
harnesses proved the shader math is correct for synthetic texel data -- this run proves the same
code path doesn't crash against real VRAM, but did not independently verify the decoded texel
*values* are correct, since that would require reading GPU-side memory back out and comparing
against an independent reference decoder, which is future work, likely only tractable once output
is wired to display and can be visually compared against the Software renderer for the same frame).

### 3m.1 Verification (2026-07-09, real Intel Mac, real ROM)

- `cmake --build build-mac-metal-test --parallel 4` / `cmake --build build-mac --parallel 4` — both
  clean, no new warnings.
- Integrated ROM run (see above): two independent launches, 70s and 113s respectively, both
  progressed through firmware boot into the game's own boot sequence, both produced the expected
  one-shot log sequence, neither crashed, both exited cleanly via `SIGTERM` with a clean save
  flush.
- Default binary string check: still zero new strings (`first integrated RenderFrame`, `first
  opaque pass`, `first non-empty opaque pass`, `first texture array allocation`, `first texture
  upload` all absent).
- Audits: `audit-config-defaults.ps1`, `check-inc-ownership.ps1` (56 files),
  `audit-metroid-literal-budget.ps1 -Budget 1`, `audit-platform-scatter-budget.ps1 -Budget 22`,
  `audit-color-dialog-prefs.ps1`, `audit-melonprime-srp-performance.ps1` — all pass.
- **Not verified:** Apple Silicon; visual/pixel-level output parity (not wired to display yet);
  texture content correctness against real VRAM (only crash-safety and dimension sanity, not texel
  values). **Not implemented:** everything already listed in §3j/§3k/§4's remaining-work lists --
  this closes an integration-verification gate, not a feature gap.

## 3n. Phase 8 continuation — texture wrap/mirror/clamp (Priority 4 closed)

Following [plan/metal_phase8_execution_instructions.md](plan/metal_phase8_execution_instructions.md)
Priority 4: §3k's textured pass always sampled with clamp-to-edge, explicitly documented as a known
gap since real DS content that tiles/scrolls a texture (the DS `TexRepeat` 4-bit attribute field:
repeat-S, repeat-T, mirror-S, mirror-T) would render visibly wrong. `BuildOpaqueRenderPipelines()`
now builds a 3x3 matrix of `MTLSamplerState` (`OpaqueTextureSamplers[sIdx][tIdx]`), one for every
independent S/T combination of `{clamp, repeat, mirror-repeat}`, via a new
`TexRepeatAddressModeIndex()` helper that maps each axis of the raw `TexRepeat` bits to the correct
mode -- matching `GLRenderer3D::SetupPolygonTexture()`'s `GL_REPEAT`/`GL_MIRRORED_REPEAT`/
`GL_CLAMP_TO_EDGE` derivation exactly (`GPU3D_OpenGL.cpp`).

Since different polygons can have different wrap modes for the same texture, `OpaqueDrawGroupKey`
gained a `TexRepeat` field (matching `GLRenderer3D::RenderPolygonBatch()`'s own `TexRepeat`-gated
grouping, confirmed at `GPU3D_OpenGL.cpp:836`), so polygons with different wrap modes no longer
collapse into the same draw-call group; each group now selects and binds the matching sampler at
draw time instead of one sampler being bound once for the whole encoder. Untextured groups are
normalized to `TexRepeat=0` in the key regardless of a polygon's raw bits, since no sampling
happens for them and letting the wrap mode vary there would only fragment the untextured group
into identical duplicate draw calls for no reason.

**Verified (2026-07-09, real Intel Mac, including a real-ROM re-run):** both trees build clean; a
third launch of the same MPH ROM against the Metal-forced test build (29s, steady 35.6% CPU) still
reached the same `first opaque pass`/`first non-empty opaque pass`/texture-array-allocation log
sequence as §3m with no crash, confirming the sampler/grouping change didn't regress the integrated
path; default binary has zero new strings (`TexRepeatAddressModeIndex`/`OpaqueTextureSamplers`
absent); all audits green. **Not verified:** Apple Silicon; whether the resulting wrap behavior is
visually correct against real tiling/scrolling texture content (still not wired to display, and
none of this session's ROM runs happened to reach a scene using non-clamp wrap modes based on the
logged data, though the code path is now present and exercised for grouping purposes on every
frame with any textured polygon).

---

## 3n.1 Phase 8 continuation — toon/highlight shader parity slice

As part of
[plan/metal_flicker_black3d_full_fix_phased_plan.md](plan/metal_flicker_black3d_full_fix_phased_plan.md)
Phase 3-5, the Metal 3D shader now receives the per-frame `RenderDispCnt` and `RenderToonTable`
data that `GLRenderer3D` sends through `uConfig`. `blendmode==2` no longer falls through as plain
modulate: toon mode indexes the table with `int(vcol.r * 31)` and replaces vertex RGB, while
highlight mode first folds vertex RGB to the red component and then adds the same toon-table color
after texture/no-texture blending, clamped to 1.0. This follows `OpenGL_shaders/3DRenderFS.glsl`'s
control flow and keeps the existing Metal modulate/decal texture path intact.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
toon/highlight scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.2 Phase 8 continuation — clear/opaque attr target parity foundation

The native Metal pass now starts populating the secondary attr render target with the same fields
the GL post-processing shaders consume. Plain clears no longer reset attr to zero; they clear red to
`RenderClearAttr1`'s polyID, blue to the clear fog flag, and alpha to 1, matching `3DClearFS.glsl`
for non-bitmap clears. Opaque polygon fragments now write `polyID`, edge marker placeholder 0, fog
flag, and alpha 1, matching the opaque branch of `3DRenderFS.glsl`.

This is intentionally a foundation slice, not the complete fog/edge feature: translucent attr
color-mask behavior, clear bitmap attr/depth texture sampling, edge line marking, and the final
fog/edge full-screen passes remain open. Keeping those separate avoids mixing post-processing
pipeline work into the attr data-format correction.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff,
because no `.nds` ROM is present in this workspace.

---

## 3n.3 Phase 8 continuation — clear bitmap plane support

Metal clear handling now covers `RenderDispCnt` bit14 instead of treating every frame as a plain
clear plane. The shared Texcache dirty check is performed before `ClearNativeTarget()` so the same
VRAM texture-slot dirty bits GL uses for clear bitmap uploads are available to Metal. Dirty slot 2
is expanded into a `RGBA8Uint` 256x256 color texture with 6-bit RGB and 5-bit alpha; dirty slot 3
is expanded into a `R32Uint` 256x256 depth/fog texture with the 24-bit DS clear depth and fog flag.

When clear bitmap mode is active, `ClearNativeTarget()` first performs the regular render-pass load
clear, then draws a fullscreen triangle through the clear pipeline. The shader repeats the 256x256
bitmap using the DS clear-bitmap X/Y offset from `RenderClearAttr2` and writes color, attr
polyID/fog, and depth, matching `3DClearBitmapVS/FS.glsl` for the currently native 256x192 target.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
clear-bitmap scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.4 Phase 8 continuation — fog final post-pass

Metal now has the first GL-style final post-processing pass: fog. `DepthStencilTarget` is created
with shader-read usage, and after native polygon rendering the renderer runs a fullscreen pass when
`RenderDispCnt` bit7 is set. The fog shader samples the native depth target and attr target,
ignores pixels whose attr fog flag is clear, computes density from `RenderFogOffset`,
`RenderFogShift`, and the 34-entry `RenderFogDensityTable`, then outputs density into a blended
BGRA8 pass over `ColorTarget`.

Two Metal pipelines mirror GL's two fog blend modes: normal color fog blends `RenderFogColor` into
RGB and alpha, while `RenderDispCnt` bit6 selects alpha-only fog that leaves RGB untouched and
blends only alpha. This pass runs before the 256x192 readback used by `GetLine()`, so the existing
soft 2D compositor consumes the fogged native 3D line data.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
fogged scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.5 Phase 8 continuation — edge final post-pass

Metal now runs the edge-marking half of Phase 3-6. Opaque fragments set the attr target's edge
marker channel when `RenderDispCnt` bit5 is enabled. The final post-pass then draws an edge shader
before fog, samples the native depth and attr targets, compares the four direct neighbors'
polyID/depth values, and blends `RenderEdgeTable[polyID >> 3]` over the native color target when a
visible boundary is found. The edge alpha follows GL's current rule: 0.5 when antialiasing
(`RenderDispCnt` bit4) is enabled, otherwise 1.0.

This is intentionally still below a ROM-verified "edge parity" claim: the shader mirrors the
existing GL final-pass structure, but the exact DS/soft edge-coverage behavior and translucent attr
color-mask preservation still need A/B visual comparison once a ROM is available.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
edge-marked scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.6 Phase 8 continuation — line polygon draw support

Metal now renders DS line polygons instead of dropping them at the native pass filter. The CPU
upload still packs the polygon's vertex data through the same path as triangle polygons, but
`Polygon::Type==1` creates a draw-group key that is distinct from triangles and emits only the first
two non-duplicate final-position vertices as indices, matching `GLRenderer3D::BuildPolygons()`.
Line groups submit with `MTLPrimitiveTypeLine`, reusing the same texture/depth/stencil and
toon/fog/edge-capable shader outputs as triangle groups.

This is a mechanical parity slice; exact line rasterization differences between GL, Metal, and DS
hardware still need ROM A/B visual review.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
line-polygon scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.7 Phase 8 continuation — shadow mask stencil foundation

Metal now lets DS shadow-mask polygons reach the native pass instead of filtering them out with
actual shadow polygons. Shadow masks use dedicated stencil-only Z/W render pipelines: they bind the
same vertex path as normal opaque polygons, disable color writes to both native color targets, keep
depth read-only, and set stencil bit 7 only when the mask fragment fails the depth test. This
matches the first half of GL's shadow-mask behavior and preserves the lower stencil bits already
used for polyID and translucent-ID suppression.

Before submitting any shadow-mask group, Metal draws a fullscreen stencil-only clear that replaces
only bit 7 with zero. Actual `IsShadow` polygons remain skipped in this slice, so this is a
foundation for the later visible shadow pass rather than complete shadow parity.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
shadow scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.8 Phase 8 continuation — visible shadow polygon pass

Metal now includes actual `IsShadow` polygons in the native pass instead of skipping them. Shadow
polygons are forced into one draw group per polygon, matching GL's `RenderSinglePolygon()` behavior
for the shadow branch so each polygon can update stencil before its own visible draw.

The implementation mirrors GL's two-step pass for the normal translucent-shadow path. First, a
stencil-only shadow prepass runs the same texture sampling and alpha discard as the regular fragment
shader, keeps depth read-only, and clears stencil bit 7 only where the lower stencil polyID matches
the shadow polygon. Second, the visible blended draw runs where bit 7 remains set and replaces the
lower stencil bits with `0x40|polyID`, preserving the existing same-ID translucent suppression model.
The shadow bit clear now runs whenever any shadow-related group is present, so the clear-plane's
initial `0xFF` stencil value cannot accidentally make a shadow draw without a preceding mask.

This is still a parity slice rather than a visual-completion claim: the special clear-alpha-zero
background path, exact ROM A/B parity, and translucent attr color-mask behavior still need follow-up
work.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff for
shadow scenes, because no `.nds` ROM is present in this workspace.

---

## 3n.9 Phase 8 continuation — translucent attr color-mask parity

Metal translucent draws now preserve the attr target unless GL would have allowed the translucent
fragment to affect fog participation. The regular translucent Z/W pipelines still blend color into
the native color target, but their attr-target write mask is disabled. When fog is enabled and the
polygon does not opt out via attr bit 15, Metal selects dedicated translucent fog-attr pipelines
whose attr-target write mask is blue-only, so the existing attr.r/attr.g/attr.a values survive and
only the fog flag is cleared, matching `3DRenderFS.glsl` plus
`glColorMaski(1, GL_FALSE, GL_FALSE, transfog, GL_FALSE)`.

The same selection is used by the visible shadow draw. This closes the previously documented
"translucent attr output placeholder" for the native Metal pass; exact visual parity still needs ROM
A/B comparison, and VRAM display-capture textures plus `BetterPolygons` remain open Phase 3 work.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff,
because no `.nds` ROM is present in this workspace.

---

## 3n.10 Phase 8 continuation — VRAM display-capture texture path

Metal now explicitly recognizes DS display-capture-backed direct-color textures during native
polygon setup. The GL renderer has to redirect these textures to `CaptureOutput128Tex` /
`CaptureOutput256Tex` because the GL renderer may still own unsynced GPU capture output. The Metal
renderer in this branch is different: it derives from `SoftRenderer`, and `SoftRenderer::DoCapture()`
writes capture scanlines directly into emulated VRAM during the normal soft 2D composition step.
After `Texcache<>::Update()` makes `VRAMFlat_Texture` coherent, the existing shared Texcache decode
path is therefore the correct source for capture-backed direct-color textures.

The native pass now calls `GPU.GetCaptureInfo_Texture()` and uses the same address-range test as GL
to identify capture-backed textures, while still submitting them through Texcache. The one-shot
visible-pass diagnostics include a `captureTextured` count so a later ROM run can prove that the
path is exercised.

This closes the Metal/GetLine-stage VRAM display-capture-as-texture item. A future full
`GLRenderer`-mirror Phase 4 may still need GPU-native capture-output arrays for a fully GPU 2D/3D
pipeline.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM runtime log showing
`captureTextured > 0`, because no `.nds` ROM is present in this workspace.

---

## 3n.11 Phase 8 continuation — BetterPolygons center-fan splitting

Metal now honors the `BetterPolygons` renderer setting instead of always using the regular
`!BetterPolygons` fan split. `MetalRenderer::SetRenderSettings()` forwards the setting into
`MetalRenderer3D`, which stores it alongside the scale factor.

For polygons with more than three vertices, the native upload path mirrors GL's BetterPolygons
algorithm: it computes the same center vertex from `HiresPosition`, W-weighted Z/color/texcoord
interpolation, emits that center vertex before the original vertices, then builds the same center-fan
index order plus the closing triangle back to vertex 1. Lines and triangles keep their existing GL
paths, and the regular fan split remains active when BetterPolygons is disabled.

This closes the final explicitly listed Phase 3-7 implementation item for the current Metal/GetLine
native 3D path. ROM A/B visual parity is still pending, and Phase 4's full hires GLRenderer mirror
remains separate work.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** ROM A/B visual diff with
BetterPolygons enabled, because no `.nds` ROM is present in this workspace.

---

## 3n.12 Phase 4 start — non-visible MetalRenderer2D output scaffold

Phase 4 has started with the smallest safe `GLRenderer2D` mirror foundation: a new
`GPU2D_Metal.{h,mm}` owns Engine A/B `MetalRenderer2D` mirror objects and allocates one
scale-aware BGRA8 render-target/shader-read output texture per engine. `MetalRenderer` configures
those mirrors from the native 3D Metal device and current `ScaleFactor`, so later BG text, affine,
OBJ, window, blend, and compositor passes have a stable owner and target to grow into.

The next scaffold slice added GL-style OBJ render targets to each mirror: a 2-layer RGBA8
`OBJLayerTex` equivalent for normal/window sprite output and a Depth32 `OBJDepthTex` equivalent for
sprite priority/depth ordering. These targets resize with the same `ScaleFactor` as the final 2D
output and remain private render-target/shader-read Metal textures until the actual sprite passes
are ported.

The BG scaffold now also mirrors GL's `AllBGLayerTex[22]` allocation shape: the same eight BG
size/classes (128x128 through 1024x1024, with the same variant counts) are allocated as private
RGBA8 render-target/shader-read textures. Later BG text, affine, extended/bitmap, and large-bitmap
passes can therefore keep the same `BGBaseIndex`/active-layer indexing model instead of inventing a
different Metal-only layout.

The input-texture scaffold now covers the remaining GLRenderer2D texture sources needed by the
first real 2D shader ports: raw BG/OBJ VRAM textures (`R8Uint`, 1024-wide with the same Engine A/B
heights as GL), BG/OBJ palette textures (`R16Uint`, preserving raw 1555 values for Metal shader
decode), the 256x16 signed mosaic lookup texture initialized with the same table as GL, and the
1024x512 sprite prerender texture. These are still resource ownership only; upload dirty tracking
and shader consumption remain the next 4a steps.

The config-buffer scaffold adds shared Metal buffers corresponding to GLRenderer2D's UBO groups:
layer config, sprite config, per-scanline config, sprite per-scanline mosaic config, and compositor
config. The CPU struct layouts are kept 16-byte aligned so later MSL argument structs can consume
the same state updates without changing the phase's resource ownership boundary.

This is deliberately **not** a visible hires path yet. `Rend2D_A/B` remain the existing soft
renderers, `GetOutput()` still returns the Phase 2/3 CPU-composited frame, and no
`RendererOutput::MetalTexture` switch is re-enabled. That preserves the Phase 4e rule against a
temporary "Metal 3D hires + CPU 2D upscale" hybrid while building the real GLRenderer mirror behind
the current correct output path.

**Verified (2026-07-10, local Intel Mac):** `tools/macos/build_metal_test.command`,
`cmake --build build-mac --parallel 4`, default-binary Metal strings check, `git diff --check`, and
the standard config/inc/literal/scatter audits all pass. **Not verified:** visible hires output,
because BG/OBJ/final Metal 2D drawing is intentionally not wired yet.

---

## 3o. Tester UI exposure + perf logging workflow

Following
[plan/metal_tester_ui_perf_audit_and_execution_instructions_v2.md](plan/metal_tester_ui_perf_audit_and_execution_instructions_v2.md),
the maintainer/tester workflow now exposes Metal early as an explicitly experimental tester option.
This is **not** a public-stability claim and does not change the Phase 8 caveats above.

Implementation points:

- Standard Video Settings creates `rb3DMetal` dynamically only in
  `__APPLE__ && MELONPRIME_ENABLE_METAL` builds, so default and force-disabled builds do not get
  Metal UI strings from `VideoSettingsDialog.ui`.
- The new radio button is labelled `Metal (Experimental)`, is registered as `renderer3D_Metal`, and
  is runtime-gated by `MelonPrime::Metal::SupportsRequiredBaseline()`. If the probe fails, the
  button is disabled and its tooltip uses `CachedFeatureInfo().unavailableReason`.
- OpenGL-only controls remain OpenGL-only: improved polygon splitting stays OpenGL-only, and
  high-resolution coordinates stay OpenGL Compute-only. The internal-resolution combo is shared by
  OpenGL/OpenGL Compute and experimental Metal during the tester phase, using the existing
  `3D.GL.ScaleFactor` hardware-renderer scale key until Metal is stable enough to justify separate
  defaults/migration. The Software-threaded checkbox stays enabled for Metal while the current Metal
  path still uses the `SoftRenderer3D` delegate.
- MelonPrime VIDEO QUALITY creates a dynamic `Video quality: Metal Test` button only in
  Metal-enabled macOS builds. It sets `Screen.UseGL=false`, disables VSync, selects
  `renderer3D_Metal`, sets the shared internal scale to 4x by default, keeps `High2` untouched, and
  uses an honest tooltip: experimental native Metal, not High2/OpenGL Compute, with effects/performance
  still incomplete. Testers can change the internal resolution afterward in standard Video Settings
  while Metal is selected.
- `MELONPRIME_METAL_PERF=1` enables low-noise aggregate logging every 600 `MetalRenderer3D` frames:
  average total frame time, native opaque-pass time, texcache time, upload bytes, draw groups, draw
  calls, command-buffer wait time, polygon counts, and whether the CPU/software delegate is still
  active. Logs also include the current Metal scale and target size (for example `scale=4
  target=1024x768`) so 1x/2x/4x tester runs can be compared honestly.
- The tester visibility path now exposes `MetalRenderer3D`'s native `ColorTarget` through
  `RendererOutput::MetalTexture` and logs when `ScreenPanelMetal` receives that native target. The
  presenter keeps the software delegate's CPU BGRA frame as the visible base for both screens because
  the native target is not a complete composited top screen yet; replacing source screen 0 outright
  hides the game image. CPU fallback remains in place while full 2D/3D composition parity is
  unfinished.
- `tools/macos/build_metal_test.command` builds `build-mac-metal-test` with
  `MELONPRIME_ENABLE_METAL=ON`; `tools/macos/run_metal_test.command` launches the built app with
  `MELONPRIME_METAL_PERF=1` by default and clears inherited force-Metal renderer/presenter env vars
  unless `MELONPRIME_ALLOW_FORCE_METAL=1` is set, so testers exercise the new UI selection path.

Performance stance remains unchanged: Metal is expected to become the macOS-friendly high-performance
path, but current results must be measured. Do not claim it is faster than Software/OpenGL/High2, and
do not call it OpenGL Compute equivalent.

---

## 4. Remaining phases / gates (Phase 8 onward)

Carried over from the source design document, trimmed to phase titles + the acceptance gate that
blocks starting each one. Do not begin implementing any of these without the stated gate
satisfied; see
[plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md](plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md)
for the per-phase code sketches, class skeletons, and acceptance-gate detail.

| Phase | Title | Blocked on |
|---|---|---|
| 8 remainder | Replace the Software delegate in `MetalRenderer3D` with native Metal equivalents of `GLRenderer3D`'s shader, texture-cache, depth/stencil, fog/edge, and final-pass paths | Current scaffold stable; needs ROM gameplay parity against Software/OpenGL on Intel. Apple Silicon parity remains a separate gate before user exposure. |
| 9 tester exposure | Separate macOS "Metal" tester option/button in Video Settings and MelonPrime Settings (`High2` stays OpenGL-compute-only and stays disabled on macOS) | Allowed before Apple Silicon only as explicitly experimental tester UI; public/shipped exposure still requires Phase 8 parity and Apple Silicon confirmation |
| 10 | Optional Metal compute-style renderer (stretch) | Phase 9 stable; separate GLSL→MSL barrier-semantics audit (`glMemoryBarrier` does not map 1:1 to any single Metal call) |

**Hard rule inherited from the source document, applies to every phase above:** `High2` must never
be redirected to Metal, on any platform, at any point. If a maintainer eventually exposes Metal in
the standard (non-MelonPrime) Video Settings dialog, it must be a new radio button, not a
repurposing of the existing "Compute" option.

---

## 5. Session hardware

Discovered 2026-07-09 while starting Phase 1 (previously assumed unavailable — see the original
Phase 0 commit's caveats, now superseded by this section):

```text
$ system_profiler SPDisplaysDataType
Intel Iris Plus Graphics 655 — Metal Support: Metal 3, VRAM 1536 MB, Built-In Retina LCD 2560x1600
$ sw_vers
macOS 15.7.7 (24G720)
```

This matches the design document's own testing-matrix entry ("Intel MacBookPro15,2 / Intel Iris
Plus Graphics 655 / x86_64 / macOS 15.x") closely enough to treat this session as a genuine Intel
test target, not just an editor. No Apple Silicon hardware is available from this session at any
point — that gate stays open regardless of how far Phases 2-10 progress here.

---

## 6. Progress tracking

| Phase | Status | Date | Notes |
|---|---|---|---|
| Kill switch scaffolding | Done | 2026-07-09 | CMake options + `MELONPRIME_METAL_ACTIVE` resolution; verified default/force-disable/enable all configure+build clean; no Metal symbols/strings in default binary |
| 0 — renderer safety hardening | Done | 2026-07-09 | `MelonPrimeVideoBackend.h/.cpp`; wired into `EmuThread::updateRenderer()`'s existing `.inc` hook and `EmuInstance::usesOpenGL()`; local build + PS1 audits green; manual stale-TOML runtime check and Windows/Linux CI not performed this session |
| 1 — presentation backend split | Done | 2026-07-09 | `PresentationBackend` enum + `ResolvePresentationBackend()`/`IsOpenGLPresentation()`; single call site in `createScreenPanel()`; local build + launch smoke test + all PS1 audits green (scatter budget exempt-marker applied); interactive Settings panel-swap regression not verified |
| 2 — Metal feature probe | Done | 2026-07-09 | `MelonPrimeMetalFeatureCheck.h/.mm`; **runtime-verified on real Intel Metal 3 hardware** (device query + command queue + real shader compile + BGRA8Unorm pipeline state all succeeded); default tree unaffected, scatter budget exempt-marker applied; Apple Silicon still unconfirmed |
| 3 — EmuThread backend tracking | Done (partial by design) | 2026-07-09 | New `videoBackend` member tracked at all 4 write sites; the 5 read sites (context-current gating, repaint signal, VSync branch) deliberately left on `useOpenGL` until Phase 4 needs 3-way branching; both `build-mac` and `build-mac-metal-test` rebuild clean, launch smoke tests pass, all PS1 audits green |
| 4 — `ScreenPanelMetal` presenter | Done (Intel runtime smoke) | 2026-07-09 | `CAMetalLayer` presenter compiled only under `MELONPRIME_METAL_ACTIVE`; env-only bootstrap `MELONPRIME_FORCE_METAL_PRESENTER=1`; CPU BGRA framebuffer upload/present path runtime-verified on Intel Iris Plus 655; default binary has no Metal strings; OSD/HUD/splash and Apple Silicon still pending |
| 5 — OSD + Custom HUD presenter parity | Done (Intel no-ROM overlay smoke) | 2026-07-09 | Added Metal UI alpha pipeline and QPainter full-window overlay upload for no-ROM splash, OSD, and Custom HUD/radar via existing software HUD code; forced-Metal run logs first draw + first UI overlay; ROM gameplay visual parity and Apple Silicon still pending |
| 6 — `RendererOutput` abstraction | Done | 2026-07-09 | Added typed CPU/OpenGL/Metal output wrapper around legacy `GetFramebuffers()`; frontend presenters now branch by explicit output kind; Metal kind is compile-gated out of default core; both build trees and audits green |
| 7 — `MetalRenderer` shell + enum | Done | 2026-07-09 | Added compile-gated `renderer3D_Metal`, developer-only `MELONPRIME_FORCE_METAL_RENDERER=1`, and a failing-safe `MetalRenderer` shell whose `Init()` returns false for Software fallback; Metal-enabled/default builds and audits green; no-ROM smoke verifies presenter path but not shell runtime fallback |
| 8 — Metal renderer native port | In progress | 2026-07-09 | Baseline commit made `MetalRenderer` produce correct CPU BGRA through the Metal presenter; follow-ups added `MetalRenderer3D` with real Metal device/queue/color/depth-stencil/attribute targets, plus runtime-compiled MSL clear shader and `MTLRenderPipelineState`/`MTLDepthStencilState` used by a full-screen triangle clear pass. Installed in `Rend3D` with a Software raster delegate. §3i audit-fix pass closed the `EmuThread` renderer-routing bug that would have blocked Phase 9, a `VideoSettingsDialog` null-deref crash risk + `UsesGL()` GL-context misclassification, `MetalRenderer3D` resource hardening, and probe strengthening. §3j added port-order steps 2-3 (vertex/index upload, opaque-polygon rasterization). §3k added step 4 (texturing) by reusing the shared `Texcache<>` template (same DS-format decode as `GLRenderer3D`) via a new `TexcacheMetalLoader`, with modulate/decal blend modes matching `3DRenderFS.glsl`. §3l strengthened the feature probe to verify real `MTLTextureType2DArray`/`RGBA8Uint`/`texture2d_array<uint>` sampling end-to-end, not just pipeline creation. **§3m closed the Priority-2 integration gate**: real MPH ROMs became available this session, and one-shot proof-of-integration logging confirmed the entire pipeline (`RenderFrame()` → `RenderNativeOpaquePolygons()` → `Texcache<>::GetTexture()`/`TexcacheMetalLoader`) actually runs against real `GPU3D::RenderPolygonRAM`/VRAM state from a real booting ROM (firmware boot through the game's own boot sequence, WIFI init, license-file load, clean save flush) for 70s and 113s across two runs, with sensible real data (`considered=6` polygons and a `256x64/64-layer` texture array matching the template's own layer-budget formula exactly), no crash. Output is still not wired to `GetLine()`/display (per Priority 3, deliberately unchanged), so this is integration/stability verification, not visual parity. Both §3j/§3k's shader/pipeline logic were also independently verified via standalone Metal harnesses (position math, both depth paths, discard, modulate/decal blend, untextured sentinel — all matched hand-computed expectations exactly). Builds/audits green on both trees throughout every increment; default binary still has zero Metal strings. §3n closed Priority 4: `TexRepeat` wrap/mirror/clamp is now implemented via a 3x3 sampler-state matrix keyed into `OpaqueDrawGroupKey`, matching `GLRenderer3D::SetupPolygonTexture()`'s address-mode derivation exactly; re-verified against the same real ROM with no regression. §3n.1 added GL-style toon/highlight `blendmode==2` shader substitution using `RenderDispCnt` and `RenderToonTable`. §3n.2 started GL-style attr target parity for plain clear polyID/fog flag and opaque fragment polyID/fog output. §3n.3 added clear bitmap color/depth/fog upload and fullscreen clear pass support. §3n.4 added the depth/attr-driven fog final post-pass. §3n.5 added the depth/attr-driven edge final post-pass. §3n.6 added line polygon draw support. §3n.7 added the shadow-mask stencil foundation by clearing/setting stencil bit 7 without color writes. §3n.8 added the two-step visible shadow polygon pass. §3n.9 fixed translucent attr color-mask parity for fog participation. §3n.10 documented and instrumented VRAM display-capture textures through the SoftRenderer/Texcache path. §3n.11 added BetterPolygons center-fan splitting. Special clear-alpha-zero shadow/background behavior, hi-res scale, final composite, visual parity, and Apple Silicon all remain open -- Phase 8 is genuinely multi-session work |
| 9 — tester UI exposure / perf workflow | In progress | 2026-07-09 | See §3o. Exposes `Metal (Experimental)` and `Video quality: Metal Test` only in Metal-enabled macOS builds, enables the internal-resolution combo for experimental Metal via the existing `3D.GL.ScaleFactor` key, adds `MELONPRIME_METAL_PERF=1` aggregate logging with scale/target fields, exposes the native Metal color target through `RendererOutput::MetalTexture` for presenter-side bring-up logging while keeping CPU BGRA as the visible base frame, and adds macOS `.command` helpers that test the UI path by default. This is tester-only, not public stability; Apple Silicon and Phase 8 visual parity remain open |
| 10 | Not started | — | See §4. Phase 10 remains stretch work after Phase 9 stabilizes; no Metal compute-style renderer has been started |
