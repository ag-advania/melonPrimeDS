# Input frame contract

This is the current-source contract for the MelonPrime input pipeline. It
defines ownership and measurement boundaries; a passing source audit is not a
claim that a physical controller, Raw Input device, or remote CI runner was
tested.

The architecture map is [Input SRP ownership](../../architecture/input/input-srp-ownership.md).
The surrounding frame-order rules are in the [SRP/performance contract](../../architecture/srp-performance-contract.md).

## Ten load-bearing rules

1. Global emulator command edges belong to the outer `inputProcess` owner.
   Pause, reset, frame step, fullscreen, screen swap, and related command
   presses are not recomputed by a late gameplay sample.
2. Gameplay controller state belongs to the guest-frame late latch. A running
   frame samples the controller immediately before `RunFrameHook`.
3. Windows Raw mouse delta may be late-latched immediately before the aim
   write. The Raw/Qt source decision remains exclusive: one physical event is
   not added by both sources.
4. Global hotkey edges are not recalculated by the late latch. The late path
   may update gameplay held/pressed state, but it cannot re-fire an outer
   command edge.
5. Reconnect and binding-generation changes establish a held baseline. A
   control already held at the boundary is not synthesized as a press.
6. Nested `FrameAdvance` is a snapshot-only consumer. It does not consume the
   outer gameplay baseline, press mailbox, or wheel impulse.
7. `DeferredDrain` runs after `RunFrame` and `drawScreen`, so data needed only
   by the next snapshot is outside the input-to-guest latency path.
8. The Raw safety drain must not be removed without evidence. The
   `GetRawInputBuffer` call before the `PeekMessage` drain protects against
   the shared-buffer path that can otherwise lose key-up events and create a
   stuck key.
9. Binding compilation and source/fanout table construction are cold-path
   operations. The frame path samples each unique physical source once and
   projects fixed values without allocation, lookup, or virtual dispatch.
10. The final input commit is immediately before `NDS::RunFrame`. The required
    order remains `RunFrameHook`, `SetKeyMask`, then `NDS::RunFrame`; in
    particular, `RunFrameHook` remains before `makeCurrentGL`.

## Ownership boundaries

`MelonPrimeJoystickDevice` is a per-`EmuInstance` value component. It owns SDL
joystick/controller pointer lifetime, capabilities, sensors, rumble, and the
per-device mutex. SDL process bookkeeping (`SDL_JoystickUpdate`, enumeration,
open, and close) uses only a short process lock. Physical source reads and
sensor/rumble operations use the instance's device lock, so separate emulator
instances do not serialize their steady-state reads on `joyMutexGlobal`.
The SDL process guard returns wait/hold ticks through a small POD; generic
process-lock metrics are committed only after the outer device lock is
released. `SdlProcessTiming`, its timing overload, and the caller's timing
object are compiled only under `MELONPRIME_ENABLE_DEVELOPER_FEATURES`.
Shipping controller sampling calls the parameterless `UpdateLocked()` path,
so it has no timing pointer plumbing or performance-counter reads.
The component does not expose its SDL handle. Cold mapping UI calls
`EmuInstance::pollJoystickMapping` and `captureJoystickAxisRest`, which own the
device lock and keep mapping-specific SDL reads inside the device owner.

Windows Raw Input has two lock planes:

- the filter's subscription mutex is for cold control-plane changes:
  subscribe/unsubscribe, owner transfer, registration, HWND recreation, and
  mapping/config changes;
- each subscription's recursive frame mutex protects its state, active HWND
  identity, and re-entrant dispatch. Stable snapshot, late-latch,
  post-draw-drain, and `HiddenWndProc` paths do not acquire the process-wide
  subscription mutex.

Retired Raw subscription records remain owned by the service until service
teardown. Frame callers publish raw subscription pointers without a per-frame
reference-count operation; retaining the record closes the unsubscribe/frame
consumer lifetime race. A retired record is rejected before state or HWND use.

## Developer telemetry

Generic input telemetry is compiled only for a developer MelonPrime build and
is enabled with `MELONPRIME_PERF=1`. The `input_metric_us` report contains:

| Metric | Meaning |
|---|---|
| `InputTotal` / `input_total` | post-limiter input sample through `RunFrameHook` and `SetKeyMask`; paused input is measured separately without the pause gap |
| `JoystickLockWait` | wait for the per-instance SDL device mutex |
| `JoystickSample` | physical sample, including the short SDL process-update lock |
| `JoystickProject` | fixed binding fanout projection after the device lock |
| `JoystickSDLUpdate` | the `SDL_JoystickUpdate` call inside the required physical sample |
| `JoystickProcessMutexWait` | wait for the short SDL process bookkeeping lock |
| `JoystickProcessMutexHold` | time spent holding that SDL process bookkeeping lock |

Each current generic `input_metric_us` entry reports `c` as the number of
observed calls, `retained` as the number of samples currently kept in its
latest-N ring (2048), and `retention_mode=latest_n` in parser output. The
p50/p95/p99 values and `retained_max` are calculated from that retained ring.
`max` is the whole live-report window or capture run, so an early outlier can
make it larger than `retained_max`. Explicit input latency (`input sample ->
RunFrame begin` and `input sample -> Present end`) uses the same calls/retained/
latest-N semantics.

The parser preserves historical provenance. A pre-ring generic report without
`retained` is marked `retention_mode=legacy_first_n` with a 2048-sample cap;
the older explicit-latency format is marked the same way with its historical
512-sample cap. For a legacy report with more calls than its cap, the reported
percentiles are only the retained prefix, and an older explicit-latency `max`
is also prefix-only; the old generic `max` remains its producer's whole-call
maximum but has no retained max. Therefore `--check-budget` refuses an
over-cap legacy report by default. `--allow-legacy-first-n` is an explicit
historical-analysis escape hatch. JSON includes the mode/cap on every parsed
metric and the top-level run, while Markdown includes the mode and max scope.

Windows Raw telemetry is separately compile-gated by
`MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY` and runtime-gated by
`MELONPRIME_RAW_INPUT_PERF=1`. Its reports provide the stable Phase 0 names
`RawSubscriptionLockWait`, `RawSnapshot`, `RawLateLatch`,
`RawDeferredDrain`, `RawBatchCallCount`, and `RawBatchEventCount`, plus:

- subscription/frame acquisition, wait, hold, and maximum-wait counters;
- `HiddenWndProc`, `LateLatch`, and `DeferredDrain` lock wait by site;
- `GetRawInputBuffer` calls, non-empty/empty calls, and event totals;
- late-latch delta claims and post-draw captured events.
- recursive acquisition count and maximum depth for the subscription/frame
  recursive mutexes; these are evidence for a future plain-mutex decision,
  not a claim that the type is already replaceable.

Generic reports and frame CSV rows carry `instance_id`. Each emulation thread
has its own generic probe state; set `MELONPRIME_PERF_CSV` to a path containing
`%INSTANCE%` when collecting more than one instance (without the placeholder,
the probe adds `.instanceN`). Set `MELONPRIME_PERF_CAPTURE_ONLY=1` to capture
without periodic sorting/formatting; the owning thread emits the generic final
report at shutdown and includes a `capture_mode ... report_seq=N
capture_only=1` marker. Every generic input, explicit-latency, phase, and
capture marker line emitted by one report uses that same `report_seq`; the
parser binds evidence by `(instance_id, report_seq)`. A newer incomplete or
markerless generation fails strict certification and never falls back to an
older complete generation. Explicit-latency lines from another generation are
kept only as unbound supplemental evidence and are omitted from certified
Markdown.
Raw capture-only runs use `MELONPRIME_RAW_INPUT_PERF_CAPTURE_ONLY=1`; the final
Raw report includes `capture_mode`, `lock_planes`, and `stage_us` lines carrying
one shared `report_seq`. Raw lock wait/hold samples are
committed after releasing the measured lock, its `DeferredDrain` report is
emitted after the stage scope, and the process-wide Raw final report is emitted
only when the last Raw service reference is released. The final Raw report is
printed before that service's cold destructor cleanup, keeping hidden-window
teardown lock acquisitions out of runtime measurements.

The Raw frame mutex remains recursive while the developer probe measures actual
recursive acquisitions and maximum depth. A plain `std::mutex` conversion is
considered only after the stress matrix records zero recursive acquisitions.

The Raw stage line uses the same explicit contract for `snapshot`, `late_latch`,
and `deferred_drain`: `calls` and `max` cover the current Raw service/capture
lifetime, `retained` is the latest 2048 samples, and p50/p95/p99 plus
`retained_max` use that retained ring. The parser labels this maximum scope as
`raw_service_lifetime`; it does not call the cumulative values a one-second
report window. The parser and Markdown output preserve the stage retention
fields. Raw lock and batch counters are also cumulative service/capture
lifetime totals; subtract two reports to derive a live interval delta, or
divide a run total by the corresponding frame/snapshot count for a rate.
Zero-duration developer samples are retained as valid observations rather than
silently dropped.

The deterministic parser emits JSON/Markdown from real logs and can enforce
the common input limits:

```text
python tools/testing/summarize-input-performance.py --self-test
python tools/testing/summarize-input-performance.py run.stderr \
  --mode controller --min-input-samples 1000 --check-budget \
  --json-out input-summary.json --markdown-out input-summary.md

# Historical or markerless artifacts are analysis-only and never certification.
python tools/testing/summarize-input-performance.py old-run.stderr \
  --historical-analysis

# A strict certification run must use both input capture-only switches.
MELONPRIME_PERF=1 MELONPRIME_PERF_CAPTURE_ONLY=1 \
MELONPRIME_RAW_INPUT_PERF=1 MELONPRIME_RAW_INPUT_PERF_CAPTURE_ONLY=1 \
  melonDS
```

`--check-budget` is strict certification: it defaults to and requires at least
1000 calls, and it must be paired with an explicit `--mode keyboard`,
`--mode controller`, or `--mode raw`. Omitting `--mode` or selecting `all` is
rejected because `all` is summary-only and does not prove every benchmark
population. `--mode controller` requires every joystick metric plus at least
the minimum calls in each metric, while `--mode keyboard` rejects any joystick
metric calls. `--mode raw` requires `snapshot` and `late_latch` stage
populations to meet the same minimum and requires one complete Raw generation:
the capture-only marker, `stage_us`, and `lock_planes` must share the latest
`report_seq`. The lock line must contain
`subscription_mutex_acq`, `subscription_mutex_wait_ns`,
`subscription_mutex_hold_ns`, `subscription_mutex_max_wait_ns`,
`frame_mutex_acq`, `frame_mutex_wait_ns`, `frame_mutex_hold_ns`,
`frame_mutex_max_wait_ns`, `recursive_acquisitions`,
`subscription_max_recursion_depth`, and `frame_max_recursion_depth`. Missing
lock evidence or any required key is a strict failure.

`--historical-analysis` is the explicit non-certifying path for old or
markerless artifacts. It permits capture-mode provenance to remain unknown and
emits JSON with `certification_scope=historical_analysis`,
`historical_analysis=true`, and `certified=false`; it must not be combined with
`--check-budget`. The legacy compatibility flags
`--allow-legacy-first-n` and `--allow-legacy-raw-unversioned` are accepted only
with `--historical-analysis`, and are deprecated: they do not change the
non-certifying status. Historical analysis itself is sufficient to parse legacy
provenance. An old Raw report without surfaced call counts is
`legacy_unversioned` and can therefore be summarized historically without
inventing sample counts or capture provenance.

JSON and Markdown outputs carry the same certification context. Every Markdown
summary states `Certification scope`, `Certified`, `Historical analysis`,
`Mode`, `Capture-only verified`, `Minimum samples`, and `Budget checked` before
the metrics table. Historical Markdown additionally begins with `NOT A
CERTIFICATION RESULT` and `Historical analysis only.` so it remains safe when
shared without its JSON sidecar.

No runtime log is generated by the source audit or local build. A physical
benchmark must preserve the original stderr and parser output with its device,
window mode, build configuration, and run length.

## Budget and benchmark matrix

These are acceptance targets, not measurements made by the static audit:

| Metric | Target |
|---|---:|
| keyboard-only input median | `< 10 us/frame` |
| active-controller input median | `< 30 us/frame` |
| Raw snapshot + late latch median | `< 40 us/frame` |
| input p99 | `< 100 us/frame` |
| usual input max | `< 250 us` |
| hot-path heap allocation | `0` |
| steady-frame config/TOML/filesystem/blocking GUI work | `0` |
| unconditional physical recovery syscall | `0` |

Collect at least 1000 frames for each applicable row:

| Run | Input condition | Window condition | Required evidence |
|---|---|---|---|
| A | keyboard only | windowed | input p50/p95/p99/max |
| B | active controller | windowed | input and joystick metrics |
| C | Windows Raw Input aim | windowed | Raw stages, lock planes, batch counts |
| D | Windows Raw Input aim | fullscreen | same Raw metrics plus focus/owner state |
| E | two instances / separate devices | windowed | instance-specific p95/p99/max for process-lock and joystick metrics before any coalescing decision |

Also record reconnect, focus-loss, Raw registration failure fallback, rumble,
sensor, short click, wheel pulse, and nested-frame cases. These are functional
or hardware checks; a source contract test cannot replace them.

## Regression and evidence boundary

The Windows and Ubuntu audit jobs run the source contract alongside the SRP
audit and the input summarizer self-test. The workflow checks prove source
shape, compile gates, and parser determinism. They do not prove physical input,
mult-instance timing, TSan, macOS/Linux runtime behavior, or remote CI
performance. Those remain open until a matching artifact is attached.

For a performance change, retain:

- the exact build feature gates and commit;
- the raw stderr log and parser JSON/Markdown;
- at least 1000 selected frames and p50/p95/p99/max;
- input and explicit-latency `calls`/`retained` values, including the latest-N
  retention semantics;
- Raw lock wait and batch syscall counts where Raw is enabled;
- single-instance and two-instance results;
- the functional matrix result and any unrun hardware/platform rows.

The review commands are:

```text
pwsh -NoProfile -File tools/ci/audits/audit-melonprime-srp-performance.ps1
python tools/testing/test_input_postpush_full_contract.py
python tools/testing/summarize-input-performance.py --self-test
git diff --check
```

Release and shipping CI builds keep both telemetry compile gates disabled.
