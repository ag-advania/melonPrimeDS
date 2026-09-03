# Pen Tablet Direct Aim

Opt-in ingress that lets pen tablets (XP-Pen, OpenTabletDriver and other
Windows Ink / pointer devices) drive the existing Stylus Mode direct-aim path
instead of only the DS touch screen.

- Setting: **Allow Pen Tablet Input for Direct Aim**
- Canonical key: `MelonPrime::CfgKey::StylusDirectAimAllowTabletInput`
- Serialized key: `Metroid.Enable.stylusDirectAimAllowTabletInput`
- Default: `false`
- Parent setting: `Metroid.Enable.stylusDirectAimWhileTouching`
  (itself a sub-option of `Metroid.Enable.stylusMode`)

The option is a performance gate, not only a preference. With it off,
`PointerWinFilter` is not allocated or installed, the native filter is not
installed, tablet tracking is not enabled, the direct-aim mailbox is not
consumed, and Windows pointer-source APIs are not called.
The DirectAimIngress object itself is embedded in `ScreenPanel` and remains
inert. The Raw Mouse frame projection is unchanged.

## Why it exists

`ScreenPanel::tabletEvent()` historically routed every `QTabletEvent` to
`EmuInstance::touchScreen()`. Direct aim therefore never saw pen movement even
when the option that turns the held touch action into normal relative aim was
enabled. Windows Raw Input only registers Mouse and Keyboard usages, and
`MelonPrimeRawInputState` deliberately ignores `MOUSE_MOVE_ABSOLUTE` reports,
so absolute devices produced no aim at all.

Rather than teaching the Raw Input subsystem to parse generic HID pen reports,
the pen is received through the OS pointer stack and normalized to a relative
delta before it joins the existing `ProcessAimInputMouse()` path. There is no
`ProcessAimInputTablet()`, no vendor SDK, no driver process/path/IPC detection,
and no VID/PID matching anywhere in the feature.

## Ownership

| Responsibility | Owner |
| --- | --- |
| Absolute-to-relative normalization, source authority | `MelonPrimeDirectAimSource.h` (`DirectAimSourceArbiter`) |
| GUI-thread capture lifecycle, publication, cursor policy | `MelonPrimeDirectAimIngress.h/.cpp` |
| Windows pointer/pen and injected-pointer message ingress | `MelonPrimePointerWinFilter.h/.cpp` |
| Qt tablet ingress, capture edges, cursor reconcile | `Screen.cpp` (`ScreenPanel`) |
| GUI → EmuThread transport | `MelonPrimeThreadBridge.h` direct-aim mailbox |
| Frame projection (one source per frame) | `MelonPrimeGameInput.cpp` |
| Aim transform | unchanged `ProcessAimInputMouse()` |

`DirectAimSourceArbiter` has no Qt, Win32 or gameplay dependency and is covered
by `tools/testing/direct-aim-source-tests.cpp`
(`melonprime_direct_aim_source_check`, built and run on every host).

## Host sources and authority

```
DirectAimHostSource : None, WinPointerPen, QtTablet, InjectedAbsolutePointer
```

Only **absolute** sources arbitrate here. The enumerator order *is* the
priority order, and one capture generation latches one of them. Every source
identity is carried as a `uint64_t` pointer ID; the injected route uses `0`:

- A capture begins with `None`; the first absolute sample latches.
- A strictly higher-priority source may pre-empt (and re-seeds its baseline).
- A lower or equal-priority route is suppressed for the rest of the capture.
- A contact-up drops only the matching source's baseline, preserving hover
  authority. A matching leave/capture-loss releases that source authority;
  the next absolute route may then seed.
- A terminal event for another source or pointer is a no-op.

This is what stops one physical pen movement from being counted three times
when it surfaces as `WM_POINTERUPDATE`, a `QTabletEvent`, and a synthesized
mouse message. There is no timer-based or idle-based source selection: only
explicit state transitions (capture edges, pointer identity, pointer leave,
focus, generation) change anything.

### The relative mouse never arbitrates

A mouse deliberately has no enumerator and never contests the latch. It keeps
its own Raw Input transport, and the frame projection falls back to it on every
frame this mailbox reports no motion. So inside a single hold:

- pen moving → the pen owns that frame,
- pen still (or lifted) → the mouse owns that frame,
- never both, and never summed.

Making the mouse a latching participant was the original design and it was
wrong twice over: a self-inflicted cursor move (the aim clip itself) arrives as
an injected absolute sample and could pre-empt it, and once any absolute source
had latched, the mouse was locked out for the rest of the hold. Frame-level
resolution removes both failure modes.

## Absolute normalization

Absolute pen coordinates are positions, not deltas. The first sample after any
boundary seeds the baseline and contributes `(0, 0)`; only later samples are
differenced. The fractional remainder is carried forward, so slow sub-pixel pen
motion accumulates instead of being quantized away, and the carry cannot drift.

The baseline is dropped (next sample re-seeds) on:

capture begin/end, touch-action release, focus loss, `WM_KILLFOCUS`,
`WM_CAPTURECHANGED`, matching `WM_POINTERUP`, `WM_DPICHANGED`,
`WM_DISPLAYCHANGE`, pointer-id change, source change, in-game state change
(ROM stop/reopen), recenter/warp and layout-generation resets, owner transfer,
and panel teardown. Matching `WM_POINTERLEAVE` and
`WM_POINTERCAPTURECHANGED` are source-lifetime releases, so they also clear
authority. Unrelated pointer terminal events do nothing.

Coordinate spaces are never mixed: the Windows route differences Win32 screen
pixels throughout, the Qt route differences Qt global logical coordinates
throughout, and a source change always re-seeds.

## Windows ingress

`PointerWinFilter` is a `QAbstractNativeEventFilter` owned by the capturing
`ScreenPanel`. It is installed on the touch-action press edge and removed on
release, so a mouse-only session never has it in the filter chain. It observes
and never consumes:

- `WM_POINTERDOWN` / `WM_POINTERUPDATE` → `GetPointerType()`, `PT_PEN` only,
  then `GetPointerPenInfo()`. Position comes from
  `pointerInfo.ptPixelLocation`; pressure, tilt and rotation are unused, and
  pointer history is not expanded (§16 of the design note: add it only if fast
  pen movement is measured to drop samples).
- `WM_POINTERUP` / `WM_POINTERLEAVE` / `WM_POINTERCAPTURECHANGED` first check
  the pointer type and the authoritative `uint64_t` pointer ID. Pointer-up
  drops only the matching baseline; leave/capture-loss releases matching
  authority. Touch or another pen pointer is ignored.
- `WM_MOUSEMOVE` → if `WinPointerPen` or `QtTablet` is authoritative, it is
  rejected before `GetCurrentInputMessageSource()`. Otherwise the filter calls
  `GetCurrentInputMessageSource()`. `IMDT_PEN` / `IMDT_TOUCH`
  are ignored (the pointer route already owns that movement), and only
  `IMO_INJECTED` becomes a generic injected absolute sample — this is *not*
  assumed to be any particular driver. An ordinary mouse is left entirely
  alone: it is carried by Raw Input, so this path must not touch it.
- `WM_KILLFOCUS` / `WM_CAPTURECHANGED` / `WM_DPICHANGED` / `WM_DISPLAYCHANGE`
  → baseline drop.

Messages are rejected up front unless `msg->hwnd` is the panel's top-level
window or its native render surface, so background instances are unaffected.
Because every submission is an absolute position, a message observed twice
(Qt filters both queued and sent messages) contributes a zero delta and is
harmless by construction.

## Cursor policy

Aim capture has two separable concerns, and `ScreenCursorPolicy` now keeps them
apart on both the acquire and the release side:

| | persistent request | transient active state |
| --- | --- | --- |
| acquire | `RequestAimCapture` | `ReconcileAimCapture` |
| release | `Unclip` | `Suspend` |

- The **request** publishes `clipWanted` → `captureWanted`, which is what
  `ShouldOwnRelativeAimInput()` consults to hand Raw Input to this instance.
  Direct aim issues it on every capture, tablet or not.
- The **active state** is presentation plus platform confinement. It performs
  no request write, so a repeated reconcile costs no cross-thread publication.

They used to be one function, `ClipCenter1px`, and that conflation is exactly
what broke mouse aim when this option shipped: skipping the "clip" for an
absolute capture silently skipped the ownership request too, so Raw Input never
took the device.

Confinement is chosen from an explicit mode the panel reports:

| `AimConfinement` | When | Effect |
| --- | --- | --- |
| `CenterPin` | tablet input off | 1px centered rect (unchanged) |
| `AimAreaBounds` | tablet input on | the aim containment rect (the rendered DS screens) |

`CenterPin` is a relative-device policy: the pointer position carries no
information, so pinning it is free. An absolute pen or injected pointer *is*
its coordinate signal, so the same pin would flatten every delta to zero.
Bounding to the aim rect keeps the pointer inside the window — no stray clicks,
no lost cursor — while leaving absolute sources a usable coordinate range. For
a tablet mapped to the whole screen the usable aim area is the area the tablet
maps onto that rect; running fullscreen gives the full tablet range. A
degenerate layout releases the clip rather than trapping the pointer.

## Frame projection

The GUI publishes into a dedicated mailbox in `MelonPrimeThreadBridge`, kept
separate from the panel-aim mailbox so the Raw Mouse single-producer fast path
keeps its exact shape. The mailbox is a packed cumulative POD total plus one
epoch word — `(capture generation << 8) | source` — so the emulation thread can
never observe a delta and an authority from different publications. There is no
queue, no event list, no mutex and no per-event allocation.

`UpdateInputStateImpl` consumes it once per frame only when all three cached
scalars are true: tablet input is allowed, relative capture is eligible, and
the configured stylus-touch action is held. An absolute source owns the frame
only when it published a **non-zero** delta; then that delta *replaces*
`m_input.mouseX/Y`, and Raw delta, the Raw late-latch in
`HandleInGameLogic`, the late-latch inside the native aim-delta
hook, and (on non-Windows) the post-aim cursor warp are all suppressed for that
frame. Otherwise the frame falls through to the ordinary Raw Mouse path
untouched. Raw and tablet motion are never summed, and neither device can lock
the other out.

## What is intentionally not here

- No tablet-specific sensitivity, acceleration or smoothing. Pen deltas share
  `ProcessAimInputMouse()` and every existing aim setting.
- No tablet late-latch or polling drain. The publication is event-driven; add
  a drain only if measurement shows it is needed.
- No pen-contact requirement. The touch action gates direct aim, so a hovering
  pen aims like a moving mouse (tablet tracking is enabled only for the held
  capture).
- No support yet for a *hardware* absolute mouse device (a virtual HID tablet
  reporting `MOUSE_MOVE_ABSOLUTE` rather than injecting or using Windows Ink).
  Raw Input correctly ignores those reports as positions, and they do not reach
  the injected fallback. OpenTabletDriver's relative and VMulti-relative output
  is already covered by the ordinary Raw Mouse path.

## Telemetry

`DirectAimTelemetry` (capture generations, per-route sample counts, duplicate
suppressions, baseline resets, source transitions) is compiled only into
developer builds and is reachable through `DirectAimIngress::Telemetry()`. On
Windows developer performance runs (`MELONPRIME_PERF=1`),
`PointerWinFilter` additionally aggregates target-message counts, pointer API
calls, accepted/rejected submissions, fast mouse rejects, and QPC ticks. It
prints one `native_filter` summary when the capture ends; no event is formatted
or logged. It is deliberately not wired into the `input_src` perf line: that
log format is shared with the existing Raw/panel counters and a new field would
change the `tools/perf/summarize-melonprime-perf.py` contract.

The isolated `tools/perf/direct-aim-mailbox-benchmark.cpp` measures only the
algorithmic arbiter/mailbox operations and explicitly is not end-to-end. The
explicit `melonprime_direct_aim_mailbox_spsc_benchmark` target measures the
GUI-producer/Emu-consumer mailbox at producer rates 125/500/1000/2000/8000 Hz
and consumer rates 60/120/144/240 Hz, including producer/consumer p50/p95/p99
and max samples. Its current adjacent-field layout is measured, not claimed to
be false-sharing-free.

## Verification status

Static audits, the Windows build, the arbitration unit tests, and the SPSC
matrix are the locally verified part. Per-device runtime behavior (XP-Pen,
OpenTabletDriver modes, multi-monitor/mixed-DPI layouts, 8 kHz mouse input,
and in-game A/B behavior) needs hardware and has not been observed here — see
the runtime matrix in the design note under `.codex/`. Do not put a device or
mode in release notes before it has actually been tried.
