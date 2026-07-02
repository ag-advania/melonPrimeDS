# MelonPrime Aim/Input Notes (No CustomHud)

This document tracks the MelonPrime path from input capture to frame input state and final aim RAM writes.

Platform scope: the sections below describe the Windows Raw Input path. On macOS (added
2026-07), `MelonPrimeRawInputMacFilter.{h,cpp}` provides the RawInput-equivalent aim deltas via
IOHIDManager (unaccelerated HID X/Y counts, fetch-and-clear at frame snapshot); mouse buttons and
keyboard hotkeys stay on the Qt event / SDL path (`EmuInstance::onMousePress`,
`emuInstance->hotkeyMask`), and the QCursor center-delta method remains the fallback when the
macOS Input Monitoring permission is not granted. See the macOS notes in [build.md](build.md).

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

## 9. Sensitivity Cache and Recalculation

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

## 10. Main Config Keys

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
