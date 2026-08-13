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
| Synchronization validation, current-tree minimize/restore | **PASS**; banner confirmed, VUID 0, `SYNC-HAZARD` 0, `DEVICE_LOST` 0 |
| VSync off control — `IMMEDIATE`, target scheduling off | as specified |
| Reflex on → `authority=NvidiaReflex`, both generic mechanisms off | as specified |
| Device loss / software fallback / recreate storm | none |
| Automated window matrix at the recorded fix | 4 phases clean; current-tree Sync gate also clean |

An earlier validation session found and fixed
(`VUID-VkPresentTimingInfoEXT-timeDomainId-12400`). The later current-tree
Sync session also found and fixed
(`VUID-vkQueuePresentKHR-pWaitSemaphores-03268`); both are recorded below.

The historical session covered startup, ROM load and steady-state presentation.
The window-driven subset is now automated and has a separate after-fix archive:
resize x40, minimize/restore x20, fullscreen x8 and idle control. The remaining
rows are still **NOT RUN** in the archive and need a person driving the
emulator: DPI, Video Settings, renderer switching, ROM lifecycle, speed modes,
and every latency number in Phases 2-3. The intended current-tree
synchronization gate is recorded in
`docs/archive/audits/vulkan/2026-08-13-event-matrix/vk-min-sync-configfix3.*`.
The older `vk-min-sync-final2.*` files are retained as a diagnostic run of the
wrong effective configuration. This is a targeted minimize/restore gate; the
full policy/manual matrix remains open.

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

Event coverage (counts, not frames). The window-driven ones are automated —
doing them by hand is slow, unrepeatable, and the interesting failures are races
that need dozens of events to surface:

```bash
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 -Rom <rom> -Phase resize   -Tag resize1
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 -Rom <rom> -Phase minimize -Tag min1
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 -Rom <rom> -Phase idle     -Tag idle1
```

It exits non-zero on any VUID or device loss and prints the VUIDs grouped by
count. Run each phase separately: attributing a failure to "resize" or to
"minimize/restore" is most of the work of fixing it.

- [x] resize x40 (drives one swapchain rebuild each)
- [x] minimize / restore x20
- [x] fullscreen toggle x8
- [x] idle control (same runtime, no events)
- [ ] DPI change — manual; automating it means changing a Windows display
      setting, which the harness deliberately does not do
- [ ] Video Settings open/cancel/apply x20 — manual (dialog interaction)
- [ ] Vulkan ↔ Software x20, ↔ OpenGL Compute, ↔ DX12 where the build has it —
      manual (dialog interaction)
- [ ] ROM launch, savestate load, reset, close, reopen — manual, and see below
- [ ] Fast Forward, Slow Motion — manual, and see below

**Why the hotkey-driven rows are not automated.** Driving them with `SendKeys`
was tried and does not work, and the details are worth keeping so the next
attempt does not repeat them:

* Hotkeys are **per-instance** config. A top-level `Keyboard.HK_*` in
  `melonDS.toml` is silently ignored; the binding has to go in
  `[Instance0.Keyboard]`, and `[Instance0]` must be declared too or the config
  save throws `toml::serialization_error` ("an implicit table cannot have
  non-table value") and the app dies.
* With the bindings correctly applied and confirmed by reading back the config
  the app itself re-saved, synthesised key events still never reached the
  emulator. `SendKeys` does reach `QAction` shortcuts — F11 fullscreen toggles
  reliably, which is why that phase is automated — so the gap is in the `HK_*`
  key-event path, not in the input synthesis. Foreground activation and a
  client-area click to focus the render widget did not change it.
* `HK_SlowMo` has **no keyboard binding at all** in this build: there is no
  `HKKey_SlowMo` entry in `Config.cpp`, only the joystick one. The Slow Motion
  row can only be driven by a pad.

The archived four-phase core-validation matrix is **clean** — no VUID, no device
loss, no software fallback, no recreate storm. Evidence and the full history of
the one defect these phases did find are in
[`docs/archive/audits/vulkan/2026-08-13-event-matrix/`](../../archive/audits/vulkan/2026-08-13-event-matrix/README.md).

That defect was `VUID-vkQueuePresentKHR-pWaitSemaphores-03268`, 20 messages over
22 rebuilds in minimize/restore: the queue-full retry re-presented on a wait
semaphore whose wait the rejected call had already enqueued. It was never a
swapchain-lifecycle bug — minimize/restore merely overflowed the present-timing
results queue often enough to expose it. Fixed by dropping the wait semaphores
on the retry, and held by a contract in `audit-low-latency-contract.py`. The
current-tree follow-up additionally pauses timing metadata before the results
queue is full, so Synchronization Validation does not encounter the
retry-as-a-second-present hazard.

When that proactive pause happens, developer telemetry must report
`fallback=timing queue pressure`. `fallback=timing query failed` is reserved for
an actual `GetPastPresentationTimingEXT` failure; the distinction keeps a
recoverable finite-queue pressure event separate from a driver query failure.

The archived post-semaphore-fix core run intentionally exercised the retry path
(9, 10 and 10 retries across three runs) and stayed clean. On the current tree,
the new capacity guard is expected to prevent an actual driver queue-full error:
the Sync gate must instead show `queue at capacity`, a subsequent queue growth,
and `queue-full errors=0`. This proves the pressure/recovery path without
re-entering the validation hazard that motivated the follow-up.

Minimize/restore is the **only** phase that fills the present-timing results
queue, because presents stall while the window is hidden. Running free (`-NoVSync`,
which selects IMMEDIATE on this surface) does not fill it, so it is a VSync-off
contract control rather than timing-queue stress.

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

For the reproducible window stress, use the harness switch below. It resolves
the same config root the portable executable uses (`<BuildDir>\portable\melonDS.toml`
when that directory exists, otherwise `<BuildDir>\melonDS.toml`), backs up and
restores that config byte-for-byte, and self-checks the final policy, Reflex
requested/actual state, requested VSync and selected present mode. It also
requires the validation banner to list **Synchronization**, scans both stdout
and stderr for VUIDs and `SYNC-HAZARD`, and restores/removes the temporary layer
settings file afterward:

```powershell
powershell -ExecutionPolicy Bypass -File tools\testing\vulkan-present-event-matrix.ps1 `
  -Rom <rom> -Phase minimize -ValidateSync -Policy 2 -ReflexMode 0 -Tag min-sync1
```

An empty log file is only evidence of "no findings" once you have proved the
settings are being read at all. Add `info` to `report_flags` for one control
run: the layer then prints a `CURRENT-VALIDATION-ENABLED` banner listing the
enabled checks, and **Synchronization** must appear in it. Without that control,
an empty log is indistinguishable from a settings file the loader never found.

**Delete the file when the pass is over.** Left in place it silently enables
sync validation for every later run of that binary.

Current-tree intended-configuration gate result (2026-08-13, Debug build with
`MELONDS_VULKAN_ENABLE_VALIDATION=1`), rerun after the `TimingQueuePressure`
follow-up:

```text
Policy=JustInTime, Reflex requested=off actual=inactive
requested-vsync=on, selected-present-mode=FIFO
config path=<BuildDir>\portable\melonDS.toml
config restore=PASS, layer restore=PASS, config integrity=PASS
Phase=minimize, minimize/restore x20, swapchain rebuilds=22
CURRENT-VALIDATION-ENABLED: confirmed; Synchronization listed
VUID=0, SYNC-HAZARD=0, DEVICE_LOST=0, exit=0
queue-at-capacity events=15, queue growth events=4 (16 -> 32)
fallback=timing queue pressure=14, timing query failure=0
queue-full errors=0
```

The first current-tree attempt exposed 20 `SYNC-HAZARD-WRITE-AFTER-PRESENT`
messages on the retry-as-a-second-present path and was correctly rejected. The
capacity guard fixed that path. The earlier `vk-min-sync-final2.*` log is
retained as a diagnostic run of the wrong effective configuration
(`TelemetryOnly` / Reflex On / VSync Off); the parent intended-configuration
log is `vk-min-sync-configfix3.*`, while the current post-follow-up gate is
archived as `vk-min-sync-final-audit.out.log` / `.err.log`, with the harness
summary saved as `vk-min-sync-final-audit.harness.log`. This targeted gate does
not close the manual lifecycle rows or the full Phase 3 A/B matrix.

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

The aggregator removes an existing `summary.csv` at the start of the
invocation. If a run is then classified `INVALID`, the stale summary remains
absent; do not interpret a previous run's file as the current result. It also
rejects an `--out` path that is one of the input captures.

Percentiles are computed per run and then compared across runs of the same mode.
Do not pool every frame of every run — a longer run would outvote a shorter one.

### Reading `target_mode` and `target_value_ns`

Every captured frame records which scheduling mode it actually used. Check this
before comparing anything: a `JustInTime` run that fell through to no target is
an A1 run wearing an A2 label, and averaging the two together produces a
meaningless number.

| `target_mode` | Meaning | `target_value_ns` is |
|---|---|---|
| `0` none | no target was requested | 0 |
| `1` absolute | an instant on the presentation timeline | a timestamp |
| `2` relative | a minimum previous-image visible duration | a **duration** |

`target_scheduling` records what the accepted present **actually carried**, not
what the policy allowed. The two differ in three situations, and all three must
count as misses rather than hits:

* **bootstrap** — absolute has no feedback baseline yet, or relative has no
  previous present to be relative to;
* **queue-full retry** — the present was re-issued with its timing metadata
  stripped, so the frame was displayed with no target at all;
* any frame where the target evaluated to zero.

This matters because the acceptance threshold below is read straight from this
column: sourcing it from the resolver's permission would count those misses as
active and inflate the ratio.

The two are not comparable quantities, which is why the aggregation script puts
the mode in the run label rather than pooling them. On a surface without
`presentAtAbsoluteTimeSupported` the `JustInTime` policies use relative mode;
that is the supported fallback, not a fault.

In relative mode the capture also records the inputs each duration was computed
from, so the cadence can be checked per present without trusting a log line:

```text
target_generation_refresh_interval_ns
target_generation_refresh_duration_ns
relative_quanta
relative_accumulator_before_ns
relative_accumulator_after_ns
```

Two invariants worth asserting over a run:

* `target_value_ns == relative_quanta * target_generation_refresh_interval_ns`
  on every quantized frame.
* `relative_accumulator_after_ns < target_generation_refresh_interval_ns`
  always — the carried fraction never reaches a whole refresh.

The refresh interval genuinely moves during a session (the driver bumps
`timingPropertiesCounter` and the pacer re-reads it), so comparing a target
against the *current* interval rather than its generating one will produce false
mismatches. That is why the generating value is stored per row.

`aggregate-vulkan-latency.py` checks both invariants, plus the consistency of
the target columns themselves, and **exits non-zero before writing any normal
summary or per-mode output** with the offending rows listed if any run
contradicts itself. A run flagged this way is `INVALID`, not a slower or faster
result — do not compare it. The synthetic regression check is:

```text
python tools/testing/aggregate-vulkan-latency-tests.py  PASS
```

### `bounded_wait` vs `bounded_wait_attempted`

`bounded_wait` is the policy's permission for the frame; `bounded_wait_attempted`
records whether `vkWaitForPresent2KHR` was actually called. They differ whenever
there is nothing to wait on — no accepted present yet, or the previous one has
already been waited for — so the permission alone would overstate how often the
wait ran. The aggregator reports both as `bounded_wait_allowed_ratio` and
`bounded_wait_attempted_ratio`.

The acceptance threshold below is a timeout *rate*, so compute it against
attempted waits, not against every frame. The aggregator reports it directly as
`wait_timeout_rate`, and reports `wait_timeouts_in_window` alongside the raw
`wait_timeout_count`: the raw column is a running total for the entire run, so
subtracting its value at the warm-up boundary is what keeps warm-up timeouts
from being charged to the measured window. Use `wait_timeout_rate`; deriving a
rate from `wait_timeout_count` directly will overstate it.

### Swapchain-local counters

`wait_timeout_count`, `timing_queue_full_count` and
`timing_queue_recovery_count` reset when the swapchain is recreated. Every
latency-capture row therefore carries a monotonic `swapchain_generation`.
`aggregate-vulkan-latency.py` treats a generation change inside the measured
window as `INVALID`; it never subtracts a post-recreate counter from the
warm-up baseline and reports a plausible but incomplete total. It also treats
a generation change between the last warm-up row and the first measured row as
`INVALID`, because the timeout baseline would belong to a different
swapchain. Re-run that mode with one generation spanning the whole measured
window and its warm-up boundary.

### Where the present span ends

`present_end_time_us` and the Reflex `PRESENT_END` marker are both taken the
instant the final `vkQueuePresentKHR` returns, before any pacer or capture
bookkeeping. `VK_NV_low_latency2` defines `PRESENT_END` as "when
vkQueuePresentKHR returns", and the host proxy below is only meaningful with the
same boundary — otherwise post-present CPU work lands inside every measured
interval.

So `input_to_present` covers input sampling through the present call returning,
and nothing after it. On the queue-full retry path the span covers both calls
and closes when the second returns: one presentation, one marker pair.

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
