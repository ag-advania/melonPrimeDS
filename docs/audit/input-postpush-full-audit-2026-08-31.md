# Input Post-Push Full Audit — 2026-08-31

Baseline: `df82009374b977ae629f581fc05ad494d6c51cdd`

This ledger closes the source-level P1/P2 findings from the post-push full
input audit. It preserves the Windows Raw Input algorithm, fixed-point Aim,
specialized native delivery, and visible `RunFrameHook` order. Platform runtime
and hardware acceptance are classified separately below.

## Finding disposition

| Finding | Source disposition | Proof |
|---|---|---|
| P1-001 stale controller state after detach | Central close clears SDL handles, input/hotkey masks, late snapshot/baseline, rumble and sensor capability; a detached held hotkey publishes one late release | Rules P/Q; controller state-model contract |
| P1-002 split SDL close ownership | `closeJoystick` is the sole `SDL_GameControllerClose` / `SDL_JoystickClose` owner; early and late poll paths route through it | Rule P; source contract |
| P1-003 function-static lifecycle cadence | Attach/detach cadence moved to `EmuInstance::joystickLifecycleCheckCounter` | Rule O; strict instance-state audit |
| P1-004 late controller edge | Late held/pressed/released lives in a MelonPrime-only snapshot; gameplay combines the Qt edge with late press and never rewrites global emulator edges | Rule Q; frame-order and state-model contracts |
| P1-005 macOS runloop race | IOHID runloop publication/wakeup uses `atomic<CFRunLoopRef>` acquire/release operations | Rule R |
| P1-006 macOS event RMW | GCMouse callbacks use one serial handler queue, IOHID one worker, and each publishes its own packed cumulative total using load/store | Rule R; fetch-add ban |
| P2 early/late SDL duplication | Outer-frame lifecycle check performs the early SDL update; one guest-frame update occurs immediately before `RunFrameHook` | Rules O/Q |
| P2 full joystick mapping scans / lock hold | Config load builds a fixed active-binding table, merging duplicate physical bindings; late polling samples only active entries under the SDL lock and assembles numeric masks after unlock | Rule S; source contract |
| P2 wheel/GUI/persist claims | Empty sentinels are relaxed-load gated before exchange | Rule S |
| P2 panel delta RMW | GUI owns one packed cumulative total; reset publishes a boundary baseline and the emulation thread owns its cursor | Rule S; panel cumulative state model |
| P2 split center publication | Center X/Y is one packed 64-bit acquire/release publication | Rule S |
| P2 duplicate owner read | Non-Windows input reuses the result of `PlatformInputOwnerService::Update` | source review |
| P2 Linux source lookup | Filter-thread cache retains the last source-state pointer for the common single-device stream | Rule S |
| P2 XI2 X/Y decode | Normal decode is bounded to mask axes 0/1 while retaining packed-value cursor semantics | Rule S; source contract |

Lower-priority P3 proposals that require platform profiles or wider behavior
changes—keyboard mapping compilation, time-based detach cadence, broader
focus/capture packing, and IOHID metadata preclassification—remain unchanged.
They are not prerequisites in the audit's final P1/P2 priority order and no
unmeasured performance improvement is claimed for them.

## Concurrency and lifecycle proof

- The SDL lifecycle and snapshot fields are per `EmuInstance` and protected by
  the existing joystick mutex. Reconnect treats the first sample as a baseline,
  so held inputs do not create phantom presses.
- Global emulator hotkey press/release is finalized in `inputProcess`. The late
  controller sample is read only by MelonPrime gameplay immediately before its
  frame hook, so emulator commands cannot be repeated by the extra sample.
- Apple `GCDevice.handlerQueue` is the callback execution queue. Assigning one
  serial queue before installing every GCMouse handler makes fractional state
  and its total single-writer. IOHID owns a different total on one worker.
- Panel motion has one GUI writer. Reset records the current total in a separate
  baseline and never writes the producer total; motion published after that
  boundary remains visible when the emulation thread consumes the next delta.
- The Linux source pointer cache is filter-thread-only. `unordered_map` node
  references remain valid across rehash; the cache and map share the filter
  implementation lifetime.

## Hot-path cost delta

| Cost | Delta |
|---|---:|
| heap allocation / growing container in frame or event path | +0 |
| virtual / `std::function` dispatch | +0 |
| mutex | +0 |
| atomic objects | +0 net (packed center offsets the panel reset flag/baseline) |
| Windows Raw Input syscalls or algorithm | +0 |
| full joystick mapping scan per late sample | removed |
| duplicate physical joystick samples | removed by cold merge |
| macOS locked accumulator RMW per changed axis | removed |
| Qt panel locked accumulator RMW per changed axis | removed |
| coherent center publication | two atomics to one packed atomic |
| Linux common-source hash lookup | avoided after first event for that source |

## Validation classification

Completed locally on Windows:

```text
tools/build/windows/build-mingw-existing.bat --jobs 1: PASS
test_input_postpush_full_contract.py: PASS
test_mouse_input_savestate_contract.py: PASS
audit-melonprime-srp-performance.ps1: PASS (Rules O-S included)
audit-melonprime-thread-boundary.ps1 -Strict: PASS (0 findings)
audit-melonprime-instance-state.ps1 -Strict: exit 0 (12 allowlisted findings)
check-doc-links.py: PASS (748 local links)
renderer-physical-ab.ps1 Software + savestate-load: PASS
  run input-full-df820-final-20260831-software-savestate;
  process exit 0; startup/action markers 1/1; 1028 frame rows;
  bad markers 0; config/layer restore PASS
```

The existing configured build compiles the current source but may embed the SHA
from its earlier configure step. It proves compilation and linked tests, not a
clean-source provenance binary or a platform latency improvement. The bounded
Software smoke likewise used `-AllowUnverifiedBinary`; it proves Windows launch,
frame progression, savestate reconciliation and clean exit, not controller
behavior or a clean benchmark.

Not run on this Windows host:

- macOS GCMouse, IOHID, reconnect, focus/capture and callback-rate matrix;
- Linux X11/XWayland/Wayland, source switch, multi-instance, and 125/1000/8000 Hz matrix;
- physical SDL controller attach/detach/reconnect/switch and held-button matrix;
- clean before/after CPU, latency, locked-RMW, and syscall measurements;
- remote CI.

These remain **NOT RUN / NOT VERIFIED**, not inferred from static audits or the
Windows build.
