# Phase 1 event matrix — first automated run, 2026-08-13

Window-driven part of the present-pacing runbook's Phase 1, executed with
`tools/testing/vulkan-present-event-matrix.ps1` against a Debug build with
`VK_LAYER_KHRONOS_validation` enabled. RTX 5070 Ti, driver 610.74.0.0, loader
1.4.357.

## Result

| Phase | Swapchain rebuilds | `VUID-...-03268` | Other VUIDs | Device lost |
|---|---:|---:|---:|---:|
| resize x40 | 42 | **0** | 0 | 0 |
| minimize/restore x20 | 22 | **20** | 0 | 0 |
| idle control | 2 | 0-2 (intermittent) | 0 | 0 |

Fullscreen x8 was also exercised in a combined run with no additional VUID class.
No device loss, no software fallback, no recreate storm in any phase.

## The open defect

```text
VUID-vkQueuePresentKHR-pWaitSemaphores-03268
vkQueuePresentKHR(): pPresentInfo->pWaitSemaphores[0] ... is waiting on
semaphore ... that has no way to be signaled.
```

Roughly one per minimize/restore cycle. What the phase separation tells us:

* It is **not swapchain recreation in general** — resize rebuilds the swapchain
  42 times without a single message.
* It is the **zero-extent path** taken while the window is minimized, where
  `RecreateSwapchain` returns early without creating anything and the frame is
  skipped, then recreation happens for real on restore.
* At idle it fires occasionally against the two startup rebuilds, which is why
  it was first seen as a flaky one-in-four.

### Not caused by the present-marker change

`minimize-vuid-before-lock-change.log` is the same phase run against the commit
*before* the present markers were moved inside the queue lock
(`33c4c6849`): **20 messages over 24 rebuilds**, matching the 20 over 22 after
the change. The defect predates it.

### Disproved hypothesis

`minimize-vuid-with-disproved-fix.log` records an attempted fix: hold the
render-finished semaphores back in a retired list and destroy them only after
the old swapchain has been destroyed, on the theory that a queued present still
held a wait on them and `vkDeviceWaitIdle` does not retire a present whose
swapchain went out of date.

**It changed nothing — still 20 over 22.** The change was reverted rather than
shipped. Whatever the cause is, the semaphore-destruction ordering alone is not
it, and the next investigation should start somewhere else. Likely next step:
log the image index and semaphore handle at each submit and present and
correlate them with the VUID lines, since the message names the specific
semaphore.

## Reproducing

```bash
tools\build\windows\build-mingw-validation.bat --jobs 1
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 -Rom <rom> -Phase minimize -Tag min1
```

The script exits non-zero and prints the VUIDs grouped by count.
