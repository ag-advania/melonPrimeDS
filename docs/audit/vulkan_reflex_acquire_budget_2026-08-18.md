# Vulkan Reflex Acquire budget audit — 2026-08-18

This is the implementation and runtime record for the bounded
`vkAcquireNextImageKHR` policy used when the Vulkan presenter bypasses its
present-slot wait. It supplements the long-form low-latency design record in
[`vulkan-low-latency.md`](../development/performance/vulkan-low-latency.md).

## Source contract

Implementation commit: `82379a7db` (`perf(vulkan): bound low-latency acquire wait`).
Contract/test commit: `34f97530b` (`test(vulkan): audit bounded low-latency acquire contract`).

The acquired-image P0 contract is unchanged: there is no `ImagesInFlight`
array and no post-Acquire host fence wait. Frame-slot fences protect command
buffers, staging, descriptors, and leases; `ImageAvailable` remains per frame
slot; `RenderFinished` remains per real swapchain image; and the render-pass
external dependency orders the acquired-image transition.

The two Acquire policies are deliberately separate and cached on first use:

| Path | Environment variable | Default | Runtime meaning |
| --- | --- | ---: | --- |
| Normal presenter-slot wait | `MELONPRIME_VULKAN_ACQUIRE_TIMEOUT_NS` | Windows `UINT64_MAX` | Existing blocking/normal policy |
| Low-latency presenter-slot bypass | `MELONPRIME_VULKAN_LOW_LATENCY_ACQUIRE_TIMEOUT_NS` | `500000 ns` | Finite A/B budget |

The low-latency experiment accepts `0`, `250000`, `500000`, and `1000000` ns.
No `getenv()` call occurs in the frame path. A `VK_TIMEOUT` or `VK_NOT_READY`
result sets `LastBeginLatencySkip`, increments the low-latency and existing
latency-budget skip counters, submits an empty FrameRing frame, and returns
without an `ImageAvailable` wait or swapchain/device failure classification.

The telemetry fields used below are:

* `acquire_timeout_ns`
* `acquire_wait_count`, `acquire_wait_ns`
* `acquire_low_latency_attempt_count`, `acquire_low_latency_skip_count`
* `acquire_repeat_image_index_count`
* `acquire_not_ready_count`, `present_skipped_for_latency_budget_count`

`acquire_repeat_image_index_count` counts consecutive successful acquisitions
returning the same image index. It is a validation-only probe and is not queue
ownership-transfer telemetry.

## Fixed machine and run procedure

| Field | Recorded value |
| --- | --- |
| Source implementation | `82379a7db` plus contract audit `34f97530b` |
| Executable SHA256 | `0E9410B8D75CDDFCD0EB94570F1F2BCB8E682DE2746E80EB144EAEAE6DAD75E4` |
| Build | Windows Debug, renderer telemetry enabled, Vulkan validation enabled |
| OS | Windows 11 Home, `10.0.22621`, build `22621` |
| GPU / driver | NVIDIA GeForce RTX 5070 Ti / `610.74.0.0` |
| Vulkan | device API `1.4.341`, instance API `1.4.357` |
| Current monitor mode | `2560x1440`, `540 Hz` reported by Windows |
| Presentation | windowed, Vulkan, 4x internal resolution, two logical presenter slots |
| ROM / state | Metroid Prime Hunters USA Rev 1, `.ml4` savestate slot 4 |
| Telemetry | `MELONPRIME_PERF=1`; one fresh process per timeout value |
| Validation result | marker present in every run; zero `VUID-*`, `SYNC-HAZARD`, device-lost, and fatal markers |

Each capture runner exited with code 0 and wrote the requested PNG captures.
The 144/240 rows below are TargetFPS controls on the same 540 Hz display, not
claims that a separate physical 144/240 Hz monitor mode was selected.

## Vulkan matrix

Acquire and Begin values are `p50/p95/p99/max` in microseconds from the final
VulkanPerf window. `attempts/skips` is the low-latency counter pair; normal
Reflex-off Acquire intentionally has zero low-latency attempts.

| Variant | Images | Target / Reflex / VSync | Acquire timeout | Acquire wait | Begin total | Attempts / skips |
| --- | ---: | --- | ---: | --- | --- | ---: |
| Default | 3 | 60 / ON / OFF | 500000 ns | 9.70/12.90/13.72/13.90 | 35.30/44.90/56.70/65.40 | 61 / 0 |
| A/B zero | 3 | 60 / ON / OFF | 0 ns | 5.80/8.70/9.34/10.00 | 28.20/33.40/37.58/38.90 | 61 / 0 |
| A/B 250 us | 3 | 60 / ON / OFF | 250000 ns | 5.90/9.70/10.34/10.40 | 27.90/36.20/36.74/36.80 | 61 / 0 |
| A/B 500 us | 3 | 60 / ON / OFF | 500000 ns | 6.70/9.80/10.18/10.30 | 28.80/35.60/38.74/39.10 | 61 / 0 |
| A/B 1 ms | 3 | 60 / ON / OFF | 1000000 ns | 6.10/9.60/11.07/11.60 | 27.70/35.91/36.57/37.40 | 60 / 0 |
| High target | 3 | 144 / ON / OFF | 500000 ns | 5.00/8.22/9.25/9.90 | 24.60/31.86/34.52/36.40 | 145 / 0 |
| High target | 3 | 240 / ON / OFF | 500000 ns | 4.80/7.37/8.78/9.90 | 23.40/30.60/33.17/35.30 | 224 / 0 |
| Two-image experiment | 2 | 60 / ON / OFF | 500000 ns | 5.60/9.30/9.68/9.80 | 27.30/34.30/40.64/48.80 | 61 / 0 |
| Reflex control | 3 | 60 / OFF / OFF | Windows `UINT64_MAX` | 6.00/10.30/16.54/24.70 | 27.80/41.20/54.22/64.60 | 0 / 0 |
| VSync control | 3 | 60 / ON / ON | 500000 ns | 5.90/9.80/10.46/10.70 | 28.20/35.80/39.04/40.30 | 61 / 0 |

All Vulkan rows reported `acquire_not_ready_count=0` and
`present_skipped_for_latency_budget_count=0`. The successful-acquire
repeat-image probe matched the successful sample count in these runs; that is
reported only as an observation of this workload and is not interpreted as a
queue-ownership transfer or as proof that the skip branch is unreachable.

The Custom HUD + scoreboard-on control used the same 3-image, 60-target,
Reflex-on, VSync-off conditions. It exited successfully with validation marker
and zero error markers, and reported:

* `hud_upload_B=147091008`
* `scoreboard_raster` p50 `642.5 us`
* `visual_render=61`, `uploads=61`
* `acquire_timeout_ns=500000`, low-latency attempts/skips `61/0`

This confirms that the Acquire measurement also covers the live HUD/compositor
path rather than only a HUD-disabled smoke scene.

## DX12 comparison

The same Debug/developer telemetry executable was run with `3D.Renderer = 4`,
Reflex ON, VSync OFF, 4x, 60-target, the same ROM/state and 80 captures.
Renderer initialization succeeded and the process exited with code 0. Its
native `MelonPrimePerf` shutdown summary was:

```text
frames=881 frame_ms p50=16.670 p95=17.601 p99=18.680 max=586.054
```

This is a renderer-health and configuration comparison only. DX12 does not
emit Vulkan Acquire counters, and this Debug/developer build is not the
shipping A/B build required for a cross-backend latency-parity claim.

## Gates and status

| Gate | Status | Reason |
| --- | --- | --- |
| Static bounded-Acquire contract | PASS | `audit-low-latency-contract.py` |
| Normal path unchanged | PASS | Reflex OFF row uses `UINT64_MAX`, low attempts 0 |
| Low-latency 0/250/500/1000 us selection | PASS | Fresh-process telemetry values match each override |
| Timeout/not-ready semantics | PASS in unit/contract coverage | No physical run produced a skip |
| 3-image default | KEEP | Surface-authorized default; no promotion change |
| 2-image experiment | OPEN | One Windows/NVIDIA workload only |
| Controlled nonzero skip-rate stress | OPEN | No GPU-saturation/forced-not-ready workload yet |
| Reflex Boost | NOT RUN | Only Reflex ON/OFF was exercised |
| DX12 latency parity | NOT CLAIMED | Native metrics are not Acquire-equivalent |
| Linux / AMD / fullscreen / physical 144/240 mode | NOT RUN | Not covered by this Windows run |

The implementation is therefore source/test complete for the bounded policy,
but the physical promotion gates remain explicitly open rather than being
inferred from the zero-skip smoke workload.
