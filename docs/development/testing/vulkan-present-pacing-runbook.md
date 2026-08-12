# Vulkan present pacing: Validation Layer and NVIDIA A/B runbook

Operator procedure for validating the vendor-neutral Vulkan present pacing path
and measuring it against NVIDIA Reflex. Everything here needs a human at a real
machine with a GPU, a monitor and a ROM; none of it can be established by
building or by static audit.

Background on what is being validated:
[`docs/features/rendering/vulkan-backend.md`](../../features/rendering/vulkan-backend.md).

## The one rule

**Correctness and latency are never measured by the same build.**

| | Validation build | A/B build |
|---|---|---|
| Preset | `debug-mingw-x86_64` | `release-mingw-x86_64` |
| How | `tools\build\windows\build-mingw-validation.bat` | explicit `cmake` (below) |
| Validation Layer | on | **off** |
| `MELONPRIME_ENABLE_DEVELOPER_FEATURES` | on | **off** for the final numbers |
| `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` | off | on |
| Produces | VUID findings | `*.csv` |
| Latency numbers | **discard** | keep |

`build-mingw.bat` is not the A/B build: it hardcodes
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON`, which is exactly the setting the
final numbers must not carry. Use the explicit configure in Phase 2.

Two separate reasons, both of which change timing:

* The Validation Layer inspects every call. That is its job, and it costs time.
* `MELONPRIME_ENABLE_DEVELOPER_FEATURES` makes the pacer request a timestamp for
  every present stage the surface supports, instead of only the target stage a
  shipping build asks for. That changes driver work and timing-queue pressure.

`MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` exists precisely so measurement does
not have to imply developer features. It adds a CSV writer and nothing else: it
must never change stage queries, pacing decisions or frame content.

## Before starting

```bash
git fetch && git rev-parse HEAD && git status --short
```

Record the SHA that was actually tested. A dirty tree does not produce a result;
commit or stash first.

Fill this in at the top of every result file, and change nothing in it inside an
A/B group:

```text
Commit SHA
Build type / preset
Compiler / toolchain
Windows version
NVIDIA GPU model
NVIDIA driver version
Vulkan loader version
VK_LAYER_KHRONOS_validation version   (vulkaninfo)
Monitor model / refresh rate
G-SYNC / VRR on or off
VSync on or off
Windowed or fullscreen
Internal resolution scale
TargetFPS
ROM region / version
Savestate and location
Power plan
NVIDIA Control Panel overrides
Overlays present (OBS / RTSS / ReShade / Discord)
```

Use one fixed scene for every run: same savestate, same room, same camera
direction, same HUD, same resolution, same audio. Do not change it midway.

## Status

A first session ran on 2026-08-13 (RTX 5070 Ti, driver 610.74.0.0, loader
1.4.357). See the runtime-status section in
[`vulkan-backend.md`](../../features/rendering/vulkan-backend.md) for details.

| Item | Result |
|---|---|
| Validation layer enabled and confirmed in log | yes |
| Core validation, policies 0/1/2/3, Reflex off | **0 errors** |
| Core validation, Reflex on | **0 errors** |
| Synchronization validation, policies 0/2/3, Reflex on+boost | **0 hazards** |
| VSync off control — `IMMEDIATE`, target scheduling off | as specified |
| Reflex on → `authority=NvidiaReflex`, both generic mechanisms off | as specified |
| Device loss / software fallback / recreate storm | none |

One VUID was found and fixed during the session
(`VUID-VkPresentTimingInfoEXT-timeDomainId-12400`).

The session covered startup, ROM load and steady-state presentation. Still
**NOT RUN**: the event matrix below (fullscreen, resize, DPI, minimize, F2,
renderer switching), the speed modes, and every latency number in Phases 2-3 —
all of which need a person driving the emulator.

One expectation in the source instructions cannot be observed on this surface:
with VSync off the fallback reason reads `absolute timing unsupported by
surface` rather than `present mode is not FIFO`. Both conditions hold; the
classifier reports the earlier one in its debug order. Once a surface supports
absolute timing, the non-FIFO reason becomes the visible one.

## Phase 1 — Validation Layer

```bash
tools\build\windows\build-mingw-validation.bat --jobs 1
```

Only `CMAKE_BUILD_TYPE=Debug` defines `MELONDS_VULKAN_ENABLE_VALIDATION`, so a
successful `build-mingw.bat` says nothing about whether validation runs.

Start the binary and confirm in the log:

```text
[Vulkan] validation layer enabled
```

If it says the layer is not installed, the phase is **BLOCKED**, not passed.
Install the Vulkan SDK and retry. Record the layer's `implementationVersion` and
`specVersion` from `vulkaninfo`.

Prefer a clean run with third-party implicit layers (OBS, RTSS, ReShade)
disabled, so injected-layer warnings are not attributed to this backend. Note
any that could not be disabled.

### Pass A — core validation only

Start with the standard layer. Do not stack GPU-Assisted, Synchronization
Validation and Best Practices on the first pass; they make attribution harder.

Per policy (`3D.Vulkan.PresentPacingPolicy`), warm up ~300 frames then run 3,000+:

- [ ] Policy 0 `TelemetryOnly`, VSync on
- [ ] Policy 1 `PresentWait`, VSync on
- [ ] Policy 2 `JustInTime`, VSync on
- [ ] Policy 3 `JustInTimeFifoLatestReady`, VSync on — **UNSUPPORTED** rather
      than FAIL if the extension, surface capability or present mode is absent
- [ ] Policy 2, VSync **off** — expect `targetScheduling=off` with
      `fallback=present mode is not FIFO`
- [ ] Reflex Off / On / On+Boost — with Reflex active expect both
      `boundedWait=off` and `targetScheduling=off` in the log
- [ ] Fast Forward hold, Fast Forward toggle, Slow Motion — both generic
      mechanisms off

Event coverage (counts, not frames):

- [ ] fullscreen toggle x20
- [ ] resize x50, DPI change, minimize, restore
- [ ] F2 Video Settings open/cancel/apply x20
- [ ] Vulkan ↔ Software x20, ↔ OpenGL Compute, ↔ DX12 where the build has it
- [ ] ROM launch, savestate load, reset, close, reopen

Watch especially for VUIDs touching `VkPresentId2KHR`, `VkPresentTimingInfoEXT`,
`VkPresentTimingsInfoEXT`, `presentStageQueries`, `targetTime`, `timeDomainId`,
`targetTimeDomainPresentStage`, `VK_SWAPCHAIN_CREATE_PRESENT_TIMING_BIT_EXT`,
`vkSetSwapchainPresentTimingQueueSizeEXT`, `FIFO_LATEST_READY` and `pNext`
lifetime.

If `vkWaitForPresent2KHR` ever returns `VK_ERROR_DEVICE_LOST`, confirm it lands
in `Fail("vkWaitForPresent2KHR", VK_ERROR_DEVICE_LOST)` and does **not** enter a
swapchain rebuild loop. Do not try to provoke this.

### Pass B — synchronization validation

Only after Pass A is clean. Enable Synchronization Validation and repeat a short
matrix: policies 0 and 2, policy 3 if supported, Reflex on, resize, fullscreen,
renderer switch. Overhead is higher again — no numbers from this pass either.

Put a `vk_layer_settings.txt` next to the executable (the loader reads it from
the process working directory):

```text
khronos_validation.validate_core = true
khronos_validation.validate_sync = true
khronos_validation.report_flags = error,warn,perf
khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
khronos_validation.log_filename = C:\tmp\vk-sync-run.log
```

An empty log file is only evidence of "no findings" once you have proved the
settings are being read at all. Add `info` to `report_flags` for one control
run: the layer then prints a `CURRENT-VALIDATION-ENABLED` banner listing the
enabled checks, and **Synchronization** must appear in it. Without that control,
an empty log is indistinguishable from a settings file the loader never found.

**Delete the file when the pass is over.** Left in place it silently enables
sync validation for every later run of that binary.

GPU-Assisted Validation is not required for present pacing. If used at all, use
a separate run.

### Gate

Proceed only with all of:

```text
core validation ERROR                    0
timing / present related WARNING         0
synchronization blocking hazard          0
DEVICE_LOST loop / swapchain recreate loop / software fallback /
Vulkan greyed out / hang / crash         none
```

Otherwise: stop, fix the root cause, re-run Phase 1.

Classify warnings. VUID, object lifetime, `pNext`, invalid feature/extension
usage, image layout, synchronization, swapchain usage, present ID and timing
metadata warnings are failures. Performance and best-practice warnings and
third-party layer warnings are recorded and investigated, not automatically
fatal. "Nothing appeared on screen" is not evidence — keep the full log, the
error and warning counts and the VUID list.

## Phase 2 — NVIDIA functional runtime

Close the validation build. Switch to a release build with the capture flag:

```bash
cmake -S . -B build/release-mingw-x86_64 -DMELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF -DMELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON
cmake --build --preset=release-mingw-x86_64 --parallel 1
```

Validation Layer off. No RenderDoc, no Nsight capture.

Confirm from the startup log:

- [ ] the expected NVIDIA GPU was selected
- [ ] `VK_NV_low_latency2` enabled, Reflex `actual=active` with Reflex On
- [ ] present timing device and surface support, present ID 2, present wait 2
      availability, `FIFO_LATEST_READY` availability — absent extensions are
      **UNSUPPORTED**, not bugs
- [ ] `vkGetLatencyTimingsNV` reports exist with non-zero `presentID` and
      timestamps
- [ ] Reflex frame ID, `VkLatencySubmissionPresentIdNV`, `VkPresentIdKHR` and
      the `VkPresentId2KHR` logical ID agree for the same frame (spot-check a
      few samples in the log)

With Reflex off and policy `JustInTime`:

- [ ] right after swapchain creation, `fallback=bootstrap waiting for feedback`
      and `targetTime=0` — this is correct, not a failure
- [ ] after feedback arrives, `authority=GenericPresentTiming`,
      `targetScheduling=capable` and a non-zero `targetTime`
- [ ] if this driver lacks `VK_KHR_present_wait2`, expect `targetScheduling` on
      with `boundedWait=off` — that is the runtime proof of the capability
      separation
- [ ] no queue-full storm, no device loss, no F2 regression

## Phase 3 — A/B measurement

Modes, VSync on, same scene, same build:

| ID | Reflex | Policy |
|---|---|---|
| A0 | Off | `TelemetryOnly` (baseline) |
| A1 | Off | `PresentWait` |
| A2 | Off | `JustInTime` |
| A3 | Off | `JustInTimeFifoLatestReady` (skip if unsupported) |
| B1 | On | `JustInTime` — expect authority `NvidiaReflex`, both generic mechanisms off |
| B2 | On+Boost | `JustInTime` |
| C0 | Off | `JustInTime`, VSync **off** — contract control, not a winner candidate |

Primary comparison is A0 vs A2 vs B1.

Each mode needs **at least 3 runs**, 600 warm-up frames plus 10,000 measured
frames, in **randomized order** so no mode sits entirely at the start or end of
a thermally drifting session. Do not run `A0 A0 A0 A2 A2 A2 B1 B1 B1`.

Set per run:

```bat
set MELONPRIME_LATENCY_RUN_ID=20260813_NV_A2_R1
set MELONPRIME_LATENCY_CSV=runs\20260813_NV_A2_R1.csv
```

Keep background load out: no Windows Update, browser video, OBS encoding, virus
scan, shader compilation or downloads. If an overlay is used, use it in every
mode. Record GPU/CPU temperature, clock and power per run; do not lock clocks
for the On+Boost comparison, since that would erase what Boost does.

Aggregate:

```bash
python tools/perf/aggregate-vulkan-latency.py --warmup 600 --out summary.csv runs/
```

Percentiles are computed per run and then compared across runs of the same mode.
Do not pool every frame of every run — a longer run would outvote a shorter one.

### What the numbers are and are not

`vkGetLatencyTimingsNV` and the capture CSV are **software marker timings**. They
describe the host pipeline up to the present call. They are not system latency
and must never be labelled click-to-photon.

End-to-end latency needs external hardware: NVIDIA Reflex Analyzer with a
compatible monitor and mouse, a high-speed camera, or a photodiode rig. With
none of those available, report the software figure as **PC pipeline latency
proxy** and record click-to-photon as NOT RUN. Where a Reflex Analyzer is
available, take 200-500 clicks per run against a fixed in-game visual response
and treat its percentiles as the primary metric.

Generic JIT produces no Reflex marker reports, so A2 and B1 cannot be compared
through `vkGetLatencyTimingsNV`. Their common ground is host frame timestamps,
frame pacing and external latency.

### Acceptance thresholds

Project criteria, not Vulkan requirements:

```text
target scheduling active, steady state    >= 95%   (excluding bootstrap and
                                                    swapchain recreation)
timing queue full count                   0        ideally
timing queue recoveries                   0        ideally
present wait timeout rate                 < 1%     above this, investigate
```

A non-zero queue-full count is not automatically a failure: check driver feedback
latency, queue size, and whether developer instrumentation was accidentally on.
Re-measure with the production-like target-stage-only build before concluding.

A single `VK_ERROR_DEVICE_LOST` is a **BLOCKER**. Stop the session and keep the
last 500 log lines, the policy, the Reflex mode, the swapchain mode, the
fullscreen state, the renderer-switch history and the driver version.

Any of: device loss after F2, software fallback, Vulkan greyed out, or a stuck
renderer is a blocker and a regression of an earlier fix.

### Calling a winner

A mode wins only with all of: no correctness regression, emulation pacing held
at the target rate, no P95/P99 frame-time regression, a latency improvement at
P50 and P95, and the same direction in all three runs. One winning run is not a
result. Use `PASS`, `REGRESSION`, `NO MATERIAL DIFFERENCE`, `UNSUPPORTED`,
`BLOCKED`, `NOT RUN` — never "feels faster"; subjective notes go in `notes.txt`.

On+Boost is preferred over On only for a reproducible improvement in P95/P99 or
in latency variance. If it only raises power and temperature, prefer On.

**One NVIDIA machine does not change the shipping default.** `TelemetryOnly`
stays the source default until AMD, Intel, Linux and MoltenVK have also been
checked. Change the config value for the A/B, not the default in the code.

## Result layout

One folder per run:

```text
runs/20260813_NV_A2_R1/
  run.json          environment block from above
  melonPrimeDS.log  full log
  latency.csv       capture output
  notes.txt         subjective observations, anomalies
```

Final report:

```text
1. Tested SHA          8. Aggregated metrics
2. Environment         9. Regressions
3. Validation result  10. Unsupported
4. Extension caps     11. Winner / no material difference
5. Functional result  12. Remaining NOT RUN
6. A/B matrix         13. Recommendation
7. Raw metrics
```

Keep the raw CSVs. Aggregations must be recomputable without re-running the
session.
