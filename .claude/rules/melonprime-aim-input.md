# MelonPrime Aim/Input Notes (No CustomHud)

This document tracks the MelonPrime path from input capture to frame input state and final aim RAM writes.

## 1. End-to-End Pipeline

1. `RawInputWinFilter` / `InputState` capture RawInput (Windows).
2. `MelonPrimeCore::UpdateInputState[Reentrant]()` builds `m_input`.
3. `ProcessMoveAndButtonsFast()` maps movement/buttons to the DS input mask.
4. `ProcessAimInputMouse()` or `ProcessAimInputStylus()` runs.
5. In mouse mode, output is written directly to `*m_ptrs.aimX / *m_ptrs.aimY`.

Main implementation files:
- `src/frontend/qt_sdl/MelonPrimeGameInput.cpp`
- `src/frontend/qt_sdl/MelonPrimeRawInputWinFilter.cpp`
- `src/frontend/qt_sdl/MelonPrimeRawInputState.cpp`

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

- Q14 fixed-point residual accumulation:
  - `m_aimResidualX/Y += delta * m_aimFixedScaleX/Y`
  - Residuals are clamped to `AIM_MAX_RESIDUAL`
- Two output paths:
  - Direct path (`DisableMphAimSmoothing=true`)
    - Uses `>> 12` output (higher granularity)
    - No deadzone
  - Legacy path (`false`)
    - Uses `ApplyAim()` with deadzone/snap behavior
    - Preserves the `>> 14`-based legacy behavior
- On output:
  - `*m_ptrs.aimX = outX`
  - `*m_ptrs.aimY = outY`
- If `AimAccumulator` is disabled, residuals are cleared at frame end.

## 6. Stylus Mode

- `ProcessAimInputStylus()` is straightforward:
  - `TouchScreen(touchX, touchY)` while `emuInstance->isTouching`
  - otherwise `ReleaseScreen()`
- Some operations (for example morph/weapon actions) set `BIT_BLOCK_STYLUS` to avoid interference.

## 7. RawInput Layer Notes (Windows)

- `RawInputWinFilter`:
  - Uses Qt target when Joy2Key is ON, hidden window when OFF
  - Splits `PollAndSnapshot` and `DeferredDrain`
  - Handles `WM_INPUT` in `HiddenWndProc` to avoid loss
- `InputState`:
  - Uses `processRawInputBatched()` for batched reads
  - Prebuilds hotkey masks via `setHotkeyVks()`
  - `snapshotInputFrameNoEdges()` preserves outer `m_hkPrev` state

## 8. Sensitivity Cache and Recalculation

- `RecalcAimSensitivityCache()`:
  - Recomputes `m_aimSensiFactor` / `m_aimCombinedY` from `AimSens` and `AimYScale`
- `ApplyAimAdjustSetting()`:
  - Applies `AimAdjust`
- `RecalcAimFixedPoint()` then refreshes:
  - fixed-point scaling values
  - minimum delta values
  - snap/deadzone thresholds
  and clears stale residuals.

## 9. Main Config Keys

- `Metroid.Sensitivity.Aim`
- `Metroid.Sensitivity.AimYAxisScale`
- `Metroid.Aim.Adjust`
- `Metroid.Aim.Disable.MphAimSmoothing`
- `Metroid.Aim.Enable.Accumulator`
- `Metroid.Enable.stylusMode`
- `Metroid.Operation.SnapTap`
- `Metroid.Apply.joy2KeySupport`

(Defined in `src/frontend/qt_sdl/MelonPrimeDef.h`)
