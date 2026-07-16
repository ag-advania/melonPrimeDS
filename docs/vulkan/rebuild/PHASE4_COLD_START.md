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

Latest cold-start run after the fix: the emulator boots, produces and
presents multiple Vulkan frames (`queuePush`, `completeProducerTransaction`,
`refreshActualRenderer` all succeed repeatedly), and reaches the test
harness's own `[VulkanColdStartTest] complete exitCode=0` checkpoint. The
run then crashes during process teardown in
`MelonPrimeVulkanFrontendSession::unregisterPresenter()` (`std::mutex::lock()`
on what looks like an already-destroyed mutex, called from
`ScreenPanelVulkan::~ScreenPanelVulkan()` during `MainWindow::~MainWindow()`)
— a distinct, later-stage destruction-order bug, not the original
post-FinishFrame crash. Tracked separately; see the shutdown-crash note below
once filed.

## CI

- `python tools/generate_sapphire_vulkan_sources.py --verify` added to `sapphire-vendor-parity.yml`
- Cold-start gate remains **expected red** until the shutdown crash above is
  also fixed (the harness's own success checkpoint firing before the crash is
  a big step, but the process must still exit 0 for the gate to pass)
