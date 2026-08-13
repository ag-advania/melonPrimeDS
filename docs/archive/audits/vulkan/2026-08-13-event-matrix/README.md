# Phase 1 event matrix — 2026-08-13

Window-driven part of the present-pacing runbook's Phase 1, executed with
`tools/testing/vulkan-present-event-matrix.ps1` against a Debug build with
`VK_LAYER_KHRONOS_validation` enabled. RTX 5070 Ti, driver 610.74.0.0, loader
1.4.357.

## Result after the fix

| Phase | Swapchain rebuilds | Validation | Device lost |
|---|---:|---|---:|
| resize x40 | 41 | clean | 0 |
| minimize/restore x20 | 22 | clean | 0 |
| fullscreen x8 | 8 | clean | 0 |
| idle control | 2 | clean | 0 |

All four phases exit 0. Logs: `*-after-fix.log`.

The minimize run is not a vacuous pass: the queue-full retry path it exercises
fired **9 times** in that same clean run
(`present timing results queue full; retrying present without optional timing
metadata`).

## Current-tree Synchronization Validation follow-up

The current tree was then run with the reproducible harness gate:

```powershell
pwsh -NoProfile -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 `
  -Rom C:\DSMPH\melonPrimeDS最新版\balancedRom.nds `
  -BuildDir build\debug-mingw-vulkan-validation2 `
  -Phase minimize -ValidateSync -Tag min-sync-final2 -WarmupSeconds 18 `
  -OutDir docs\archive\audits\vulkan\2026-08-13-event-matrix
```

Result: 26 rebuilds, validation banner confirmed with **Synchronization**,
VUID 0, `SYNC-HAZARD` 0, `DEVICE_LOST` 0, and exit 0. The run recorded 22
queue-at-capacity pauses and 10 queue-growth recoveries (16 -> 32), with
`queue-full errors=0`.

The first current-tree Sync attempt found 20
`SYNC-HAZARD-WRITE-AFTER-PRESENT` messages when the queue-full retry was
re-presenting the same image. The follow-up now pauses timing metadata before
the results queue is full and counts only complete timing reports as released
slots. The final logs are `vk-min-sync-final2.out.log` and
`vk-min-sync-final2.err.log`; the failed diagnostic run is retained as
`vk-min-sync-current.out.log` / `.err.log`.

The earlier `min-sync-final2` result is valid only for the configuration it
actually ran (`TelemetryOnly` / Reflex On / VSync Off). The harness was then
fixed to write the portable config root, verify the effective runtime state,
and restore both config files byte-for-byte. The intended current-tree gate was
rerun with `Policy=2`, `ReflexMode=0`, VSync on and saved as
`vk-min-sync-configfix3.out.log` / `.err.log`, with the harness summary in
`vk-min-sync-configfix3.harness.log`:

```text
config path: build/debug-mingw-vulkan-validation2/portable/melonDS.toml
config self-check: policy=JustInTime reflex=off/inactive vsync=on present-mode=FIFO
config restore: PASS; layer restore: PASS
swapchain rebuilds: 23
VUID: 0; SYNC-HAZARD: 0; DEVICE_LOST: 0; exit: 0
queue-at-capacity: 13; queue growth: 4 (16 -> 32); queue-full errors: 0
```

The harness self-check also passed short idle controls for the adjacent
configurations:

```text
Policy0 / ReflexOff / VSyncOn: 2 rebuilds, banner confirmed, VUID 0,
  SYNC-HAZARD 0, DEVICE_LOST 0, config/layer restore PASS
Policy2 / ReflexOn / VSyncOn: 2 rebuilds, banner confirmed, VUID 0,
  SYNC-HAZARD 0, DEVICE_LOST 0, config/layer restore PASS
```

Their raw logs and harness summaries are `vk-sync-policy0-short.*` and
`vk-sync-reflexon-short.*`.

## The defect that was open, and its cause

```text
VUID-vkQueuePresentKHR-pWaitSemaphores-03268
vkQueuePresentKHR(): pPresentInfo->pWaitSemaphores[0] ... is waiting on
semaphore ... that has no way to be signaled.
```

First measured at 20 messages over 22 rebuilds in the minimize/restore phase,
0 over 42 in resize, intermittent at idle (`minimize-vuid-after.log`,
`resize-clean.log`).

**Cause: `VulkanPresentPacer::PrepareRetryWithoutTiming` retried the present on
the same wait semaphore.** When the optional present-timing results queue fills,
`vkQueuePresentKHR` rejects the presentation with
`VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT` and the presenter retries once with the
timing chain removed. The rejected call does not enqueue the image or consume the
present ID — but it *does* enqueue the semaphore wait operations it was given.
The retry therefore asked for a second signal that nothing would ever produce.

The fix drops `waitSemaphoreCount`/`pWaitSemaphores` on the retry. Ordering
survives: the wait the first call enqueued is already ahead of the retry on the
same present queue, and queue operations start in submission order, so the
retried presentation still follows the rendering it depends on.

The retry remains as a defensive fallback, but normal queue pressure is handled
before `vkQueuePresentKHR` reaches the full queue. This avoids turning the
rejected retry into a second present write under Synchronization Validation.

It looked like a swapchain-lifecycle bug only because minimize/restore is what
made the timing queue overflow often enough to be visible.

### How it was found

`minimize-vuid-rootcause-trace.log` is the trimmed diagnostic trace from
temporary instrumentation (removed before commit) logging the acquire result,
image index, submit signal semaphore and present result. It shows the VUID pairs
landing immediately after the one present in each cycle that returned
`-1000208000`, and it shows `img=0` on every acquire — the swapchain hands back
the same image index every frame, so `RenderFinished[]` is effectively a single
semaphore and the double-wait is unambiguous.

### Earlier findings, kept for the record

* **Not caused by the present-marker change.**
  `minimize-vuid-before-lock-change.log` is the same phase against the commit
  before the present markers moved inside the queue lock (`33c4c6849`): 20 over
  24, matching 20 over 22 after. The defect predated it.
* **Disproved hypothesis.** `minimize-vuid-with-disproved-fix.log` records an
  attempted fix that held the render-finished semaphores in a retired list and
  destroyed them only after the old swapchain. It changed nothing — still 20 over
  22 — and was reverted rather than shipped. Semaphore *destruction* ordering was
  never the issue; semaphore *reuse within one frame* was.

## Regression protection

`tools/ci/audits/audit-low-latency-contract.py` requires the retry path to clear
both `present.waitSemaphoreCount` and `present.pWaitSemaphores`, and requires
the capacity guard plus complete-report accounting that keeps the retry path
from being the normal queue-pressure route.

## Reproducing

```bash
tools\build\windows\build-mingw-validation.bat --jobs 1
```

```bash
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 -Rom <rom> -Phase minimize -Tag min1
```

The script exits non-zero and prints the VUIDs grouped by count.

## Not covered by this automation

DPI change, Video Settings open/cancel/apply, renderer switching
(Vulkan ↔ Software / OpenGL Compute / DX12) and ROM lifecycle
(launch/savestate/reset/close/reopen) are still manual.
