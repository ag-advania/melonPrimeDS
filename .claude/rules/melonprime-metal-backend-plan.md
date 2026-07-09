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

## 4. Remaining phases (Phase 3 onward — not yet implemented)

Carried over from the source design document, trimmed to phase titles + the acceptance gate that
blocks starting each one. Do not begin implementing any of these without the stated gate
satisfied; see
[plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md](plan/melonprime_metal_backend_implementation_guide_final_killswitch_separate_metal_preset.md)
for the per-phase code sketches, class skeletons, and acceptance-gate detail.

| Phase | Title | Blocked on |
|---|---|---|
| 3 | `EmuThread` `ActiveVideoBackend` lifecycle split | Phase 2 |
| 4 | `ScreenPanelMetal` presenter (`CAMetalLayer`, CPU-BGRA-only, no `renderer3D_Metal` yet) | Phase 3. Intel-side resize/fullscreen/HiDPI/input-regression testing is achievable this session (real display + real GPU); Apple Silicon confirmation is a separate, later gate before this is considered portable |
| 5 | OSD + Custom HUD presenter parity on Metal | Phase 4 stable on Intel (Apple Silicon parity separately gated) |
| 6 | `RendererOutput` abstraction (explicit CPU/GL/Metal output kind) | Phase 4 (needed before any Metal-texture renderer output can exist) |
| 7 | `MetalRenderer`/`MetalRenderer2D`/`MetalRenderer3D` shell + `renderer3D_Metal` enum value | Phase 6; must not add the enum value until the shell can fail gracefully (log + fallback) instead of crashing |
| 8 | Port `GLRenderer3D` (not the compute renderer) to Metal, correctness-first | Phase 7 stable on Intel; parity-tested against existing OpenGL/software output on this machine. Apple Silicon parity is a separate gate. |
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
| 3–10 | Not started | — | See §4. Phases 3-8 are achievable on this session's real Intel hardware; Apple Silicon confirmation (required before Phase 9 ships to users) is not |
