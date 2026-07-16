# Phase 4 — ROM cold-start (rebuild branch)

Run: `python tools/test_sapphire_vulkan_cold_start_regression_s79.py`
Binary: `build/rebuild-mingw-x86_64/melonPrimeDS.exe` (preferred; exact-pinned GPU2D
off — the third of three exact-pin compile errors,
`melonDS::GPU2D` class-vs-namespace conflict, remains unfixed, see
`docs/vulkan/rebuild/EXACT_PIN_COMPILE_STATUS.md` — diagnostic symbols on,
LTO/strip off — see `.claude/skills/build-mingw-vulkan-
sapphire-rebuild-exact.bat`) with `MELONPRIME_SAPPHIRE_REBUILD=ON`. The test's
`find_vulkan_binary()` checks this tree first.

## Post-FinishFrame crash: root-caused and fixed (2026-07-16, S81-1/S81-4/S81-5)

The crash that blocked every attempt from S77 through S81 is fixed. Full root
cause and the symbolization method that finally found it:
[PHASE4_POST_FINISHFRAME_CRASH_ROOT_CAUSE.md](PHASE4_POST_FINISHFRAME_CRASH_ROOT_CAUSE.md).

Short version: `sapphire_frame_latch_core` / `sapphire_frame_queue_core` (two
CMake `OBJECT` libraries compiling files that `#include "GPU.h"`, and through
it `NDS.h`) never defined `JIT_ENABLED` and were missing several Vulkan/VMA
macros that `core` (the library that actually compiles `NDS.cpp`/`GPU.cpp`)
defines. `JIT_ENABLED` alone gates tens of megabytes of ARMJIT lookup-table
members in `class NDS` — a probe confirmed `sizeof(NDS)`/`offsetof(NDS, GPU)`
differed by ~62MB between the two compilation contexts. Real ODR violation,
not a logic bug in `RunFrame()`/`FinishFrame()` (where every prior session's
tracing was aimed). Fixed by linking those object libraries against `core`
itself instead of hand-copying its macro list, so future PUBLIC macros on
`core` can't silently drift out of sync again.

## Shutdown crash: also root-caused and fixed (2026-07-16, same session)

After the fix above, the cold-start test still crashed, but during process
teardown: `MelonPrimeVulkanFrontendSession::unregisterPresenter()`
(`std::mutex::lock()` on what looked like an already-destroyed mutex),
called from `ScreenPanelVulkan::~ScreenPanelVulkan()` during
`MainWindow::~MainWindow()`.

Root cause: `MainWindow` has `Qt::WA_DeleteOnClose`, so `win->close()` only
*schedules* `~MainWindow()` (and therefore `~ScreenPanelVulkan()`) via
`deleteLater()` — it does not destroy the object synchronously.
`EmuInstance::deleteWindow(id, true)` calls `win->close()` and then, in the
very same call stack (once `numWindows == 0`), calls
`deleteEmuInstance(instanceID)` -> `delete inst` ->
`EmuInstance::~EmuInstance()`, which synchronously destroys
`vulkanFrontendSessionOwner`. By the time Qt's event loop actually processes
the deferred `deleteLater()` and runs `~ScreenPanelVulkan()`, the
`EmuInstance` it still held a raw pointer to (and, through it,
`vulkanFrontendSessionOwner`) was long gone. This is a real, pre-existing
ordering hazard in the shutdown path, not something introduced by the
FinishFrame fix or by the test harness — it would affect a real user closing
the Vulkan-backed window too, not just the cold-start test.

Fixed by making `ScreenPanel::beginClose()` virtual and overriding it in
`ScreenPanelVulkan` to unregister the presenter there instead of (only) in
the destructor. `beginClose()` already runs synchronously from
`MainWindow::closeEvent()`, before `deleteWindow()`/`~EmuInstance()`, so
`emuInstance` and its `vulkanFrontendSessionOwner` are still guaranteed
valid at that point. The destructor keeps the same unregister call as a
safety net for a `ScreenPanelVulkan` destroyed without going through
`closeEvent()` first, gated on the same `sessionPresenterRegistered` flag so
it becomes a no-op in the normal (now-fixed) path.

Separately, the cold-start test harness itself was calling
`QMetaObject::invokeMethod(qApp, "quit", Qt::QueuedConnection)` directly,
which bypasses `MainWindow::closeEvent()` entirely (`quit()` only stops the
event loop; it doesn't close any window). Changed it to invoke
`EmuInstance::deleteAllWindows()` instead, the same call a real user's
window-close reaches via `closeEvent()`, so the test now exercises the same
shutdown path production users do rather than a shortcut that happened to
expose (but did not cause) the ordering hazard above.

## Current status (2026-07-16)

The cold-start regression test **no longer crashes at all** — commit/dirty
build, ~5 Vulkan frames produced and pushed
(`queuePush`/`completeProducerTransaction`/`refreshActualRenderer` all
succeed repeatedly), clean shutdown through `deleteAllWindows()` ->
`~EmuInstance()` -> `~MainWindow()` -> `~ScreenPanelVulkan()`, process exits
0. The test still **fails** on a content assertion —
`self.assertRegex(combined, r"\[VulkanPresent\] frameId=\d+
surfacePresent=1", ...)` never matches; every acquired frame in this
`QT_QPA_PLATFORM=offscreen` run logs `defer id=... reason=surfaceGenMismatch`
instead of presenting. Root-caused (not yet fixed): `frame->surfaceGeneration` is written to a real,
tracked value (`activeSurfaceGeneration_`) in exactly one place,
`DesktopFrameLifetimeTracker.cpp` — and that file is compiled out entirely
under `MELONPRIME_SAPPHIRE_REBUILD=ON`
(`src/frontend/qt_sdl/CMakeLists.txt`'s `if (NOT MELONPRIME_SAPPHIRE_REBUILD)
target_sources(melonDS PRIVATE DesktopFrameLifetimeTracker.cpp ...)`).
Every other write site (`VulkanReference/FrameQueue.cpp:252`) just
initializes it to the literal `0` and never updates it again. So on the
rebuild branch every produced frame permanently carries `surfaceGeneration =
0`, while `surfaceHost.generation()` (the live value frames are compared
against at present time) increments to `1` once the surface actually
configures — a mismatch on every single frame, forever, regardless of
platform.

This is exactly the architecture gap the base rebuild plan already flagged
for Phase 3/5 ("旧Desktop full pipelineが接続されたまま" /
"旧実装削除...CMakeから旧実装は未削除"): removing
`DesktopFrameLifetimeTracker.cpp`'s compilation without replacing its
`surfaceGeneration` bookkeeping with a pure-Sapphire equivalent left this
responsibility unimplemented rather than migrated. Two real fix directions,
neither attempted yet:
1. Re-enable `DesktopFrameLifetimeTracker.cpp` under
   `MELONPRIME_SAPPHIRE_REBUILD=ON` too (fast, but keeps "Desktop" naming/
   logic alive in the "pure Sapphire" build, contradicting the rebuild's own
   stated goal).
2. Implement the equivalent generation-tracking in the pure-Sapphire path
   (`SapphireVulkanFrameLatch.cpp` / `SapphireVulkanFramePipeline.cpp`) and
   have frame production read from there instead. Matches the rebuild's
   actual intent but is real feature work, not a one-line fix.

## OpenGL/Software rendering broken after touching Vulkan: partial fix, REOPENED (2026-07-16, S82)

**Status correction (S82 audit,
`.claude/rules/plan/Vulkan_S82_OpenGL無表示_Software走査線縦複製_DeadLegacy2DPath_Sapphire単一Ownership_監査修正指示.md`):**
the `ActiveGPU2DPath` fix below is real and worth keeping, but it does **not**
fix OpenGL or Software rendering. It only stops the state flag from getting
stuck; the branch it now correctly falls back to
(`GPU2DExecutionPath::LegacyOuterRenderer`) is dead code in a
`MELONPRIME_ENABLE_VULKAN` build: `SoftRenderer`/`GLRenderer` never construct
`Rend2D_A`/`Rend2D_B` in that configuration (`#if !defined(MELONPRIME_DS) ||
!defined(MELONPRIME_ENABLE_VULKAN)` guards them out in both `GPU_Soft.cpp`
and `GPU_OpenGL.cpp`), so the legacy path has no BG/OBJ producer at all.
Software's `SoftRenderer::DrawScanline()` still unconditionally runs
`DrawScanlineA`/`DrawScanlineB`, which read from `Output2D[2][256]` — never
written this frame because the only writer (`Rend2D_A->DrawScanline(line)`)
is compiled out — producing the reported symptom exactly: one stale
horizontal line duplicated down all 192 rows, independently per screen (two
separate `Output2D[screen]` slots). OpenGL's `GLRenderer2D` is compiled out
the same way, so its `OutputTex2D[0]/[1]` textures are allocated but never
populated, and the final compositor pass samples them anyway -> blank.

This was not caught before because verification for the earlier fix used
only the existing Vulkan cold-start regression test, which never selects
Software or OpenGL — the `ActiveGPU2DPath` transition log line was confirmed
correct, but "the flag resets correctly" was mistaken for "the target it
resets to actually renders." The claim "OpenGL/Software rendering broken ...
root-caused and fixed" below is inaccurate and reopened.

Real fix requires unifying `GPU3D` renderer ownership across backends (today
only Vulkan calls `GPU::SetRenderer3D()`; Software/OpenGL keep their 3D
renderer private in the base `Renderer::Rend3D` member, so
`GPU3D.HasCurrentRenderer()` is false for them) and making the canonical
`SapphireGPU2DCore::GPU2D::SoftRenderer` (already generic — it branches on
`GPU.GPU3D.IsRendererAccelerated()`, not on a Vulkan-specific flag, for both
BG/OBJ composition and 3D-line consumption via `GetLine()`) the single
always-active GPU2D compositor for every backend, instead of a Vulkan-only
`ActiveGPU2DPath` selector choosing between it and a legacy path that no
longer exists in this build. See the S82 plan for the full analysis and
phased fix; work in progress below this section.

### S82-4 (2026-07-16, `d90bc05e8`): GPU3D ownership unified; Software activation wired

`Renderer::TakeOwnRenderer3D()` lets `GPU::SetRenderer()` promote each
backend's privately-constructed `Rend3D` into `GPU3D`'s unified ownership
whenever `GPU3D` has no renderer registered yet (the common case for
Software/OpenGL/Compute — Vulkan's separate `GPU::SetRenderer3D()` call
still supersedes this for the Vulkan path). `EmuThread.cpp` no longer wipes
that promotion with an unconditional `SetRenderer3D(nullptr)` on non-Vulkan
branches, and the Software renderer-selection branch now calls
`ActivateSapphireVulkan2D()` instead of unconditionally deactivating.
`GPU::ActivateSapphireVulkan2D()` no longer requires
`Renderer3D::UsesStructured2DMetadata()` (a Vulkan-only "provides
GPU-resident structured planes" flag) — only `GPU3D.HasCurrentRenderer()`,
which S82-4 now guarantees for every backend.

Because `GPU_Soft.cpp`'s `GetFramebuffers()` / `PublishSapphire2DFrame()`
already source pixels from the same GPU-owned `Framebuffer[][]` array the
canonical `GPU2D::SoftRenderer` writes into (see `SoftRenderer::DrawScanline`
early-returning via `GPU.UsesSapphireGpu2DPath()`, and
`AssignFramebuffers()`/`FramebufferPlane()` binding the same storage for
both the legacy and canonical write paths), no additional plumbing was
needed for Software specifically once ownership was unified — the dead
`Rend2D_A`/`Rend2D_B`/`Output2D` path is simply no longer reached at all.
OpenGL/Compute deliberately still call `DeactivateSapphireVulkan2D()` for
now: `GLRenderer3D::IsRendererAccelerated()` reports `true`, which would
push the canonical compositor into its GPU-resident "structured plane"
branch instead of the CPU `GetLine()` branch Software uses, and no
`GLRenderer2D` → canonical-`Unit` adapter exists yet to consume that (S82-6).

Verified: `build/rebuild-mingw-x86_64` links clean;
`test_sapphire_vulkan_cold_start_regression_s79.py` still reaches exit 0
with every checkpoint intact (the one remaining failure is the pre-existing
`surfacePresent=1` gap above, S82-9, not a regression from this change).
**Not yet verified**: an actual pixel-level check that Software's vertical
scanline-duplication symptom is gone, since this environment has no
GPU/display and no fixture existed for it — that is S82-1, tracked next in
the S82 plan.

### Original (partial) writeup, 2026-07-16

User report after retesting the fixes above: OpenGL and Software rendering
were broken (blank/garbled), and Vulkan ROM boot still showed no change from
the splash screen (the already-documented `surfaceGenMismatch` gap above,
unrelated).

Root cause, in `src/GPU.cpp`: `GPU::IsSapphireCanonicalGpu2DActive()` is
gated purely on one flag, `ActiveGPU2DPath == GPU2DExecutionPath::
SapphireCanonical`, and is consulted at 17+ call sites throughout `GPU.cpp`
to decide whether GPU2D compositing should take the Sapphire-canonical path
or the legacy outer-renderer path. `GPU::DeactivateSapphireVulkan2D()`'s
`#if defined(MELONPRIME_SAPPHIRE_REBUILD)` branch deactivated `Sapphire2D`
itself but never reset `ActiveGPU2DPath` back to `LegacyOuterRenderer` (the
non-rebuild fallback path below it does reset it). `GPU::SetRenderer()` calls
`DeactivateSapphireVulkan2D()` unconditionally on every renderer transaction,
and `EmuThread.cpp`'s video-transaction code also calls it explicitly
whenever the selected renderer is not Vulkan
(`else { nds->GPU.DeactivateSapphireVulkan2D(); }`, EmuThread.cpp:1685-ish).
So: boot or switch to Vulkan once (`ActivateSapphireVulkan2D()` sets
`SapphireCanonical`) -> switch to OpenGL or Software -> `DeactivateSapphireVulkan2D()`
runs but silently leaves `ActiveGPU2DPath` stuck at `SapphireCanonical` ->
every subsequent `IsSapphireCanonicalGpu2DActive()` check across the file
still routes 2D compositing down the Sapphire path for a renderer that has
no active Sapphire2D framebuffer bindings, producing broken/blank output for
every renderer after the first Vulkan attempt.

`ActiveGPU2DPath` defaults to `LegacyOuterRenderer`
(`src/GPU.h:1049`), so this bug is dormant on a build that has never
activated Vulkan in the current process; it manifests the first time Vulkan
is tried (or is the default) and the user then switches to OpenGL/Software,
which matches the reported symptom.

Fixed by resetting `ActiveGPU2DPath = GPU2DExecutionPath::LegacyOuterRenderer;`
unconditionally inside the rebuild branch of `DeactivateSapphireVulkan2D()`,
matching the non-rebuild fallback path. Added a matching one-shot
`Platform::Log` line on both `Activate`/`Deactivate` transitions (only logs
when the flag actually changes) so this state machine is traceable from log
output without needing to inspect the GUI. Verified the `activate` log line
fires at the expected point (`[RomBootTrace] Sapphire Vulkan activation
begin` -> `[MelonPrime] Sapphire2D: activate (rebuild path) ActiveGPU2DPath
LegacyOuterRenderer -> SapphireCanonical` -> `[RomBootTrace] frontend session
initialize begin`) in a rebuilt `build/rebuild-mingw-x86_64/melonPrimeDS.exe`
via `test_sapphire_vulkan_cold_start_regression_s79.py`; that test never
switches away from Vulkan, so the `deactivate` transition (and therefore full
end-to-end confirmation that OpenGL/Software render correctly again after
having used Vulkan) has not been exercised by an automated test in this
session — no headless test exists for OpenGL/Software content, and this
agent has no way to visually inspect GUI rendering. The fix is a direct,
narrowly-scoped correction of an asymmetry against the already-correct
non-rebuild code path immediately below it, not a new heuristic.

`build-mingw-vulkan-sapphire-rebuild-exact.bat` and the `rebuild-mingw-x86_64`
CMake preset were also corrected to default
`MELONPRIME_SAPPHIRE_GPU2D_EXACT_PIN=OFF` (it previously defaulted to `ON`,
which does not currently configure/build at all — see
`EXACT_PIN_COMPILE_STATUS.md` — so every invocation of the script since it
was added was failing before even reaching this GPU.cpp bug). The cache
verification loop's exact `KEY:TYPE=VALUE` string match was also loosened to
`KEY.*=VALUE`, since `ENABLE_LTO_RELEASE` (a `cmake_dependent_option`) can
legitimately cache as `INTERNAL` instead of `BOOL` depending on its
dependency condition, which was tripping the verification step as a false
failure even on a successful build.

## CI

- `python tools/generate_sapphire_vulkan_sources.py --verify` added to `sapphire-vendor-parity.yml`
- Cold-start gate remains **expected red** until the `surfacePresent=1`
  assertion above passes, but the blocking crash-on-boot and crash-on-exit
  bugs that dominated S77 through S81 are both fixed
