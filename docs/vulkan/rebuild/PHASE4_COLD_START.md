# Phase 4 — ROM cold-start (rebuild branch)

Run: `python tools/test_sapphire_vulkan_cold_start_regression_s79.py`
Binary: `build/rebuild-mingw-x86_64/melonPrimeDS.exe` (preferred; exact-pinned GPU2D
off, diagnostic symbols on, LTO/strip off — see `.claude/skills/build-mingw-vulkan-
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
instead of presenting. Not yet root-caused: could be a genuine surface-
generation bug, or could be an artifact of the offscreen/headless Qt
platform never producing a real swapchain generation bump the way an
on-screen window does. Needs investigation before the cold-start gate can go
green.

## CI

- `python tools/generate_sapphire_vulkan_sources.py --verify` added to `sapphire-vendor-parity.yml`
- Cold-start gate remains **expected red** until the `surfacePresent=1`
  assertion above passes, but the blocking crash-on-boot and crash-on-exit
  bugs that dominated S77 through S81 are both fixed
