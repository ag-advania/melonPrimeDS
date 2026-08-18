# Vulkan low-latency presentation

This document records the implementation corresponding to the Vulkan
low-latency instruction dated 2026-08-18. The presenter remains synchronized;
the optimization is allowed to drop a presentation callback, never to reuse a
busy slot or an acquired swapchain image. The acquired-image synchronization
contract is documented separately from the frame-slot admission contract so a
zero image-fence counter cannot be mistaken for an unmeasured wait.

## Phase 0: telemetry contract

The renderer-performance build reports the low-latency measurements required
by the instruction without adding state or timing work to a normal build. The
CPU samples cover presenter-begin, presenter-slot fence wait, swapchain image
acquire, HUD upload, and queue submit. The legacy `present_image_fence` metric
remains in the report schema as a required zero counter after the acquired-image
host wait was removed. The other counters cover busy-slot skips,
acquired-image `VK_NOT_READY`, latency-budget skips, screen-frame reuse,
screen-layer upload skips, direct/buffer output selection, swapchain image
count, logical presenter depth, present mode, VSync, and vendor latency mode.
GPU timestamps cover the raster/compositor/presenter stages when the telemetry
build can create timestamp query pools.

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
replacement, submit, and present. Only telemetry counters are updated.
Blocking mode continues to use the existing bounded frame-slot fence wait.

The frame ring does not advance `CurrentIndex` until the readiness probe and
command-buffer recording boundary have succeeded. This is the invariant that
makes a busy skip a no-op for frame-ring state.

### P0: acquired-image host fence removal

The old `ImagesInFlight` array and its post-acquire `vkWaitForFences` were
removed in commit `05a18fb7b`. The removal is safe because the static proof
found no mutable application resource indexed by the acquired swapchain image:

1. `SwapchainImages`, their image views, and their framebuffers are immutable
   between swapchain recreations. Recreation drains the device before those
   objects are destroyed.
2. `RenderFinished` is the only synchronization object indexed by the acquired
   image. It has one semaphore per real swapchain image and is selected with
   `RenderFinished[CurrentImageIndex]`.
3. `ImageAvailable`, command buffers, staging storage, transient descriptors,
   frame leases, and frame fences remain owned by the frame-slot ring. Deferred
   destruction is keyed by the submitted frame number, not by an image index.
4. `vkAcquireNextImageKHR` returns the image and signals the frame-slot
   `ImageAvailable` semaphore. The submit waits for that semaphore at
   `COLOR_ATTACHMENT_OUTPUT`; the render-pass external dependency orders the
   implicit acquired-image layout transition at the same stage.
5. No screenshot/readback or other deferred callback retains a swapchain image
   through `ImagesInFlight`. The `present_image_fence` telemetry field is kept
   only to expose a zero post-removal measurement and compatibility with older
   report parsers.

Consequently, the normal path performs no host wait after acquire. The submit
waits on the per-slot `ImageAvailable`, signals the per-image `RenderFinished`,
and `vkQueuePresentKHR` waits on that per-image semaphore. Reuse is therefore
ordered by the next acquire of the same swapchain image rather than by a
second CPU fence wait.

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

### Phase 3: bounded low-latency swapchain Acquire

The low-latency presenter path now gives `vkAcquireNextImageKHR` its own
bounded policy. The existing normal path is unchanged:

| Presenter path | Timeout source | Default on Windows | Evaluation |
| --- | --- | ---: | --- |
| `waitForPresentSlot=true` | `MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS` | `UINT64_MAX` | cached once |
| `waitForPresentSlot=false` | `MELONPRIME_VULKAN_LOW_LATENCY_ACQUIRE_TIMEOUT_NS` | `500000 ns` | cached once |

The low-latency override is an A/B control. Values `0`, `250000`, `500000`,
and `1000000` ns are accepted; the environment is not read from the frame
loop. The initial default is the finite 500 us candidate, not an unconditional
zero, so a real driver/display comparison can choose the final budget.

When a bounded low-latency Acquire returns `VK_TIMEOUT` or `VK_NOT_READY`, no
swapchain image or `ImageAvailable` signal exists. The presenter marks
`LastBeginLatencySkip`, submits the empty logical frame-ring frame, retires the
logical frame, and retries on the next callback. The result is not classified
as surface loss, device loss, or swapchain recreation. The existing
`EmuThread` low-latency finish boundary therefore still closes the Reflex
logical frame. No post-success image fence wait, `vkDeviceWaitIdle`, sleep, or
spin was added.

Telemetry adds the selected `acquire_timeout_ns`, low-latency attempt/skip
counters, and `acquire_repeat_image_index_count`. The last field counts
consecutive successful acquisitions returning the same swapchain image index;
it is a validation probe and explicitly is **not** queue-ownership telemetry.
The existing Acquire wait, not-ready, and latency-budget skip counters remain
unchanged.

## Deferred experiments and gates

The optional two-image swapchain experiment is available only through
`MELONPRIME_VULKAN_SWAPCHAIN_IMAGE_COUNT=2`. The default remains
`minImageCount + 1`; the surface minimum/maximum remains authoritative. It is
not promoted by source inspection alone. Promotion requires same-machine A/B
evidence for latency, p95/p99, starvation/errors, fullscreen/windowed mode,
NVIDIA/AMD, and FIFO/IMMEDIATE/MAILBOX where available.

The direct-descriptor generation transition still has a rare `Frames.WaitIdle`
when the renderer publishes a new resource lifetime. Moving that wait to a
single renderer-transition boundary is deferred because resource recreation
also occurs at resolution-dependent renderer lifecycle boundaries; removing it
without proving that ordering would reintroduce use-after-destroy risk. It is
not a steady-state same-generation path.

The instruction's 60/120/144/240 Hz, VSync on/off, HUD, internal-resolution,
and lifecycle matrix must be recorded per available physical environment.
Unavailable refresh rates or platforms are recorded as `OPEN` / `NOT RUN`, not
treated as pass evidence.

## Validation recorded on 2026-08-18

Static and build checks:

- `py -3 tools/ci/audits/audit-low-latency-contract.py`: PASS. The audit covers
  the pre-acquire readiness probe, the cached low-latency Acquire timeout and
  timeout-skip retirement contract, the frame-ring no-op invariant, the screen
  retention key, invalidation boundaries, the single-`VkPresentIdKHR`
  pNext-chain invariant, and the acquired-image no-host-fence proof.
- `cmd /c tools\build\windows\build-mingw-existing.bat --jobs 1`: PASS.
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir
  build\debug-mingw-x86_64 --jobs 1`: PASS; 13 registered tests passed.
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir
  build\debug-vulkan-telemetry --jobs 1`: PASS; the telemetry-enabled build
  completed with all 13 registered tests passing.
- `git diff --check`: PASS.

The matrix below retains pre-P0 telemetry measurements for comparison. Those
captures used the former acquired-image host fence path and are not evidence
for the current post-P0 behavior; in particular, their nonzero
`present_image_fence` values are historical.

The pre-P0 telemetry-enabled runtime was previously executed on Windows with an NVIDIA GeForce
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

One validation-layer regression found during the pre-P0 run was a duplicate
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
- The complete post-P0 refresh/VSync/window-mode/renderer-switch matrix remains
  `OPEN` where an exact current-binary run was not repeated below. The
  available 3-image and 2-image smoke evidence is recorded separately.

## Pre-P0 current-tree runtime smoke reference

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

## Post-P0 exact-current-binary runtime smoke

After removing the acquired-image host fence wait, the telemetry executable
was rebuilt from the exact current source tree. Its SHA256 is
`4D3FFF627F0541C0BED4DE4711E27BE53ED14A1A2EF9657DF4D114C3F2333399`.
Both runs used Windows, an NVIDIA GeForce RTX 5070 Ti, Vulkan validation
enabled, Reflex on, Anti-Lag 2 off, 4x internal resolution, VSync off,
windowed presentation, the same ROM and `.ml4` savestate slot 4, and two
logical presenter slots. The `.ml4` was opened successfully and each run
exited with code 0 after writing 60 capture PNGs.

| Swapchain | Captures | `present_begin_total` p50/p95 (n) | `present_image_fence` p50/p95/p99/max (n) | Key counters |
| --- | ---: | ---: | ---: | --- |
| Default, 3 images | 60 | 28.90/40.10 us (61) | 0/0/0/0 us (0) | image-fence wait 0/0 ns, busy 0, `screen_copy_B=0`, `acquire_not_ready=0` |
| Experiment, 2 images | 60 | 30.55/38.24 us (60) | 0/0/0/0 us (0) | image-fence wait 0/0 ns, busy 0, `screen_copy_B=0`, `acquire_not_ready=0` |

The 3-image report selected `swapchain_image_count=3`, and the experiment
selected `swapchain_image_count=2`; both reports also showed
`presenter_frame_fence_wait_count=0`, `presenter_frame_fence_wait_ns=0`, and
`direct_image_frames` equal to the telemetry sample count. Combined stdout and
stderr contained the validation-layer enable marker and no `VUID-*`,
`SYNC-HAZARD`, `VK_ERROR_DEVICE_LOST`, `DEVICE_LOST`, or fatal marker.

The 2-image path therefore passed this bounded synchronization smoke test,
but it is not promoted as the default. The instruction requires same-machine
A/B latency and p95/p99 coverage across the relevant VSync/present-mode,
window/fullscreen, refresh-rate, and available vendor matrix; this run covers
only one Windows/NVIDIA workload. The default remains the surface-authorized
3-image policy until that broader gate is closed.

## Post-P1 bounded-Acquire runtime evidence

The exact telemetry executable used for the post-P1 runs has SHA256
`0E9410B8D75CDDFCD0EB94570F1F2BCB8E682DE2746E80EB144EAEAE6DAD75E4`.
The detailed run record is
[`docs/audit/vulkan_reflex_acquire_budget_2026-08-18.md`](../../audit/vulkan_reflex_acquire_budget_2026-08-18.md).
It covers the same Windows/NVIDIA machine, ROM, `.ml4` slot 4, windowed 4x
Vulkan workload, validation marker, and no VUID/device-lost/fatal markers.

The 3-image A/B selected every requested timeout value in a fresh process.
At 60-target FPS the low-latency path recorded 61 attempts and zero skips for
each value; the 144/240-target runs recorded 145/224 attempts and zero skips.
The 2-image experiment also recorded 61 attempts and zero skips, so it remains
an experiment rather than a promoted default. The 500 us VSync-on run selected
FIFO (`present_mode=2`, `vsync_enabled=1`). A Custom HUD + scoreboard-on run
reported `hud_upload_B=147091008`, `scoreboard_raster` p50 642.5 us, and 61
low-latency Acquire attempts.

The available workload did not produce a real `VK_TIMEOUT`/`VK_NOT_READY`
Acquire skip, so the runtime stress gate for nonzero skip rate remains
`OPEN`; the fake/contract tests and source audit cover the skip semantics. A
DX12 run is recorded as a renderer-health comparison only: its Debug/developer
build and native `frame_ms` report are not a Vulkan Acquire-latency parity
claim. Reflex Boost, Linux/AMD, fullscreen, and a controlled GPU-saturation
skip run remain `NOT RUN`/`OPEN`.

## Post-P2/P3 exact-current-binary follow-up

The P2/P3 source fixes and the exact current-binary follow-up are recorded in
[`vulkan_reflex_acquire_budget_followup_2026-08-18.md`](../../audit/vulkan_reflex_acquire_budget_followup_2026-08-18.md).
P2 moves `LastBeginLatencySkip` into the low-latency Acquire timeout branch so
the normal blocking Acquire path cannot be misclassified. P3 rejects negative
timeout environment values and uses the cached fallback; both fixes have
source audits/build tests and separate phase commits.

The follow-up adds exact-SHA A/B, physical 60/240/540 Hz runs, a Release
telemetry run, a same-build DX12 comparison, and an event-matrix run with core
and Synchronization Validation. The current Windows/NVIDIA workload remained
zero-skip, so true Acquire skip/recovery and confirmed fullscreen remain
explicitly `OPEN`; the result is not promoted by inference from clean smoke
runs.
