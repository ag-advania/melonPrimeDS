# Vulkan low-latency presentation

This document records the implementation corresponding to the Vulkan
low-latency instruction dated 2026-08-18. The presenter remains synchronized;
the optimization is allowed to drop a presentation callback, never to reuse a
busy slot or an acquired swapchain image.

## Phase 0: telemetry contract

The renderer-performance build reports the low-latency measurements required
by the instruction without adding state or timing work to a normal build. The
CPU samples cover presenter-begin, presenter-slot fence wait, swapchain image
acquire, acquired-image fence wait, HUD upload, and queue submit. The counters
cover busy-slot skips, acquired-image `VK_NOT_READY`, latency-budget skips,
screen-frame reuse, screen-layer upload skips, direct/buffer output selection,
swapchain image count, logical presenter depth, present mode, VSync, and vendor
latency mode. GPU timestamps cover the raster/compositor/presenter stages when
the telemetry build can create timestamp query pools.

The runtime matrix is kept separate from the implementation claim: a zero
counter means that branch was not triggered by that workload, not that a
stress-only branch was proven unreachable.

## Implemented phases

### Phase 1: pre-acquire presenter-slot admission

`ScreenPanelVulkan` derives `waitForPresentSlot` from
`VulkanRenderer::ShouldBypassPresentWait()`, matching the DX12 call-site
contract. When low-latency mode bypasses the wait,
`FrameRing::TryBeginFrame()` probes the next slot with `vkGetFenceStatus`.

If the fence is not ready, the callback returns before swapchain acquisition,
fence reset, command-buffer reset/begin, staging or descriptor reset, lease
replacement, `ImagesInFlight` mutation, submit, and present. Only telemetry
counters are updated. Blocking mode continues to use the existing bounded
fence wait. Swapchain-image fence handling remains blocking and unchanged.

The frame ring does not advance `CurrentIndex` until the readiness probe and
command-buffer recording boundary have succeeded. This is the invariant that
makes a busy skip a no-op for frame-ring state.

### Phase 2: same renderer-frame screen retention

The Vulkan screen panel retains a POD key made from renderer identity, frame
serial, resource generation, dimensions, and native buffer/image identity.
When the key matches, presenter-owned buffer screen images are reused without a
new `vkCmdCopyBufferToImage`. Direct sampled images restore only a persistent
descriptor-cache binding; transient descriptor-pool fallback bindings are never
retained. Direct output keeps a dedicated renderer-output lease until the key
is replaced or invalidated. When a retained direct image is sampled by a new
presenter slot, that slot also receives a renderer-output lease; this keeps the
dedicated retained lease from being released while an older slot can still
sample the image.

Retention is invalidated on renderer/backend transitions, direct resource
generation changes, screen-layer image recreation, presenter teardown, and
fallback/new-output identity changes. Save/load/reset transitions must use the
renderer transition boundary so a reused serial cannot retain stale screen
content.

`FrameRing::WaitIdle()` excludes an open, not-yet-submitted recording frame
from `CompletedFrame` bookkeeping. This is required because descriptor or
staging resource recreation can occur after presenter admission; otherwise a
later deferred-destroy collection could retire resources using that frame
number before its submission fence has completed.

The counters are emitted in renderer performance telemetry:

- `screen_frame_reuse_count`
- `screen_layer_upload_skip_count`

## Intentionally not implemented

Phase 3 image-fence non-blocking admission is not enabled. An acquired
swapchain image still follows the existing blocking fence and lifecycle path;
an acquired image is never abandoned by an early return. It should only be
revisited after telemetry proves that wait is a material hotspot and a safe
acquire/submit/present lifecycle is designed and tested.

The instruction's 60/120/144/240 Hz, VSync on/off, HUD, internal-resolution,
and lifecycle matrix must be recorded per available physical environment.
Unavailable refresh rates or platforms are recorded as `OPEN` / `NOT RUN`, not
treated as pass evidence.

## Validation recorded on 2026-08-18

Static and build checks:

- `py -3 tools/ci/audits/audit-low-latency-contract.py`: PASS. The audit covers
  the pre-acquire readiness probe, the frame-ring no-op invariant, the screen
  retention key, invalidation boundaries, and the single-`VkPresentIdKHR`
  pNext-chain invariant.
- `cmd /c tools\build\windows\build-mingw-existing.bat --jobs 1`: PASS.
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir
  build\debug-mingw-x86_64 --jobs 1`: PASS; 13 registered tests passed.
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir
  build\debug-vulkan-telemetry --jobs 1`: PASS; the telemetry-enabled build
  completed with all 13 registered tests passing.
- `git diff --check`: PASS.

The matrix below retains the earlier telemetry measurements for comparison.
Those captures were produced before the current-tree telemetry executable was
rebuilt, so they are not used as exact-SHA evidence for the current worktree.

The telemetry-enabled runtime was previously executed on Windows with an NVIDIA GeForce
RTX 5070 Ti, Vulkan validation enabled, Reflex on, Anti-Lag 2 off, and three
swapchain images/two logical presenter slots. The checked variants were Vulkan
at 4x internal resolution with VSync off at 60/120/144/240 Hz, 16x at 240 Hz,
VSync on at 60 Hz, and Custom HUD plus scoreboard enabled at 4x/240 Hz. Every
run exited with code 0; the combined logs contained no `VUID-*`, device-lost,
`VK_ERROR_DEVICE_LOST`, or validation-error marker.

Representative phase-0 metrics from the final report windows were:

| Variant | Frames | `present_begin_total` p50/p95 | `present_image_fence` p50/p95 | Busy/wait/reuse/upload-skip |
| --- | ---: | ---: | ---: | ---: |
| 4x, 60 Hz, VSync off | 61 | 46.90/96.10 us | 17.50/61.80 us | 0/0/0/0 |
| 4x, 120 Hz, VSync off | 121 | 42.30/58.20 us | 15.70/26.50 us | 0/0/0/0 |
| 4x, 144 Hz, VSync off | 144 | 41.65/53.82 us | 15.45/22.88 us | 0/0/0/0 |
| 4x, 240 Hz, VSync off | 227 | 40.00/51.04 us | not emitted in final window | 0/0/0/0 |
| 16x, 240 Hz, VSync off | 223 | 39.20/49.27 us | not emitted in final window | 0/0/0/0 |
| 4x, 60 Hz, VSync on/FIFO | 60 | 48.35/104.56 us | not emitted in final window | 0/0/0/0 |
| 4x, 240 Hz, Custom HUD + scoreboard | 202 | 41.40/48.19 us | not emitted in final window | 0/0/0/0 |

The four final counter columns are, in order,
`presenter_slot_busy_skip_count`, `presenter_frame_fence_wait_count`,
`screen_frame_reuse_count`, and `screen_layer_upload_skip_count`. `screen_copy_B`
was `0` in the direct-output runs. The zero busy/reuse counts are an honest
result of this host: its tested workload generated a new renderer frame for
each presentation and did not fill the two-slot presenter ring. They verify
that the normal path has no accidental waits or reuse, but they are not a
stress proof of the busy-skip or same-renderer-frame branches.

The HUD/scoreboard run also exercised the live Custom HUD renderer path:
`hud_upload_B=116298672`, `scoreboard_raster` p50 about `635 us`, and
`visual_render=202`; it completed the Vulkan -> Software -> Vulkan transition
after loading savestate slot 4. The VSync-on run selected FIFO and reported
`vsync_enabled=1`, `present_mode=2`.

One validation-layer regression found during the run was a duplicate
`VkPresentIdKHR` in `VkPresentInfoKHR` when Reflex and the generic Vulkan
pacer were both active. `PreparePresent()` may already attach the legacy
present-id node; EndFrame now adds its Reflex node only when that node is not
already present. A fresh validation-layer run after the fix emitted no VUID.

The following physical gates remain outside this Windows environment and are
not claimed as pass evidence:

- A controlled GPU-saturation test that produces a nonzero busy-slot skip and
  a repeated same-renderer-frame retention count: `OPEN`.
- Linux runtime coverage: `NOT RUN`.
- macOS/Metal and BSD runtime coverage: `NOT RUN` because those platforms are
  unavailable on this Windows host.
- The instruction's Phase 3 nonblocking acquired-image fence path remains
  intentionally unimplemented; the current code keeps the existing blocking
  acquired-image lifecycle.

## Current-tree runtime smoke rerun

The current telemetry executable was rebuilt from the worktree and has SHA256
`04ACA95D4E5C259BE038AAC5E3956EFC9E004D73026A476327854473372BB6E4`.
Windows smoke runs used the same RTX 5070 Ti, validation enabled, Reflex on,
Anti-Lag 2 off, three swapchain images, and two logical presenter slots. Each
run loaded the `.ml4` savestate successfully, exited with code 0, wrote 60
capture PNGs, and had zero `VUID-*`, `VK_ERROR_DEVICE_LOST`, `DEVICE_LOST`,
`validation-error`, or fatal markers.

| Current-tree variant | Frames | `present_begin_total` p50/p95 | Busy/wait/reuse/upload-skip |
| --- | ---: | ---: | ---: |
| 4x, 60 Hz target, VSync off | 60 | 49.60/60.11 us | 0/0/0/0 |
| 4x, 120 Hz target, VSync off | 121 | 40.80/53.80 us | 0/0/0/0 |
| 4x, 144 Hz target, VSync off | 145 | 40.50/50.48 us | 0/0/0/0 |
| 4x, 240 Hz target, VSync off | 227 | 40.20/48.50 us | 0/0/0/0 |
| 16x, 240 Hz target, VSync off | 223 | 40.80/49.28 us | 0/0/0/0 |
| 4x, 60 Hz target, VSync on/FIFO | 60 | 47.45/106.97 us | 0/0/0/0 |
| 4x, 240 Hz target, Custom HUD + scoreboard | 201 | 42.00/50.40 us | 0/0/0/0 |

The HUD/scoreboard smoke reported `hud_upload_B=145298880`,
`scoreboard_raster` p50 about 653 us, and `visual_render=201`; the HUD path
was therefore active in the current binary. The current VSync run selected
FIFO (`present_mode=2`, `vsync_enabled=1`), while the VSync-off runs selected
IMMEDIATE. All current runs reported `screen_copy_B=0` and
`acquire_not_ready_count=0`.

The zero busy/reuse values mean the available workload did not fill the
two-slot presenter ring and did not deliver repeated presentations of one
renderer frame. They are not stress proof of those branches; controlled
GPU-saturation and repeated same-frame runtime evidence remain `OPEN`.
