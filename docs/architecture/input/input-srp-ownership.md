# Input SRP Ownership and Hot-Path Contract

This document is the implementation map for the input-side SRP work. It applies
the repository's [SRP/performance contract](../srp-performance-contract.md) to
the current input tree without turning each responsibility into a heap object or
generic command layer.

The governing rule is:

> Logical ownership is explicit; latency-critical data remains physically local.

`MelonPrimeCore` remains the per-instance storage and frame sequencer. The unit
listed as owner below is the only unit that knows how to derive, mutate and
invalidate that responsibility's internal state.

## Ownership and hotness map

| Responsibility / state | Owner and writer | Reset / invalidation owner | Readers | Cadence | Physical-move risk |
|---|---|---|---|---|---|
| `FrameInputState` (`down`, `press`, mouse delta, wheel count, move index) | `UpdateInputStateImpl` in `MelonPrimeGameInput.cpp` | input snapshot path; full clear only on timeline replacement | move/buttons, actions, Aim | once per emulated frame; bounded reentrant projection | Critical: aligned 64-byte CL0; do not copy or heap-separate |
| hotkey to `down` / `press` projection | `MelonPrimeInputProjection.h` | stateless | `UpdateInputStateImpl` | once per input snapshot | Low: header-only fixed arithmetic; no state to move |
| platform relative delta / Raw Input edge and wheel acquisition | platform filter plus `MelonPrimeInputSubscription` | platform owner and registration-generation transaction | `UpdateInputStateImpl` | per event plus one frame snapshot | Critical: single-writer atomics and generation ordering are load-bearing |
| SDL controller lifetime and capability state | `EmuInstance::openJoystick` / `closeJoystick` under `joyMutex` | physical owner publishes `joystickGameplayResetPending` only | absent-device lifecycle probe and physical sampler | lifecycle edge; active devices are attachment-checked by the required sample | High: GUI/config writers never mutate command/gameplay-derived masks |
| controller physical acquisition | EmuThread `sampleJoystickPhysicalLocked` under `joyMutex` | central lifetime owner | command and gameplay projection | once immediately before a running guest frame; once per low-rate paused outer cycle while connected | Critical: the initialized source count is explicit; fixed scratch is not maximum-size zeroed |
| controller global-command snapshot | EmuThread `projectJoystickCommandState` | `resetJoystickConsumerState`; reconnect uses a command-only baseline | outer Pause/Reset/frame/window command edge detection | running late sample, or paused outer-cycle refresh | Critical: remains live without guest frames and never mutates gameplay baseline/mailbox state |
| late SDL gameplay snapshot | EmuThread `projectJoystickGameplayState` | `resetJoystickConsumerState`; EmuThread owns the edge baseline | MelonPrime gameplay projection only | once immediately before `RunFrameHook` | Critical: reentrant samples refresh held state but never commit the press baseline |
| compiled joystick sources/fanout | `EmuInstance::inputLoadConfig` | config reload/device rebind | shared physical sampler/projector | cold rebuild; unique physical sources sampled once | High: fixed storage; asserted source indices, direction predicates and mask fanout run outside the mutex |
| Qt gameplay held/edge projection | GUI level atomics plus `qtGameplayPressPending`; normal `UpdateInputStateImpl` is sole consumer | GameInput lifecycle profiles clear/rebaseline it | MelonPrime gameplay projection only | event publication plus normal guest-frame late claim | Critical: sub-frame taps survive; reentrant frames never claim; wheel stays on its generation mailbox |
| Qt panel aim cumulative total | GUI-thread `AddPanelAimDeltaFromGui` | GUI publishes boundary+generation; emulation thread alone owns cursor+seen generation | non-raw Aim fallback | per Qt event plus one stable frame snapshot | Critical: generation-before/after retry prevents reset replay/duplication |
| input-surface snapshot | primary `MainWindow`/`ScreenPanel` for one `EmuInstance` | primary close/focus/capture lifecycle | owner selection, Aim center/HWND, cursor GUI | GUI edges plus per-frame read | High: secondary presentation windows cannot publish or clear shared authority |
| GUI focus/capture/panel policy | primary GUI surface publishes one changed-only packed atomic; `ReadGuiInputPolicyForEmu` decodes it | primary surface lifecycle | one immutable `GuiInputPolicySnapshot` per input decision | one acquire load per normal or reentrant input resolve | High: downstream receives the snapshot and cannot mix policy generations through field accessors |
| DS movement/button projection | `ProcessMoveAndButtonsFastImpl` | per-frame `InputReset` | DS input mask | active frame and reentrant frame | Critical: direct fixed lookup and one mask store |
| Aim config-derived Q14 values | Aim configuration section in `MelonPrimeGameInput.cpp` | `ApplyAimRuntimeConfig`, `RecalcAimFixedPoint` | `ProcessAimInputMouse`, native aim hook fragments | config / sensitivity hotkey only | Critical: fixed values stay beside residuals; no per-frame float work |
| Aim residuals and native delivery deltas | Aim state machine and aim hook unity fragments in `MelonPrimeGameInput.cpp` | `ResetAimTransientState` and Aim-owned transition paths | Aim state machine and hook dispatch | per active aim frame; lifecycle reset | Critical: current hot scalar cluster is load-bearing; no pointer owner or PIMPL |
| Aim layout/capture baseline | Aim/platform bridge in `MelonPrimeGameInput.cpp` | layout and platform reset paths | Aim acquisition | layout/capture transitions | High: preserve platform-specific syscall and warp gates |
| immediate input overlay latch | overlay section and hook fragment in `MelonPrimeGameInput.cpp` | `ResetImmediateOverlayInputState` via lifecycle profiles | input-overlay dispatcher | once per frame plus hook reads | High: edge is resolved once, never once per hook entry |
| native Biped Fire latch | Biped Fire section/hook fragment in `MelonPrimeGameInput.cpp` | `ResetNativeBipedFireInputState` via lifecycle profiles | Biped Fire dispatcher | once per frame plus hook reads | High: feature latch stays independent from the shared player identity |
| post-poll overlay player identity | `ApplyPostPollOverlayInput` coordinator | `ResetPostPollOverlayCoordinatorState` at shared lifecycle boundaries | immediate overlay and native Biped Fire | one guest read only while either feature is enabled | High: shared baseline has one owner; feature disable edges must not reset it |
| direct transform / weapon / zoom pending requests | specialized gameplay hook fragments | lifecycle boundary profile plus each request's specialized producer/consumer | native dispatchers and frame TTL maintenance | pressed edge and bounded TTL | Medium: fixed-size per-instance state; no queue or command bus |
| control-preset bindings | `PresetButtonBindings::BuildFromRecord` | game-join rebuild | movement, fire, zoom, morph/boost delivery | once per join, read per frame | High: precomputed fixed table avoids guest reads and branches per frame |
| ROM/player pointer cache (`HotPointers`) | ROM detect and game-join cache rebuild | ROM/session/join lifecycle | gameplay and Aim paths | cold rebuild, hot reads | Critical: tiered CL1+ layout; do not replace with a generic context copy |
| frame sequencing | `MelonPrimeCore::RunFrameHook` | `MelonPrimeCore` orchestration | all frame responsibilities | once per emulated frame | Critical: the documented 19-stage order is load-bearing |

The audited design mentioned an existing `FrameGameState`; the current source
tree has no such type. `HotPointers`, `StateFlags`, the control-preset snapshot,
and the specialized capability caches already provide the bounded frame/game
state needed by the hot path. A second generic context would duplicate state and
copies, so none is introduced. Optional guest facts remain lazy and specialized.

## Input lifecycle profiles

Top-level callers pass an `InputLifecycleBoundary` to
`ResetInputForLifecycleBoundary`. The GameInput owner, not the caller, defines
the physical fields in each profile.

| Boundary | Preserved reset semantics |
|---|---|
| `EmuStart` | Aim transient delivery, immediate overlay, direct transform |
| `Boot` | `EmuStart` plus native Biped Fire |
| `EmuStop` | direct transform and native Biped Fire only |
| `GameLeave` | immediate overlay, direct transform, native Biped Fire |
| `FocusLoss` | direct transform and native Biped Fire only |
| `GameJoin` | immediate overlay, direct transform, native Biped Fire, weapon and Direct Invocation requests |
| `SavestateLoad` | Aim transient delivery plus every overlay and pending request above |

Every profile also invalidates the Qt gameplay level baseline and clears the
event-edge mailbox. A real GUI press publishes a pending edge even if release
occurs before the next guest frame. Reentrant FrameAdvance neither clears nor
claims that edge.

The remaining asymmetry is intentional behavior preservation. In particular, architecture
cleanup must not widen `EmuStart` or `EmuStop` without a dedicated behavior
change and runtime evidence.

Config disable edges use the corresponding narrow owner API, for example
`ResetDirectTransformInputState`, `ResetImmediateOverlayInputState`, and
`ResetNativeBipedFireInputState`. The two post-poll features reset only their
own latches on an enabled-to-disabled transition. The shared
`m_postPollOverlayLocalPlayerPtr` baseline is reset by the coordinator at each
historically equivalent lifecycle boundary, not by either feature reset.

## Hot/cold and dependency direction

The hot direction remains:

```text
platform / Qt hotkey state
    -> fixed FrameInputState projection
       -> direct movement / action conditions / Aim
          -> specialized guest helper or hook mailbox
```

It must not become:

```text
input -> allocated action list -> generic dispatcher -> router -> guest
```

The following stay off the steady-state input path:

- config table lookup, key construction, clamp and float conversion;
- ROM detection metadata preparation;
- heap allocation and growing containers;
- virtual, `std::function`, function-table or string dispatch;
- unbounded queues, generic per-action atomics, or steady locked RMWs;
- device enumeration and unconditional OS polling;
- diagnostic formatting.

ROM addresses, Direct Invocation protocols and hook installation remain below a
narrow gameplay action/hook boundary. Platform acquisition must not interpret
Morph, Boost, weapon, Zoom, hunter or ROM semantics.

## Load-bearing fast paths

- `FrameInputState` remains fixed-size, aligned and passed through the Core by
  reference/member access; no duplicate snapshot is constructed.
- `ProcessAimInputMouse` retains fixed-point Q14 scales and residuals.
- no physical delta means the Aim path returns before multiply, clamp or output;
  residual carry is not autonomous motion.
- Native/Low-Latency and Touch/Dual delivery remain specialized paths.
- LateLatch only re-polls after a reentrant `FrameAdvance` opened a sampling
  window; a normal frame performs no extra Raw Input syscall.
- the Raw-owner wheel count and generation-tagged Qt fallback remain exclusive;
  the count is never reduced to a boolean at the platform boundary.
- non-Windows acquisition resolves and caches the frame's warp decision once;
  `ProcessAimInputMouse` uses that scalar. GCMouse/raw steady state emits no
  `GuiRequestRecenter` from capture-wanted alone.
- Linux RawMotion has one accumulator writer. A lock-free packed 64-bit total
  publishes modulo-32-bit X/Y with one `load(relaxed) + store(release)` per
  nonzero event; the frame reader uses one acquire and wrap-safe subtraction.
  Availability and first-motion readiness share one packed state byte and one
  acquire in the source resolver. Absolute baselines and per-device fractional
  residuals are reset through a cold mailbox at the filter-loop/event-batch
  boundary; the normal RawMotion arithmetic does not pay a reset-atomic check.
- SDL physical lifetime has one mutex-held owner. One physical sample projects
  separately to application-global command state and gameplay state. Running
  samples once immediately before `RunFrameHook`; paused outer cycles refresh
  command state only, so controller Pause release/re-press remains live without
  advancing the gameplay previous mask, baseline or press mailbox. Only a
  normal guest frame commits the gameplay press baseline. Reconnect establishes
  both baselines without a phantom command/gameplay press. An absent device is
  enumerated only at the existing per-instance lifecycle cadence.
- Joystick sampling walks fixed physical-source and fanout tables built on
  config load. Each unique SDL button/hat/axis is fetched once under the mutex;
  direction predicates and DS/hotkey mask assembly run after unlock. Fanout
  indices are asserted below the initialized source count, so the hot fixed
  scratch does not clear its unused maximum-size tail. Active running devices
  use this required sample for attachment checking instead of issuing a second
  cadence `SDL_JoystickUpdate` in the same outer cycle.
- GCMouse callbacks and ownership transitions share one serial `handlerQueue`.
  Connect claims `BackendGc` before handler install; disconnect removes the
  handler, drains queued callbacks, clears the queue-local producer gate, then
  releases IOHID. Each backend retains its own packed cumulative total.
- Windows hidden-window Raw ownership is also subscription-local and
  creator-thread affine. When an inactive subscription reacquires ownership,
  the filter destroys its old hidden HWND and creates a replacement before the
  new registration; this invalidates queued `WM_INPUT` from the old epoch
  without adding steady-frame work. The buffered Raw drain is private to
  `RawInputWinFilter`, and the only foreign-subscription reset remains under
  its recursive subscription mutex.
- `HiddenWndProc` is deliberately minimal on the event-hot path: it loads the
  active subscription under that mutex, compares only `hiddenWindow == hwnd`,
  and then processes the handle. `GetWindowLongPtr*`, `GetCurrentThreadId`, Qt,
  config and clock queries stay out of `WM_INPUT`; creator-thread proof and
  epoch recreation remain cold lifecycle work. Raw contention/recovery
  telemetry is compiled only by `MELONPRIME_ENABLE_RAW_INPUT_PERF_TELEMETRY`
  and then enabled at runtime only with `MELONPRIME_RAW_INPUT_PERF=1`.
- Windows frame ownership and snapshot validation use the same
  `UpdateOwnerAndSnapshot*` transaction. The returned owner bit is the result
  of the eligible-owner check and the generation-validated snapshot, so the
  frame projector does not repeat owner resolution in a second mutex
  transaction.
- Qt panel aim has one GUI writer and one emulation-thread cursor. Both consumer
  read and discard paths retry until generation is stable around the total, so
  a concurrent GUI reset cannot replay motion or move the cursor backward.
- rare config, cursor-mode, and wheel consumers load the empty sentinel before
  their exchange claim. A producer racing an empty load remains pending for the
  next normal frame. Wheel generation-only publications are nonzero and are
  still claimed at their boundary.
- wheel-up/down hotkey masks are projected in `EmuInstance::inputLoadConfig`;
  neither the Windows raw wheel path nor the Qt wheel pulse path scans `HK_MAX`.
- tracked mouse press/release and lost-release recovery share five
  cold-precomputed masks. Normal movement performs only load-first stale tests;
  correcting RMWs occur solely when a lost release left a bit set. On macOS,
  the GUI mouse-move path queries global button state only while a compact
  armed mask indicates that a tracked press could still need recovery.
- focused/capture/panel policy is one packed changed-only GUI publication and
  one acquire snapshot per input decision; downstream capture resolution uses
  that immutable snapshot. Stylus publication is changed-only too. GUI
  reconciliation uses a work revision, so steady raw Aim draw calls stop before
  CAS/queued invocation.
- non-Windows Aim resolves process ownership once and carries source,
  raw-active, and warp policy in one frame result reused by Aim and UI.
- Native Wayland relative motion, when enabled, stores a borrowed primary-core
  `ThreadBridge` target during cold authority transitions. The event callback
  performs integer `wl_fixed_t` residual accumulation and direct bridge
  publication; it contains no erased callback or per-event policy/core lookup.
- `MelonPrimeInputSubscription` production state contains no debug counters.
  Input debug counters and Linux diagnostic formatting are compiled only under
  `MELONPRIME_ENABLE_INPUT_DEBUG_TELEMETRY`, which release presets force off.
- The controller binding-program generation is plain mutex-guarded state; its
  publication and activation both occur under `joyMutex`, leaving one
  synchronization authority.

## Frame-order contract

`RunFrameHook` keeps the exact order recorded in
`srp-performance-contract.md`: reentrant handling, cold config reload, running
guard, focus snapshot, input snapshot, DS input reset, stylus unblock, global
hotkeys, ROM detection, in-game state, join, battle-runtime transition, HUD
pre-frame clamp, damage notification, focused gameplay/menu input, cursor/touch,
focus-transition reset, pending-request tick, and running-guard clear.

The enclosing frame loop performs RTC sync and its shader-readiness decision
before the limiter. After the late input marker, the critical sequence is
controller refresh, `RunFrameHook`, `SetKeyMask`, then `NDS::RunFrame`. The
orchestration method is allowed to remain long where ordering itself is the
important visible contract.

## Proof per change

For input SRP changes, review and run:

```text
pwsh -NoProfile -File tools/ci/audits/audit-melonprime-srp-performance.ps1
pwsh -NoProfile -File tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict
pwsh -NoProfile -File tools/ci/audits/audit-melonprime-instance-state.ps1 -Strict
python tools/testing/test_mouse_input_savestate_contract.py
git diff --check
```

The SRP audit ratchets the owner definitions, lifecycle profiles, forbidden hot
abstractions and `RunFrameHook` order. Rule L2 additionally pins macOS
source-resolved warp policy, Linux single-writer/load-first shapes, disabled
overlay guest-read rejection, shared coordinator ownership, rare command claims,
and cold wheel-mask projection. Rules O-S additionally pin controller lifecycle
ownership and reentrant late-edge isolation, macOS producer serialization and
availability, packed bridge publications, and the Linux common-source/X-Y
decode fast path. Rules T-V pin post-limiter ordering, fixed-mask mouse release
recovery, and changed-only bridge publications. Rules W-AB pin event-edge
conservation, stable reset snapshots, serialized macOS handoff, primary input
surface authority, physical-source controller compilation, packed Linux totals,
frame-result reuse, and revision-driven GUI reconciliation. Rule AC pins paused
controller command liveness, command/gameplay owner separation, one running
physical sample, initialized-source scratch, and one coherent GUI policy read.
Rules BE-BM pin the Windows hidden-HWND registration-epoch fence, the private
buffered-drain API, the mutex contract for foreign lifecycle resets, the
minimal `WM_INPUT` callback and its dedicated telemetry gate. Rule BN pins the
cadence-first absent-controller probe. The Savestate contract additionally pins the
next-normal-frame reconciliation and the full input reset profile.

A compile/static pass is not a runtime latency claim. Changes to Aim arithmetic,
Raw Input consume semantics, polling cadence, atomics, syscalls, guest read/write
counts or hot layout require before/after measurement. Hardware acceptance
includes 1000 Hz and 8000 Hz mouse input, wheel bursts, focus/capture transitions,
Savestate load, controller transitions, renderer switches and multi-instance use.
