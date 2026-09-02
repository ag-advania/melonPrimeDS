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

Windows Raw telemetry is separately compile-gated by
`MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY` and runtime-gated by
`MELONPRIME_RAW_INPUT_PERF=1`. Its reports provide the stable Phase 0 names
`RawSubscriptionLockWait`, `RawSnapshot`, `RawLateLatch`,
`RawDeferredDrain`, `RawBatchCallCount`, and `RawBatchEventCount`, plus:

- subscription/frame acquisition, wait, hold, and maximum-wait counters;
- `HiddenWndProc`, `LateLatch`, and `DeferredDrain` lock wait by site;
- `GetRawInputBuffer` calls, non-empty/empty calls, and event totals;
- late-latch delta claims and post-draw captured events.

Generic reports and frame CSV rows carry `instance_id`. Each emulation thread
has its own generic probe state; set `MELONPRIME_PERF_CSV` to a path containing
`%INSTANCE%` when collecting more than one instance (without the placeholder,
the probe adds `.instanceN`). Set `MELONPRIME_PERF_CAPTURE_ONLY=1` to capture
without periodic sorting/formatting; the owning thread emits the final report
at shutdown. Raw lock wait/hold samples are committed after releasing the
measured lock, and its `DeferredDrain` report is emitted after the stage scope.

The Raw lock and batch counters are report-window totals. Divide them by the
corresponding frame/snapshot count when a per-frame rate is needed. Timing
percentiles retain at least the latest 2048 samples in the developer probe;
the `calls` value remains the total observed count for that process window.

The deterministic parser emits JSON/Markdown from real logs and can enforce
the common input limits:

```text
python tools/testing/summarize-input-performance.py --self-test
python tools/testing/summarize-input-performance.py run.stderr \
  --mode controller --min-input-samples 1000 --check-budget \
  --json-out input-summary.json --markdown-out input-summary.md
```

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
| E | two instances / separate devices | windowed | no process-global joystick lock serialization |

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
