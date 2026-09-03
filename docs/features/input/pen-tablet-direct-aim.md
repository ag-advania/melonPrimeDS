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

The option is a performance gate, not only a preference. With it off, none of
the machinery below is constructed, installed, or consulted, and the Raw Mouse
frame projection is unchanged.

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
DirectAimHostSource : None, WinPointerPen, QtTablet,
                      InjectedAbsolutePointer, RawRelativeMouse
```

The enumerator order *is* the priority order. One capture generation latches
one logical source:

- A capture begins with `None`; the first eligible source latches.
- A strictly higher-priority source may pre-empt (and re-seeds its baseline).
- A lower or equal-priority route is suppressed for the rest of the capture.

This is what stops one physical pen movement from being counted three times
when it surfaces as `WM_POINTERUPDATE`, a `QTabletEvent`, and a synthesized
mouse message. There is no timer-based or idle-based source selection: only
explicit state transitions (capture edges, pointer identity, pointer leave,
focus, generation) change anything.

`RawRelativeMouse` is a latch only. Its motion keeps travelling through Raw
Input and the existing late-latch; it never enters the direct-aim mailbox.

## Absolute normalization

Absolute pen coordinates are positions, not deltas. The first sample after any
boundary seeds the baseline and contributes `(0, 0)`; only later samples are
differenced. The fractional remainder is carried forward, so slow sub-pixel pen
motion accumulates instead of being quantized away, and the carry cannot drift.

The baseline is dropped (next sample re-seeds) on:

capture begin/end, touch-action release, focus loss, `WM_KILLFOCUS`,
`WM_CAPTURECHANGED`, `WM_POINTERUP`, `WM_POINTERLEAVE`,
`WM_POINTERCAPTURECHANGED`, `WM_DPICHANGED`, `WM_DISPLAYCHANGE`, pointer-id
change, source change, in-game state change (ROM stop/reopen), recenter/warp
and layout-generation resets, owner transfer, and panel teardown.

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
- `WM_POINTERUP` / `WM_POINTERLEAVE` / `WM_POINTERCAPTURECHANGED` → baseline drop.
- `WM_MOUSEMOVE` → `GetCurrentInputMessageSource()`.
  `IMDT_PEN` / `IMDT_TOUCH` are ignored (the pointer route already owns that
  movement). `IMO_INJECTED` becomes a generic injected absolute sample — this
  is *not* assumed to be any particular driver. Everything else latches
  `RawRelativeMouse`.
- `WM_KILLFOCUS` / `WM_CAPTURECHANGED` / `WM_DPICHANGED` / `WM_DISPLAYCHANGE`
  → baseline drop.

Messages are rejected up front unless `msg->hwnd` is the panel's top-level
window or its native render surface, so background instances are unaffected.
Because every submission is an absolute position, a message observed twice
(Qt filters both queued and sent messages) contributes a zero delta and is
harmless by construction.

## Cursor policy

Center clipping is a `RawRelativeMouse` policy. Clipping the cursor to one
pixel destroys the coordinate signal an absolute source is made of, so:

| Source | 1px center clip |
| --- | ---: |
| `RawRelativeMouse` | yes |
| `WinPointerPen` | no |
| `QtTablet` | no |
| `InjectedAbsolutePointer` | no |

With the option off, capture start clips immediately, exactly as before. With
it on, capture starts unclipped and the clip is applied only when the first
hardware mouse movement latches `RawRelativeMouse` — one mouse event of
exposure. Cursor mutation always happens on the GUI thread, through a callback
the panel installs at capture begin.

An absolute source therefore leaves the pointer free for the whole capture.
For a tablet in absolute mode this means the usable aim area is the area the
tablet maps onto the window; running fullscreen gives the full tablet range.

## Frame projection

The GUI publishes into a dedicated mailbox in `MelonPrimeThreadBridge`, kept
separate from the panel-aim mailbox so the Raw Mouse single-producer fast path
keeps its exact shape. The mailbox is a packed cumulative POD total plus one
epoch word — `(capture generation << 8) | source` — so the emulation thread can
never observe a delta and an authority from different publications. There is no
queue, no event list, no mutex and no per-event allocation.

`UpdateInputStateImpl` consumes it once per frame, and only when the option is
on. When the published source is absolute, that delta *replaces*
`m_input.mouseX/Y`; Raw delta, the Raw late-latch in `HandleInGameLogic`, the
late-latch inside the native aim-delta hook, and (on non-Windows) the
post-aim cursor warp are all suppressed for that frame. Raw and tablet motion
are never summed.

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
developer builds and is reachable through `DirectAimIngress::Telemetry()`. It
is deliberately not wired into the `input_src` perf line: that log format is
shared with the existing Raw/panel counters and a new field would change the
`tools/perf/summarize-melonprime-perf.py` contract. Nothing is formatted or
logged per event in either build type.

## Verification status

Static audits, the Windows build, and the arbitration unit tests are the
verified part. Per-device runtime behavior (XP-Pen, OpenTabletDriver modes,
multi-monitor and mixed-DPI layouts) needs hardware and has not been observed
here — see the runtime matrix in the design note under `.codex/`. Do not put a
device or mode in release notes before it has actually been tried.
