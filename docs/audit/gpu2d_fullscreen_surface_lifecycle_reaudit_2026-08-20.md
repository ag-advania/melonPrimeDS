# GPU2D fullscreen surface-lifecycle re-audit — 2026-08-20

## Scope and verdict

This report records the implementation and validation of
`.codex/MelonPrimeDS_GPU2D_フルスクリーン_2D点滅_色化け_再監査_修正指示書_develop_hud_2026-08-20.md`.
The instruction file is a local, untracked work instruction and is not part of
the implementation commit. The implementation was made on branch
`develop_hud`, parent commit `d2a8a93a6873d19a1a9dbd6f266b88865d70b1e2`, with a
dirty working tree containing only the changes listed by `git status`.

The Windows implementation and the requested USA Rev 1 Software state-load
matrix are complete. The 100-toggle Vulkan validation gate is also complete.
Cold-boot visual capture on every backend, cross-platform hardware coverage,
and the 14 FPS/4x performance gate remain unrun and are not claimed here.

## Root causes addressed

The previous failure surface was not a single shader color equation. It was a
presentation-lifecycle contract failure:

1. Native surface identity was represented by a dirty bit without a complete
   GUI-thread snapshot and generation contract on Windows/non-Linux paths.
2. The emulation thread could observe QWidget/native-window state directly,
   and child native-surface recreation could be missed by the parent event.
3. Renderer-output backpressure could be promoted to a native GPU2D fatal
   failure, causing a Software/hybrid frame during fullscreen transitions.
4. Vulkan accepted an arbitrary first surface format/colorspace instead of
   failing closed to the required SDR 8-bit UNORM + sRGB nonlinear contract.
5. DX12 did not explicitly reassert the required SDR color space on every
   swapchain creation/resizing path.
6. The frontend could expose the startup Software-2D + native-3D placeholder
   before the first complete native frame.

The fix now provides:

- `NativeSurfaceSnapshot`/`NativeSurfaceSnapshotStore` with native handle,
  platform connection pointers where required, generation, logical/physical
  extent, fullscreen state, and validity, published only from the GUI side.
- Child native-host lifecycle callbacks. Destruction publishes an invalid,
  incremented generation before teardown; creation/recreation publishes a new
  valid generation before the emulation thread rebinds.
- Presenter-side quiesce, retained-output invalidation, lease release,
  surface/swapchain shutdown, and generation-aware rebind. Same-identity size
  changes use the resize path.
- `GPU2DComposeResult::{Success,Backpressure,Unavailable,Fatal}`. Backpressure
  and temporary unavailability preserve the last known-good native frame and
  do not trigger Software fallback or runtime failure.
- Vulkan fail-closed format/colorspace selection and explicit recreation logs;
  DX12 `DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709` application on each swapchain
  creation/resizing path.
- A first-complete-native-frame visibility gate. Internal startup fallback
  bookkeeping is not exposed through the native child before a complete native
  frame exists.
- A parameterized fullscreen event-matrix runner so the acceptance count is
  explicit rather than hard-coded to eight toggles.

## Build and static evidence

The following Windows builds completed successfully after the final lifecycle
callback change:

- `tools\build\windows\build-mingw-existing.bat --jobs 1`: Release,
  `[65/65] Build succeeded`.
- `tools\build\windows\build-mingw-validation.bat --jobs 1`: Debug with
  developer features and Vulkan validation, `[200/200] Build succeeded`.

The following audits passed with exit code 0:

- `audit-gpu2d-native-temporal-contract.py`
- `audit-raster-software-parity.py`
- `audit-vulkan-native-surface-visibility.py`
- `audit-structured-composition-contract.py`
- `audit-renderer-physical-ab-contract.py`
- `audit-melonprime-thread-boundary.ps1` (`findings: 0`)
- `git diff --check`

The release and debug post-build test suites also passed their invoked
renderer-memory, frame-retire, queue-sharing, surface-lifecycle, present
timing/pacer, HUD-layout, XeLL, fallback-regression, and DX12-memory targets.

## Requested Software state agreement

Target ROM:

`C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`

The exact states `.ml1`, `.ml2`, `.ml3`, `.ml4`, `.ml5`, and `.ml8` were loaded
by the current Debug executable. Each run reached process exit 0, restored the
configuration, reported `[SavestateDiff] ... loaded=1`, and produced a
non-empty Software canonical Top/Bottom dump. The independent Software dump
comparison reported exact hashes and `mismatches=0` for every requested slot:

| State | Result |
|---|---|
| `.ml1` / F1 | PASS — `mismatches=0` |
| `.ml2` / F2 | PASS — `mismatches=0` |
| `.ml3` / F3 | PASS — `mismatches=0` |
| `.ml4` / F4 | PASS — `mismatches=0` |
| `.ml5` / F5 | PASS — `mismatches=0` |
| `.ml8` / F8 | PASS — `mismatches=0` |

The run artifacts are under
`build/gpu2d-validation-20260820-current-surface-software/` and
`build/gpu2d-validation-20260820-current-surface-software-f1/`. The candidate
was the current Debug build with `MELONDS_EMBED_BUILD_INFO=OFF` and a dirty
working tree, so this is an exact pixel result with unverified binary
provenance, not a clean-release provenance claim.

## Native Vulkan/DX12 state-load smoke

The current Release executable loaded `.ml1` for both native backends:

- Vulkan: process exit 0, configuration restore PASS, state action marker 1,
  native exact failure/mismatch/fallback markers 0, no `DEVICE_LOST`. The
  physical harness returned exit 1 only because this Release configuration has
  renderer performance telemetry disabled and therefore produced zero capture
  rows; runtime presentation itself was observed and clean.
- DX12: process exit 0, configuration restore PASS, state action marker 1,
  native exact failure/mismatch/fallback markers 0, capture gate PASS.

The logs show generation-aware surface recreation and first successful native
presentation. Vulkan recreated with `VK_FORMAT_B8G8R8A8_UNORM` and
`VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`; DX12 recreated with
`DXGI_FORMAT_B8G8R8A8_UNORM` and
`DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709`.

Artifacts:

- `build/gpu2d-validation-20260820-current-surface-vulkan-f1/`
- `build/gpu2d-validation-20260820-current-surface-dx12-f1/`

This is an F1 native smoke, not a claim that all six states have been rerun on
both native backends at the current dirty source.

## Fullscreen validation gate

Command shape:

```powershell
tools/testing/vulkan-present-event-matrix.ps1 `
  -Rom "C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds" `
  -BuildDir build/debug-mingw-x86_64 `
  -Phase fullscreen -FullscreenCycles 100 -ValidateSync `
  -Tag current-surface-fullscreen-100-20260820 `
  -OutDir build/gpu2d-validation-20260820-current-surface-fullscreen-100
```

Result: PASS.

- fullscreen toggles: 100
- swapchain rebuilds: 22
- device lost: 0
- synchronization hazards: 0
- validation: clean
- config restore: PASS
- layer restore: PASS
- config integrity: PASS
- restored policy: `JustInTime`, Reflex off/inactive, VSync on, FIFO

The complete output is
`build/gpu2d-validation-20260820-current-surface-fullscreen-100/vk-current-surface-fullscreen-100-20260820.out.log`.

## Screenshot diagnosis

The current Release executable accepts `--build-info-json` and emits the JSON
build-info record. The screenshot's `Unknown option 'build-info-json'` therefore
comes from an older/different executable than the current build. The separate
`libgcc_s_seh-1.dll` dialog is a runtime packaging/PATH mismatch for the binary
that was launched; it is not evidence against the current source option parser.

## Not run / not claimed

- Physical cold-boot first-100-frame visual gate for Software, Vulkan, and DX12.
- Current dirty-source Vulkan/DX12 six-state native matrix; only F1 native smoke
  was run, while all six requested slots were run for Software agreement.
- macOS, Linux, BSD hardware presentation and native-surface recreation.
- 4x/16x sustained performance, 14 FPS reproduction, or refresh-rate A/B.
- The legacy `[RasterDiff]` 3D runner as a current PASS: the attempted Vulkan
  state run loaded `.ml1` and initialized the native renderer, but emitted no
  `[RasterDiff]` frame rows because the current native GPU2D path bypasses that
  old structured-composition comparison hook. It is therefore classified as
  observation unavailable, not pixel PASS or FAIL.

These limitations do not weaken the completed Software F1/F2/F3/F4/F5/F8
agreement or the 100-cycle fullscreen validation result; they delimit the
platform and cold-boot claims that remain open.
