# MelonPrime Metal Backend Plan (Phase 0–10) — macOS Native Metal Renderer

**Created:** 2026-07-09
**Branch:** `highres_fonts_v3`
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

### 3h.1 Verification (2026-07-09, real Intel Mac)

- `cmake --build build-mac-metal-test --parallel 4` — clean after reconfiguration; core compiled
  `GPU3D_Metal.mm` under the `MELONPRIME_METAL_ACTIVE` guard.
- `cmake --build build-mac --parallel 4` — clean after reconfiguration; default build still
  excludes `GPU3D_Metal.mm`.
- Metal-enabled binary symbol/string check confirms `melonDS::MetalRenderer3D` and its
  initialization strings exist only in `build-mac-metal-test`; default binary still has no `metal
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
| 9 | Separate macOS "Metal" preset/button in MelonPrime Settings (`High2` stays OpenGL-compute-only and stays disabled on macOS) | Phase 8 stable on Intel **and** confirmed on Apple Silicon before exposing to users — this is the phase where "Intel-only verified" is no longer good enough, since it changes what a shipped build offers |
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
| 8 — Metal renderer native port | In progress | 2026-07-09 | Baseline commit made `MetalRenderer` produce correct CPU BGRA through the Metal presenter; follow-up added `MetalRenderer3D` with real Metal device/queue/color/depth-stencil/attribute targets and a clear pass, installed in `Rend3D` with a Software raster delegate. Builds/audits/no-ROM smoke green; native replacement for `GLRenderer3D` draw/shader/texture paths and ROM parity remain open |
| 9–10 | Not started | — | See §4. Apple Silicon confirmation (required before Phase 9 ships to users) is not available in this session; Phase 8 native `GLRenderer3D` replacement remains incomplete |
