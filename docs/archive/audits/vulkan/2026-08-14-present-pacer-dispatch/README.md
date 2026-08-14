# Vulkan present-pacer dispatch re-audit

Date: 2026-08-14 (JST)

Repository: `develop_remakeVulkan_ver3`

Baseline: `4e7579359371` (`add md`)
Implementation commit: `5c05d249c27c` (`Add Vulkan present pacer dispatch coverage`)
Follow-up repository HEAD: `18bad3ade1f0` (`fix`)

Present-pacer audited source HEAD: `fd8e5c69e` (`Close Vulkan present-pacer audit follow-up`)

Latest repository HEAD at the renderer-fallback re-audit start: `0b5b49d10`
(`Guard renderer fallback panel lifetime`). The `0b5b49d` renderer-fallback
follow-up is recorded separately below; it must not be retroactively folded
into the `fd8e5c69e` present-pacer evidence.

The Vulkan implementation and its audit artifacts were committed by
`5c05d249`. The `.codex/` and `docs/development/codex/` files in that commit
were outside the Vulkan implementation scope, but they were co-committed and
are included in the commit provenance below.

## Root cause

The previous audit proved only pure Vulkan present-timing classifiers and presenter-action mapping. The production `VulkanPresentPacer` dispatch path was not linked into the test target, so a fake Vulkan result could not be shown to reach the production state machine, generation/capture invalidation, queue-pressure handling, or presenter action.

The first Linux re-build also exposed a portability defect in the new test wiring: `vulkan-present-timing-tests.cpp` includes `VulkanPresentPacer.h`, but the pure timing target did not propagate `MELONPRIME_VULKAN_INCLUDE_DIR`. The target failed with `vulkan/vulkan.h: No such file or directory`. This was corrected in the same Vulkan-active CMake block.

## Implementation plan and result

1. Keep hot-path behavior unchanged and introduce a value-owned `VulkanPresentPacerDispatch` containing only the nine timing/lifecycle PFNs used by the pacer.
2. Copy production PFNs and handles once during initialization; use the same `InitializeCommon` path for production and the test-only `InitializeForTesting` seam.
3. Add a separate fake-dispatch executable that compiles the production pacer implementation and drives scripted Vulkan results through it.
4. Cover every contract-relevant result class, retry/disable behavior, queue allocation pressure, generation/capture fallback, and same-frame swapchain recreation plus lifecycle failure.
5. Propagate the pinned Vulkan include directory to both Vulkan test targets and re-run macOS, Linux, static, and physical F2 checks.

## Luna workers

- `pacer_dispatch_design` (Luna read-only): mapped production dispatch calls, state transitions, and the minimal SRP/KISS seam.
- `presenter_route_audit` (Luna read-only): audited `BeginFrame`/`EndFrame` routing and Vulkan result contracts; identified the documentation drift for `VK_NOT_READY` on time-domain queries.
- `implement_pacer_fake_dispatch` (Luna Max): implemented the dispatch seam, production direct-PFN path, fake API-level tests, CMake target, audit assertions, and documentation correction. A follow-up was delegated for the three gaps found by main review (same-frame lifecycle failure, exhausted time-domain retry, and queue allocation failure); all were fixed and re-audited.
- `fix_linux_vulkan_test_includes` (Luna Max): added `${MELONPRIME_VULKAN_INCLUDE_DIR}` to the pure timing target, verified Vulkan ON/OFF target conditioning, and passed the macOS target build/tests.

## Vulkan implementation-scope files

- `src/VulkanPresentPacer.h`
- `src/VulkanPresentPacer.cpp`
- `tools/testing/vulkan-present-pacer-dispatch-tests.cpp`
- `src/frontend/qt_sdl/CMakeLists.txt`
- `tools/ci/audits/audit-low-latency-contract.py`
- `docs/features/rendering/vulkan-backend.md`
- `docs/archive/audits/vulkan/2026-08-14-present-pacer-dispatch/f2-runtime.log`
- this `README.md`

## Commit provenance

The same `5c05d249` commit also included `.codex/config.toml`,
`.codex/agents/luna-worker.toml`, and
`docs/development/codex/luna-orchestrator-prompt.md`. Those files were outside
the Vulkan implementation scope, but they were co-committed and are not
described as uncommitted or omitted from the commit scope.

The P4 closure follow-up is a separate current working-tree change and is not
retroactively attributed to `5c05d249`; it updates this README and
`docs/features/rendering/vulkan-backend.md`, and extends
`tools/testing/vulkan-present-pacer-dispatch-tests.cpp` with recovery and
legacy-capability assertions.

## Test and build results

### Production/fake coverage

`melonprime_vulkan_present_pacer_dispatch_tests` passes on macOS and Linux. It exercises fake calls through production code for:

- `WaitForPresent2KHR`: `TIMEOUT`, `SUBOPTIMAL`, `OUT_OF_DATE`, `DEVICE_LOST`, `SURFACE_LOST`, success, and unknown/disabled-wait handling.
- `GetPastPresentationTimingEXT`: `INCOMPLETE`, lifecycle failures, success, and optional disable.
- `GetSwapchainTimingPropertiesEXT`: `NOT_READY` retry and `SURFACE_LOST` failure.
- `GetSwapchainTimeDomainPropertiesEXT`: count/array success, bounded `INCOMPLETE` retry, retry exhaustion after a present, `SURFACE_LOST`, and missing-domain behavior.
- `GetRefreshCycleDurationGOOGLE` and `GetPastPresentationTimingGOOGLE`: success, incomplete, lifecycle failures, and optional disable.
- queue-size allocation failure, queue-pressure detection/retry/pause, and
  completed-report drain → queue growth → metadata re-enable recovery.
- modern surface-capability failure → legacy surface-capability fallback.
- same-frame swapchain recreation with eager lifecycle failure, generation invalidation, backend-none capture fallback, and next-frame typed failure/action routing.

`melonprime_vulkan_present_timing_tests` also passes. `python3 tools/ci/audits/audit-low-latency-contract.py` and `python3 tools/ci/audits/audit-raster-software-parity.py` pass. `python3 tools/ci/audits/check-vulkan-shaders.py` passes: 111 modules compiled/validated, 592 scale-specialized modules validated, and arithmetic checked for scales 1..16.

### macOS

- Developer-features ON Vulkan/Metal build: PASS (approved wrapper, `--jobs 4`).
- Release-features OFF Vulkan/Metal build: PASS (219/219).
- Pure and fake timing targets: PASS.
- Deep strict codesign verification for both bundles: PASS.
- Runtime bundle contains x86_64 MoltenVK and uses the bundled loader.
- Current-HEAD Debug validation build: PASS (`build-mac-vulkan-validation`,
  Vulkan/Metal, `--jobs 4`, no-bundle test runtime).
- Mac-only validation runtime: PASS for a 60-second F2 launch on Intel Iris
  Plus 655. The Khronos loader, MoltenVK ICD, and
  `VK_LAYER_KHRONOS_validation` were active; the state loaded, Vulkan remained
  the actual renderer at 1x, and no VUID or validation ERROR message appeared.
  The only two validation-channel messages were MoltenVK warnings about
  unsupported primitive-restart disabling (`mvk-warn`,
  `VK_ERROR_FEATURE_NOT_PRESENT`).

### Linux

The clean VirtualBox Ubuntu build completed with the pinned Vulkan-Headers revision and two guest CPUs: `313/313`, including production `VulkanPresentPacer.cpp`, pure timing test PASS, fake-dispatch test PASS, and final `melonPrimeDS` link PASS. Wayland pointer lock remained `AUTO`; the build selected the X11/Qt fallback because `wayland-protocols` is absent.

### Windows and validation layer

- Original macOS audit host: Windows build `NOT RUN` because it had no
  MinGW/MSYS2 or `cl.exe`.
- Khronos validation layer: the packaged bundle intentionally loads its direct
  MoltenVK dylib, so a Finder-style bundled run does not load the Khronos
  layer. The Mac-only no-bundle Debug run above used the Homebrew Khronos
  loader plus the MoltenVK ICD and completed the validation gate; Windows and
  Linux validation remain unverified here. No validation VUID or device-loss
  result was observed in the Mac run.

### Follow-up validation on the current Windows host

`tools/build/windows/build-mingw.bat --jobs 1 --tail 220` completed successfully
after reconfiguring the Vulkan ON / DX12 ON / developer-features ON Release tree.
The current source compiled and the pure timing, production fake-dispatch,
renderer-fallback, and XeLL state-machine tests all passed. This is a local
Windows build; Windows Khronos validation-layer coverage remains unverified.

### Latest `0b5b49d` renderer-fallback follow-up

The follow-up source changes are deliberately separated from the present-pacer
evidence above:

- `Window.cpp` now keeps the panel lifetime mutex/null check inside
  `#ifdef MELONPRIME_DS`; the upstream path retains its original call.
- Developer builds expose a one-shot
  `MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE` seam. The production fallback
  path, runtime-failure latch, and `MELONPRIME_FORCE_VULKAN_RENDERER` guard are
  exercised by `tools/testing/vulkan-renderer-fallback-stress.ps1`.
- With `C:\DSMPH\melonPrimeDS最新版\balancedRom.nds`, the stress run passed:
  one forced failure injection, one runtime-failure report, one Vulkan
  selection, one Vulkan-to-Software fallback, `40/40` renderer switches, no
  fatal diagnostics, and 20 seconds of process liveness. The concise committed
  evidence is `fallback-stress-runtime.log` in this directory.

This closes the `0b5b49d` fallback/panel-lifetime follow-up for the tested
Windows developer configuration. It does not close Windows/Linux Khronos layer
coverage, AMD/Intel cross-GPU coverage, or physical Linux validation.

## F2 runtime evidence

The current developer bundle was run with the verified Japanese ROM and F2 state:

- ROM: `/Users/admin/Downloads/_Documents/Metroid Prime - Hunters (Japan).nds`, SHA-256 `8116cff4964daa430c4c4039170ecd063348fc6f768636b9bc3a19a951306e02` (game code `AMHJ`).
- State: `/Users/admin/Downloads/_Documents/Metroid Prime - Hunters (Japan).ml2`, SHA-256 `fb8bb5c3c590a5a13a88681653a2fee030513cce5e9a50eeca2d7c37097a5932`.
- `MELONPRIME_TEST_SAVESTATE_UNPAUSE=1` loaded the state and emitted `loaded=1`; the user then reloaded the longer match state. Runtime resolution stayed at `1x`; no 16x setting was used.
- Vulkan selected Intel Iris Plus Graphics 655, FIFO/VSync, and Google display timing JIT. The app remained alive for over 30 minutes; the screen was visibly in-match and changed across delayed screenshots. Three resize/minimize/restore cycles completed and returned to the original 256x412 window.
- During the simultaneous two-CPU Linux LTO link, the title temporarily showed about 35/60. Immediately after the VM link completed it recovered to 58–60/60 (sample `60/60`, average 16.65 ms); later in the longer match it remained live with samples around 49/60 (`20.30 ms` average). This is host/contention and scene-load evidence, not a renderer freeze.
- `/tmp/melonprime-f2-runtime.log` was archived as `f2-runtime.log`; only the repository copy is part of this audit.

## Final audit

PASS for the requested production fake-dispatch hardening and lifecycle/result
routing at the `fd8e5c69e` audited source head. The value-owned dispatch has no
test branch, virtual call, lock, `std::function`, or per-frame allocation.
Production and test initialization share the same capability/state setup. Enum
ordering, generation separation, reset fallback, capture invalidation,
presenter action mapping, queue-pressure recovery, and legacy
surface-capability fallback were re-checked after the follow-up fixes.

The later `0b5b49d` renderer-fallback/panel-lifetime changes were separately
validated by the Windows build and process stress run above; they are not
claimed as part of the older macOS/Linux present-pacer runtime evidence.

The confusing Japanese-named temporary copy `lastRavenRom.nds` is no longer present. The remaining `mphLastRaven.nds` and `Last Raven's balanced MPH V1.2.11.nds` files are the same AMHP ROM (not the Japanese AMHJ ROM) and were intentionally left untouched.

## Remaining risks

- Windows Khronos validation-layer and Linux validation-layer execution still
  require their respective environments; the Mac Khronos validation run is
  recorded above. The Windows renderer-fallback stress is a separate local
  developer-runtime result, not a validation-layer result.
- No physical Linux Vulkan/F2 run was claimed; the Linux result is a clean compile and test execution inside the VM.
- The F2 runtime log is an Intel macOS developer run, not a cross-GPU or
  long-term visual-parity certification. The 1x setting was retained for
  deterministic F2 evidence; no 16x runtime was used.
