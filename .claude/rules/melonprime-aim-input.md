# MelonPrime Aim/Input Notes (No CustomHud)

This document tracks the MelonPrime path from input capture to frame input state and final aim RAM writes.

Platform scope: the sections below describe the Windows Raw Input path first. On macOS (added
2026-07), `MelonPrimeRawInputMacFilter.{h,mm}` provides RawInput-equivalent aim deltas via
GCMouse first, then IOHIDManager as a fallback (unaccelerated HID X/Y counts, fetch-and-clear at
frame snapshot). GCMouse is primary because it does not need the Input Monitoring TCC permission;
IOHID is only opened after a short grace period when no GCMouse backend appears. On Linux/X11,
`MelonPrimeRawInputLinuxFilter.{h,cpp}` provides the equivalent through XInput2 `XI_RawMotion`.

Mouse buttons and keyboard hotkeys stay on the Qt event / SDL path (`EmuInstance::onMousePress`,
`emuInstance->hotkeyMask`) on non-Windows platforms. They are intentionally not captured by the
raw-delta filters so a physical click cannot create duplicate press edges.

The QCursor center-delta method remains the fallback when the macOS raw backends are unavailable.
On Linux the source of truth is the XInput2 raw filter when available (2026-07-03): relative
axes are used as-is, absolute pointing devices (VirtualBox's integrated tablet) are converted to
deltas per-device inside the filter, so cursor warps and VBox host re-syncs cannot corrupt aim.
The Qt mouse-move accumulator in `ScreenPanel` is the non-XCB (Wayland) fallback. See §9 and the
macOS/Linux notes in [build.md](build.md).

## 1. End-to-End Pipeline

1. `RawInputWinFilter` / `InputState` capture RawInput (Windows).
2. `MelonPrimeCore::UpdateInputState[Reentrant]()` builds `m_input`.
3. `ProcessMoveAndButtonsFast()` maps movement/buttons to the DS input mask.
4. `ProcessAimInputMouse()` or `ProcessAimInputStylus()` runs.
5. In mouse mode the per-frame aim is delivered by one of three mechanisms (see §6):
   - default: written directly to `*m_ptrs.aimX / *m_ptrs.aimY`
   - Native Aim Delta Hook (developer-only): the per-frame delta is staged in
     `m_nativeAimDeltaX/Y` and injected into the game's own aim register by an ARM9 hook
   - Low-Latency Aim Hook (`LowLatencyMode`): the game's aim/orientation vector is rewritten
     directly by an ARM9 hook — a separate path from the `aimX/aimY` delta write

Main implementation files:
- `src/frontend/qt_sdl/MelonPrimeGameInput.cpp`
- `src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp`
- `src/frontend/qt_sdl/MelonPrimeRawInputState.cpp`
- `src/frontend/qt_sdl/MelonPrimeRawInputMacFilter.mm`
- `src/frontend/qt_sdl/MelonPrimeRawInputLinuxFilter.cpp`
- `src/frontend/qt_sdl/Screen.cpp` (non-Windows cursor grab/recenter)

## 2. FrameInputState and Input Bits

- `FrameInputState` is a 64-byte struct in `MelonPrime.h`.
- Key fields:
  - `down`: held buttons
  - `press`: edge-triggered presses
  - `mouseX/mouseY`: per-frame mouse delta
  - `wheelDelta`
  - `moveIndex`: 4-bit movement index (F/B/L/R)
- Hotkey-to-internal-bit projection is centralized in `ProjectDownState()` and `ProjectPressMask()`.

## 3. UpdateInputState Variants

- `UpdateInputStateImpl<false>`:
  - Normal frame path
  - Uses `PollAndSnapshot` (advances edge state)
  - Reads `wheelDelta`
  - P-47: clears `m_didFrameAdvanceSinceSnapshot` right after `PollAndSnapshot`, so
    `LateLatchMouseDelta` is skipped on normal frames (no `FrameAdvance` since the snapshot)
- `UpdateInputStateImpl<true>`:
  - Re-entrant path during `FrameAdvance`
  - Uses `PollAndSnapshotNoEdges` (does not advance edge state)
  - Forces `press=0`, `wheelDelta=0`
- Goal:
  - Preserve outer-frame edge behavior while still updating input state safely in re-entrant execution.

## 4. Aim-Block Management

- `m_aimBlockBits` (`AimBlockBit`) stores block causes:
  - `AIMBLK_CHECK_WEAPON`
  - `AIMBLK_MORPHBALL_BOOST`
  - `AIMBLK_CURSOR_MODE`
  - `AIMBLK_NOT_IN_GAME`
- `SetAimBlockBranchless()` toggles bits with minimal branching.
- At the start of `ProcessAimInputMouse()`:
  - if `m_aimBlockBits != 0` or
  - if `m_isLayoutChangePending == true`,
  then control is diverted to `HandleAimEarlyReset()`.

## 5. Mouse Aim Path (`ProcessAimInputMouse`)

- P-44: zero-delta fast skip — when the mouse delta is zero **and** both residuals are zero,
  the function returns immediately (no IMUL / clamp / output).
- Q14 fixed-point residual accumulation (only on a nonzero delta):
  - `m_aimResidualX/Y += delta * m_aimFixedScaleX/Y`
  - Residuals are clamped to `AIM_MAX_RESIDUAL` (`ClampAimResidual`)
- Two output paths:
  - Direct path (`DisableMphAimSmoothing=true`)
    - `>> AIM_DIRECT_BITS` (=12) output → 4× the granularity of the legacy `>> 14`
    - No deadzone
  - Legacy path (`false`)
    - `ApplyAim()` branchless deadzone/snap; preserves the `>> 14`-based legacy behavior
- Output delivery:
  - Direct path, Native Aim Delta Hook ON (`m_enableNativeAimDeltaHook`, developer-only):
    `m_nativeAimDeltaX/Y = outX/outY` — the ARM9 hook applies it into the native aim register
    (the C++ side does **not** write `aimX/aimY`).
  - Otherwise (direct fallback or legacy path): `*m_ptrs.aimX = outX; *m_ptrs.aimY = outY`.
- The consumed integer portion is subtracted (`outX << bits`); the fractional remainder carries
  to the next frame, so floor-rounding (`>>`) introduces no long-term drift.
- If `AimAccumulator` is disabled, residuals are cleared at frame end (no carry).
- LateLatch (P-47): `HandleInGameLogic` calls `m_rawFilter->LateLatchMouseDelta()` just before
  `ProcessAimInputMouse` **only** when `m_didFrameAdvanceSinceSnapshot` is set (a morph/weapon
  `FrameAdvance` happened this frame, opening a ~32–96 ms window). It re-drains the kernel buffer
  and **adds** any newly-arrived delta. Normal frames skip it (~40–100 ns window not worth the syscall).
- Non-Windows cursor recenter:
  - The cursor-delta **fallback** needs the cursor returned to `m_aimData.centerX/Y` after any
    consumed movement, including early returns where residuals changed but output was still zero.
  - Linux never recenters per-frame (`warpCursorAfterAim == false` — the Qt fallback uses
    previous-position differencing, which a warp here would corrupt). Containment is the
    panel's >96px threshold warp; every warp re-seeds the fallback baseline. See §9.
  - `unfocus()` must call `unclip()` on Linux too; otherwise Escape leaves the cursor
    hidden/locked.

## 6. Native / Low-Latency Aim Injection Mechanisms (newer)

Beyond the classic `aimX/aimY` write, three ARM9-hook-based mechanisms can take over or augment
aim/fire. All are configured in `ReloadConfigFlags()` and dispatched by the shared ARM9 hook
(`MelonPrimeArm9Hook.cpp` → `DispatcherCallback`). "Developer-only" means compiled/forced off
unless `MELONPRIME_ENABLE_DEVELOPER_FEATURES`.

### 6.1 Native Aim Delta Hook (`Metroid.Aim.NativeHookMode`, developer-only)
- `m_nativeAimHookMode` is forced to `0` in release builds; `m_enableNativeAimDeltaHook = (mode != 0)`.
- Only meaningful on the direct path (`DisableMphAimSmoothing=true`).
- Modes: `1` RegisterInjection, `2` PostFoldWrite — two ROM-hook strategies that inject the
  per-frame `m_nativeAimDeltaX/Y` into the game's own aim register instead of the C++ side writing
  `aimX/aimY`. Implemented in `MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc` /
  `...PostFoldWriteVersion.inc`. RegisterInjection re-runs the LateLatch + residual math at the
  hooked PC for the lowest possible latency.

### 6.2 Low-Latency Aim Hook (`Metroid.Aim.LowLatencyMode`, release-available)
- Forced `Off` unless `DisableMphAimSmoothing=true`; also inert in stylus mode.
- A **separate** aim mechanism: at the aim-function exit PCs it rewrites the player's orientation
  basis (forward/side/up vectors at `CPlayer +0x4C / +0x58 / +0x64`) directly.
- Release-available modes: `ImmediateSync` (snap orientation straight to the target) and
  `MoonLikeAim` (chase the target with tunable Q12 step sizes:
  `MoonLikeAimNormalStepQ12` / `FastStepQ12` / `FastThresholdQ12`).
- `InstantAimFollow` is developer-only (`LowLatencyMode = 3`) and is backed by the separate
  `MelonPrimePatchInstantAimFollow` patch, not the runtime exit-PC hook. Public builds normalize
  existing `InstantAimFollow` configs to `ImmediateSync`.
- Hook implemented in `MelonPrimePatchLowLatencyAimHook.inc`; registered/dispatched via
  `MelonPrimeArm9Hook.cpp`.

### 6.3 Native Biped Fire (`BipedFireMethod`, developer-only)
- When enabled (`m_enableNativeBipedFire`, forced off in release), `ProcessMoveAndButtonsFast`
  leaves `INPUT_L` released — it does **not** synthesize the legacy fire input. The shoot edge is
  owned by the ARM9 fire-edge hook (`MelonPrimePatchNativeBipedFireHook.inc`). The `modBits` /
  `nativeFireMask` logic in `ProcessMoveAndButtonsFastImpl` implements this split.

## 7. Stylus Mode

- `ProcessAimInputStylus()` is straightforward:
  - `TouchScreen(touchX, touchY)` while `emuInstance->isTouching`
  - otherwise `ReleaseScreen()`
- Some operations (for example morph/weapon actions) set `BIT_BLOCK_STYLUS` to avoid interference.

## 8. RawInput Layer Notes (Windows)

- `RawInputWinFilter`:
  - Uses Qt target when Joy2Key is ON, hidden window when OFF
  - Splits `PollAndSnapshot` and `DeferredDrain`
  - Handles `WM_INPUT` in `HiddenWndProc` to avoid loss
- `InputState`:
  - Uses `processRawInputBatched()` for batched reads
  - Prebuilds hotkey masks via `setHotkeyVks()`
  - `snapshotInputFrameNoEdges()` preserves outer `m_hkPrev` state

## 9. Linux Raw / Relative Aim Notes

Since 2026-07-03, Linux aim uses XInput2 `XI_RawMotion` as the source of truth when available.
The key insight is to synthesize deltas at the **device level**, which makes them immune to
cursor warps: `XWarpPointer` and VirtualBox's host-position re-sync move the *cursor*, never a
device's own axis state.

History of the two failed schemes (do not regress to either):

1. *Center-delta polling / warp-per-event Qt accumulation*: under VirtualBox mouse integration
   the guest cursor is slaved to the **host** pointer's absolute position. Warping to center is
   undone by the next host event, so the full `hostPos - center` offset was re-added on every
   event instead of the increment — a constant bottom-right drift with no aim control.
2. *Naive raw-relative promotion*: `raw_values` from an **absolute** pointing device (the VBox
   tablet) are positions, not deltas. Treating them as deltas produced the same drift; the
   "XWarpPointer emits RawMotion" observation was this absolute-device behavior, not a real
   warp echo.

A third failure mode (2026-07-03, VM): **XWayland sessions accept the XI2 `XI_RawMotion`
selection but never deliver raw events** — `isAvailable()` alone made the runtime trust a silent
raw source and aim froze entirely. Hence the `hasReceivedMotion()` gate below.

Current implementation:

- `LinuxRawInputFilter` (`MelonPrimeRawInputLinuxFilter.cpp`):
  - queries each source device's valuator modes once via `XIQueryDevice` (`sourceid`-keyed cache,
    filter-thread-only);
  - relative axes 0/1 accumulate as-is; **absolute axes accumulate the difference of successive
    values** (first event seeds the baseline);
  - axes above 0/1 (scroll-wheel valuators on many drivers) are never fed into aim;
  - `resetAll()` invalidates the absolute baselines (`absBaseInvalid`) so a focus gap cannot
    produce one huge catch-up delta, but intentionally does **not** clear `receivedMotion`
    (a static session property; clearing it would flap the aim source on every focus loss);
  - `isAvailable()` means `XOpenDisplay` + XInput2 query + `XISelectEvents(XI_RawMotion)` succeeded;
  - `hasReceivedMotion()` means at least one nonzero raw delta actually arrived.
- **Raw mode gate** (`IsLinuxRawAimActive()` and the matching check in `UpdateInputStateImpl`):
  `isAvailable() && hasReceivedMotion()`. Until the first real raw delta proves the session
  delivers raw motion, the Qt fallback owns aim; the first raw event switches over.
- `MelonPrimeCore::Initialize()` only acquires the filter when
  `QGuiApplication::platformName() == "xcb"`. Wayland does not expose a global raw mouse stream.
- In `UpdateInputStateImpl` (Linux): raw mode → `fetchMouseDelta()` is the aim delta and the
  panel accumulator is dropped (`resetAimMouseDelta()`); otherwise the Qt accumulator
  (`ScreenPanel::getAimMouseDelta()`) is authoritative **even when zero** (falling through to
  `QCursor::pos() - center` would drift now that the cursor is not recentered every event).
- `ScreenPanel::mouseMoveEvent` (Linux aim frames):
  - raw mode → containment only: invalidate the fallback baseline and warp back to center when
    the hidden cursor strays >96px (warping every event fights VBox's re-sync and storms events);
  - fallback → **previous-position differencing**: delta = `global - aimLastGlobal`, then
    re-seed. Consecutive positions are pure pointer motion, so VBox host re-syncs and our own
    warps cannot be double-counted. Every warp (threshold containment, `clipCursorCenter1px`,
    `ShowCursor`, layout resets) invalidates the baseline via `resetAimMouseDelta()` so the jump
    is never counted as motion.
- `ProcessAimInputMouse` never warps per-frame on Linux (`warpCursorAfterAim == false` — a warp
  there would corrupt the prev-position fallback); containment is the panel's threshold warp.
- X11 recenter uses `MelonPrime::LinuxWarpCursorGlobal` (`XWarpPointer` on a thread-local
  `Display*`). Do not use `QCursor::setPos` on X11; it can fail under VirtualBox guest mouse
  integration or when called off the GUI thread.
- `ScreenPanel::clipCursorCenter1px()` hides the cursor and recenters it. Do not use
  `grabMouse(Qt::BlankCursor)` here unless Escape/unfocus and keyboard delivery are re-tested on
  Linux; a previous attempt left the cursor hidden after Escape in the VM.
- Known limitation with absolute devices (VBox integration ON): the guest only receives events
  while the **host** cursor is over the VM window, and the guest cannot recenter the host
  cursor — the pointer leaves the window / pegs at an edge within a second and input stops.
  Sustained mouse-look is structurally impossible in that mode. Abs devices still get pixel
  scaling (XI axis range → screen), a ±300px teleport guard, and warp re-seeding
  (`NotifyCursorWarp`), so short motions behave sanely. **Aim testing in the VM requires mouse
  integration OFF** (Host+I → capture; verified working 2026-07-03, `X=rel Y=rel`). Real Linux
  hardware mice are relative and unaffected. `MELONPRIME_INPUT_DEBUG=1` logs the full pipeline
  (raw device modes / per-second sums / gate states / consumption source) for diagnosis.

RawMotion parsing rules:

- XInput2 reports one `raw_values` entry for each set bit in `valuators.mask`.
- Axis 0 maps to X and axis 1 maps to Y. Do not treat "first received value" as X unconditionally:
  a Y-only event would become horizontal aim.
- If axis 0/1 are absent, the code keeps a conservative first-two-relative-values fallback for
  unusual devices, but normal mouse devices should hit the explicit axis 0/1 path.
- The filter captures only relative motion. Buttons and keyboard state remain owned by Qt/SDL
  hotkey handling to avoid double press edges.

Troubleshooting signals:

- Launch from a terminal and look for `[MelonPrime] linux input: XInput2 RawMotion active`.
- That log only proves XInput2 selection succeeded. Aim should still work without using raw deltas,
  because Linux input is mouseMoveEvent accumulation + recenter.
- In a VM, disable host mouse integration or switch the Ubuntu login session to **Ubuntu on Xorg**
  before testing. Wayland sessions are not the primary supported path for FPS aim.
- If the view spins, inspect recenter paths first: the Linux mouseMoveEvent path must ignore
  zero-delta warp events and recenter through `LinuxWarpCursorGlobal`. If the view does not move at
  all, inspect whether `ScreenPanel::mouseMoveEvent` is firing while the core is focused/in-game.

## 10. Sensitivity Cache and Recalculation

- `RecalcAimSensitivityCache()`:
  - Recomputes `m_aimSensiFactor` / `m_aimCombinedY` from `AimSens` and `AimYScale`
- `ApplyAimAdjustSetting()`:
  - Applies `AimAdjust`
- `RecalcAimFixedPoint()` then refreshes:
  - fixed-point scaling values
  - minimum delta values
  - snap/deadzone thresholds
  and clears stale residuals.
- Zoom aim sensitivity uses `MelonPrime::ZoomStatus` with cached CanZoom:
  - common unscoped case reads only local player + `player+0x850`
  - weapon flags are read only while scoped and only when `player+0x858` changes
  - do not add `weapon+0x54` zoom FOV, HUD animation reads, or per-mouse-delta
    floating-point math back into this path
  - shared rationale lives in `.claude/features/zoom-status-performance.md`

## 11. Main Config Keys

- `Metroid.Sensitivity.Aim`
- `Metroid.Sensitivity.AimYAxisScale`
- `Metroid.Aim.Adjust`
- `Metroid.Aim.Disable.MphAimSmoothing`
- `Metroid.Aim.Enable.Accumulator`
- `Metroid.Aim.NativeHookMode` — §6.1 (developer-only; forced `0` in release)
- `Metroid.Aim.LowLatencyMode` — §6.2 (`Off` / `ImmediateSync` / `MoonLikeAim`; `InstantAimFollow` is developer-only and public builds migrate it to `ImmediateSync`; requires `DisableMphAimSmoothing`)
- `Metroid.Enable.stylusMode`
- `Metroid.Operation.SnapTap`
- `Metroid.Apply.joy2KeySupport`

(Defined in `src/frontend/qt_sdl/MelonPrimeDef.h`)
