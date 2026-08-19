# Vulkan Reflex Acquire budget follow-up — 2026-08-18

This is the post-P2/P3 follow-up to
[`vulkan_reflex_acquire_budget_2026-08-18.md`](vulkan_reflex_acquire_budget_2026-08-18.md).
It records the exact current binaries, physical refresh-rate runs, the
window-event validation run, and the remaining gates. A zero-skip workload is
not treated as proof that the `VK_TIMEOUT`/`VK_NOT_READY` branch is unreachable.

## Source phases and exact revisions

The source fixes were intentionally committed as separate phases:

| Phase | Commit | Change | Result |
| --- | --- | --- | --- |
| P2 | `6e23f76edea74f3fd60c8dbbf862f112c83601f5` | Classify `LastBeginLatencySkip` only inside the low-latency Acquire timeout branch; tighten the static audit to inspect that branch | PASS |
| P3 | `ee3bfd15c5dfb980ed86c6d9135fb9bcd8e77208` | Reject a leading `-` in the cached timeout environment parser and cover the fallback in the timeout unit test | PASS |
| P3 cleanup | `5c77069b8ba0e532c268e41c87619accb7790ddd` | Count `VulkanPresentSkippedForLatencyBudgetCount` only for low-latency Acquire skips; retain the all-outcome `VulkanAcquireNotReadyCount`; strengthen the block audit and normal negative override coverage | PASS |

The implementation parent is `f6d17a4ac06c5baa3f7a9f0c9b9d8628850eb74e`.
The historical A/B and refresh-rate binaries below were built from
`ee3bfd15c5dfb980ed86c6d9135fb9bcd8e77208`. The current source after the
telemetry cleanup is `5c77069b8ba0e532c268e41c87619accb7790ddd`.

P2 is required for correctness: a normal presenter Acquire timeout must not
become an intentional low-latency idle result. P3 is defensive input handling:
negative values now use the documented fallback instead of entering numeric
conversion with an invalid sign. The follow-up P3 cleanup makes the
latency-budget skip counter match its name without changing synchronization,
frame submission, or user-visible behavior.

## Build and static evidence

| Artifact | Configuration | SHA256 | Validation |
| --- | --- | --- | --- |
| A baseline | historical post-P0 Debug telemetry binary, source phase `8438e8278` | `4D3FFF627F0541C0BED4DE4711E27BE53ED14A1A2EF9657DF4D114C3F2333399` | historical baseline; no P2/P3 fix |
| B historical | Debug, Vulkan + renderer telemetry, validation enabled | `8A606BD3B035B2F7D512628967EC481775CF56F572279769C0307BCECE987EDC` | P2/P3 source before the telemetry-only cleanup |
| P3 cleanup Debug | Debug, Vulkan + renderer telemetry | `C6411AF6A5D9EB814DC4A4BAC9EA2DCBFF26ED3816EE6C351EBF614762CBC91C` | current source; full configured build PASS |
| Release historical | Release, developer features off, Vulkan + renderer telemetry | `387BE30BD95CD50CBE6B66886AFF2CDE94C188F9F70A0789FD4DDD52AE24E006` | pre-cleanup shipping-like binary; validation not enabled |

The following checks passed against the current source:

- `py -3 tools/ci/audits/audit-low-latency-contract.py`
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\debug-mingw-x86_64 --jobs 1`
- `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\debug-vulkan-telemetry --jobs 1`
- Debug `melonprime_vulkan_presenter_timeout_tests.exe`, including normal and low-latency negative overrides
- The configured build suites, including the 82 registered-language Classic On-Screen Edit geometry cases

The current Debug build completed all 16 configured steps and its timeout,
present timing, surface lifecycle, fake-dispatch, renderer fallback, XeLL,
and 82-language layout checks passed. A Release rebuild was attempted with
the same existing-tree command, but the environment repeatedly failed to load
MSYS2 `sh.exe` during the link/LTO phase (`CANNOT_OPEN_SHARED_OBJECT_FILE`);
there is no valid current Release binary from this attempt. The previous
Release hash above remains historical evidence only, and a current Release
validation is `OPEN` for this host environment.

The current Debug executable also completed a short physical Vulkan smoke
with the ROM used by the preceding matrix, Reflex On, Just-in-Time pacing,
VSync On, two-frame logical depth, and a `0 ns` low-latency override. The
process exited normally and emitted the following final-window counters:

```text
acquire_low_latency_attempt_count=60
acquire_low_latency_skip_count=0
acquire_not_ready_count=0
present_skipped_for_latency_budget_count=0
```

This smoke used the renderer performance log rather than a formal CSV capture
(`capture_rows=0`), so it is runtime smoke evidence and not a new A/B result.

## Fixed machine and common runtime conditions

| Field | Value |
| --- | --- |
| OS | Windows 11 Home, `10.0.22621`, build `22621` |
| GPU | NVIDIA GeForce RTX 5070 Ti |
| Driver | runtime record `610.74.0.0`; Windows display driver `32.0.16.1074` |
| Vulkan | device API `1.4.341`, instance API `1.4.357` |
| Output | `2560x1440`, physical refresh changed to 60/240/540 Hz per run and restored to 540 Hz |
| ROM/state | Metroid Prime Hunters USA Rev 1, same `.ml4` savestate slot 4 |
| Renderer | Vulkan, 4x internal resolution, Custom HUD + scoreboard enabled |
| Common A/B controls | TargetFPS 240, VSync ON, Reflex ON, two-image experiment, low-latency timeout env `0 ns`, `MELONPRIME_PERF=1` |
| Capture | fresh process, 60 PNG captures, process cleanup after the capture window |

The old A binary does not contain the current low-latency timeout counter
fields; its Acquire and Begin metrics are retained as the baseline measurements.

## Exact-SHA A/B at physical 60 Hz

Both binaries used the same ROM/state, configuration, physical 60 Hz mode,
VSync/Reflex settings, two-image request, HUD/scoreboard configuration, and
capture procedure. The environment override was also present for A, but A
predates that override and therefore ignored it.

Values are `p50/p95/p99/max` in microseconds from the final telemetry window.

| Binary | Samples/captures | `present_begin_total` | `present_acquire` | Low-latency attempts/skips/not-ready | `frame_ms` shutdown p50/p95/p99/max |
| --- | ---: | --- | --- | --- | --- |
| A `4D3FFF…3399` | 60 / 60 | 28.40/37.96/44.26/45.20 | 7.90/10.40/11.96/12.20 | legacy counters unavailable; not-ready 0 | 16.660/19.505/23.987/405.166 |
| B `8A606B…87EDC` | 60 / 60 | 25.60/34.53/39.25/44.80 | 5.00/7.81/8.72/8.90 | 60/0/0 | 16.654/19.329/21.581/361.246 |

The differences are observations from one host/run pair, not a promotion or
causal performance claim. The important correctness result is that the B
normal/low-latency classification and timeout counters remain internally
consistent under the exact current binary.

## Physical refresh-rate matrix on current Debug B

All three runs changed the Windows display mode before launch and restored the
original 540 Hz mode in a `finally` path. Each wrote 60 captures and exited
through the capture runner without a fatal/device-lost marker.

| Physical mode | VSync / TargetFPS | Samples in final telemetry window | `present_begin_total` p50/p95/p99/max us | `present_acquire` p50/p95/p99/max us | Attempts/skips/not-ready |
| ---: | --- | ---: | --- | --- | --- |
| 60 Hz | ON / 240 | 60 | 25.60/34.53/39.25/44.80 | 5.00/7.81/8.72/8.90 | 60/0/0 |
| 240 Hz | ON / 240 | 193 | 23.50/25.60/27.34/27.90 | 4.60/5.54/6.79/7.80 | 193/0/0 |
| 540 Hz | ON / 240 | 193 | 23.00/28.02/35.69/48.00 | 4.50/5.80/6.82/8.70 | 193/0/0 |

The physical modes above are not TargetFPS labels: Windows was explicitly
changed to each refresh rate and verified after every run.

### Controlled 0 ns stress attempt

The current Debug B was also run at physical 60 Hz with two images, VSync OFF,
TargetFPS 1000, Reflex ON, and a `0 ns` low-latency Acquire budget. It produced
194 samples in the final window, with:

```text
present_begin_total p50/p95/p99/max = 22.50/25.04/28.01/34.80 us
present_acquire    p50/p95/p99/max = 4.30/5.10/7.56/9.30 us
acquire_low_latency_attempt_count=194
acquire_low_latency_skip_count=0
acquire_not_ready_count=0
present_skipped_for_latency_budget_count=0
```

The workload was high-rate and completed normally, but it did not make the
driver return `VK_TIMEOUT`/`VK_NOT_READY`. Therefore a physical skip-to-recovery
sequence remains `OPEN`; no zero-skip run is promoted as branch coverage.

## Release and DX12 same-build comparison

The current Release binary (`387BE3…E006`) was run on the same ROM/state at
physical 60 Hz with Vulkan, Reflex ON, VSync ON, TargetFPS 240, two images, and
the scoreboard HUD. It wrote 60 captures and emitted:

```text
present_begin_total p50/p95/p99/max = 8.70/17.50/18.84/20.40 us
present_acquire    p50/p95/p99/max = 2.20/4.30/4.72/4.90 us
acquire_low_latency_attempt_count=61
acquire_low_latency_skip_count=0
acquire_not_ready_count=0
present_skipped_for_latency_budget_count=0
renderer=Vulkan vsync=1 present_mode=2 reflex_mode=1 swapchain_backbuffer_count=2
```

This is shipping-like telemetry evidence only: Release validation was not
enabled. The Debug validation runs above are the validation evidence.

The same Release binary and same scene/physical 60 Hz conditions were run with
DX12 and Reflex ON. DX12 initialized on the RTX 5070 Ti and completed 60
captures. Its native telemetry reported `present_begin_wait` windows of:

```text
p50/p95/p99/max = 8.866/12.333/12.771/12.919 ms
```

This is a same-build renderer-health/frame-pacing comparison. DX12's
`present_begin_wait` is not Vulkan's `vkAcquireNextImageKHR` metric, so no
cross-backend input-to-present parity claim is made.

## Window/fullscreen and validation event run

The Debug B build was exercised with
`tools/testing/vulkan-present-event-matrix.ps1 -Phase fullscreen
-ValidateSync -ReflexMode 1 -Policy 2`. The run completed:

```text
fullscreen toggle x8
config restore : PASS
layer restore  : PASS
swapchain rebuilds: 8
device lost       : 0
sync validation   : enabled (banner confirmed)
sync hazards      : 0
validation        : clean
```

The Vulkan logs show every resulting swapchain as `window-mode=windowed`, even
though the F11 fullscreen-toggle event sequence and eight rebuilds completed.
Consequently this is clean resize/toggle lifecycle evidence, but a confirmed
true-fullscreen presentation-mode pass remains `OPEN` rather than being
claimed from the event script label.

## Remaining gates

| Gate | Status | Evidence/limitation |
| --- | --- | --- |
| P2 normal-path classification | PASS | source audit, current Debug build, and historical Release artifact |
| P3 negative timeout fallback | PASS for current Debug | normal and low-latency unit cases; current Release rebuild blocked by the host MSYS2 linker environment |
| P3 telemetry semantics cleanup | PASS for current Debug/source | counter is structurally inside the low-latency timeout block; normal timeout retains only `acquire_not_ready_count` |
| Physical 60/240/540 Hz windowed Vulkan | PASS for this Windows/NVIDIA workload | historical pre-cleanup B, 60 captures per run, zero error markers; cleanup is telemetry-only |
| Release Vulkan smoke | PASS historically; current rebuild OPEN | historical pre-cleanup binary passed; current Release artifact was not produced because of the MSYS2 `sh.exe` environment failure |
| Fullscreen Vulkan with synchronization validation | OPEN | F11 cycle clean, but logs did not confirm `window-mode=fullscreen` |
| Controlled nonzero `VK_TIMEOUT`/`VK_NOT_READY` skip and recovery | OPEN | 0 ns/high-rate/2-image stress still produced 0 skips |
| Reflex ON + Boost | NOT RUN | all runs report `lowLatencyBoost=false` |
| Linux/AMD runtime | NOT RUN | platform/GPU unavailable on this host |
| DX12 latency parity | NOT CLAIMED | native wait metric is not Acquire-equivalent |

No source change restores `ImagesInFlight`, adds a post-Acquire fence wait,
calls `vkDeviceWaitIdle` in the frame path, or changes the 500 us default to
zero. Those exclusions remain part of the bounded Acquire contract.
