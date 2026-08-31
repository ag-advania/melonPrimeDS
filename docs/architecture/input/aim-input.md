# MelonPrime Aim/Input Notes (No CustomHud)

This document tracks the MelonPrime path from input capture to frame input state and final aim RAM writes.
The field-level owner, writer, reset and hot-layout map is maintained in
[input-srp-ownership.md](input-srp-ownership.md).

Platform scope: the sections below describe the Windows Raw Input path first. On macOS (added
2026-07), `MelonPrimeRawInputMacFilter.{h,mm}` provides RawInput-equivalent aim deltas via
GCMouse first, then IOHIDManager as a fallback (unaccelerated HID X/Y counts in a process-wide
monotonic collector, read through a per-instance subscription cursor). GCMouse is primary because
it does not need the Input Monitoring TCC permission;
IOHID is only opened after a short grace period when no GCMouse backend appears. On Linux/X11,
`MelonPrimeRawInputLinuxFilter.{h,cpp}` provides the equivalent through XInput2 `XI_RawMotion`.
The macOS/Linux call sites go through `MelonPrimePlatformInput.h`. All platforms share
`PlatformInputOwnerService` owner semantics and an instance-owned
`MelonPrimeInputSubscription`; Windows keeps its native collector in `RawInputWinFilter` and
allocates one `InputState` per subscription.

Mouse buttons and keyboard hotkeys stay on the Qt event / SDL path (`EmuInstance::onMousePress`,
`emuInstance->hotkeyMask`) on non-Windows platforms. They are intentionally not captured by the
raw-delta filters so a physical click cannot create duplicate press edges.

The QCursor center-delta method remains the fallback when the macOS raw backends are unavailable.
On Linux the source of truth is the XInput2 raw filter when available (2026-07-03): relative
axes are used as-is, absolute pointing devices (VirtualBox's integrated tablet) are converted to
deltas per-device inside the filter, so cursor warps and VBox host re-syncs cannot corrupt aim.
The Qt mouse-move accumulator in `ScreenPanel` is the non-XCB (Wayland) fallback. See §9 and the
macOS/Linux notes in [../../development/build/overview.md](../../development/build/overview.md).

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
- `src/frontend/qt_sdl/MelonPrimePlatformInput.h` (macOS/Linux raw delta and warp facade)
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
  - macOS: GCMouse deltas are warp-immune; IOHID (trackpad) and QCursor fallback use panel
    containment warps. Cursor disassociation/hide (`MacSetAimCursorCaptured`) is **GCMouse
    only** — see §10. Acquisition caches the source-resolved warp decision once
    in `m_warpCursorAfterAimThisFrame`; `ProcessAimInputMouse` reads that scalar,
    and capture-wanted alone never emits a recenter.
  - `unfocus()` must call `unclip()` on Linux and macOS; otherwise Escape leaves the cursor
    hidden/locked.

## 6. Native / Low-Latency Aim Injection Mechanisms (newer)

Beyond the classic `aimX/aimY` write, three ARM9-hook-based mechanisms can take over or augment
aim/fire. `ReloadConfigFlags()` is the lifecycle wrapper: it loads and clamps a
`RuntimeConfigSnapshot` via `LoadRuntimeConfigSnapshot()` and applies it via
`ApplyRuntimeConfigSnapshot()`. The resulting per-instance `Arm9HookActivationPlan` is then
consumed by the shared ARM9 hook (`MelonPrimeArm9Hook.cpp` → `DispatcherCallback`), which does
not reread `Config::Table` keys on its install edge. "Developer-only" means compiled/forced off
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
- `ImmediateSync` and `MoonLikeAim` are forced `Off` unless
  `DisableMphAimSmoothing=true`; both are also inert in stylus mode. The legacy
  `InstantAimFollow` value is separate from that gate.
- A **separate** aim mechanism: at the aim-function exit PCs it rewrites the player's orientation
  basis (forward/side/up vectors at `CPlayer +0x4C / +0x58 / +0x64`) directly.
- Release-available modes: `ImmediateSync` (snap orientation straight to the target) and
  `MoonLikeAim` (chase the target with tunable Q12 step sizes:
  `MoonLikeAimNormalStepQ12` / `FastStepQ12` / `FastThresholdQ12`).
- `InstantAimFollow` is the legacy value (`LowLatencyMode = 3`) for the separate
  `FpsCameraLock` camera-behavior patch, not the runtime exit-PC hook. It is retained as a distinct
  value and is never reinterpreted as `ImmediateSync`.
- `FpsCameraLock` is a public independent camera-behavior switch. It is separate from both the
  aim-follow timing modes and `DisableMphAimSmoothing`.
- Hook implemented in `MelonPrimePatchLowLatencyAimHook.inc`; registered/dispatched via
  `MelonPrimeArm9Hook.cpp`.

### 6.2a Touch versus Dual aim delivery

- The ROM has **two** aim producers and MelonPrime has to feed the right one. Both the biped
  dispatcher and the alt-form dispatcher branch on `record[0x00] & 0x2` (`player+0x364`), the same
  word the preset snapshot already reads:
  - **set** (Touch R `0x0076`, Touch L `0x0276`): the touch producer sums the four history samples
    at `InputSlot+0x38..0x46` into `+0x2A`/`+0x2C`, then multiplies by `player+0x3F8`/`+0x3FC` and
    calls the yaw/pitch updaters.
  - **clear** (Dual R `0x007C`, Dual L `0x027C`): that branch is jumped over entirely. The aim path
    loads `player+0xE4` (yaw) and `player+0xE8` (pitch) and passes them to the same updaters
    directly, so those fields already carry the sensitivity product.
- MelonPrime wrote only the touch chain (`m_ptrs.aimX/aimY`, the newest history slot at
  `InputSlot+0x3E`/`+0x46`), which is why the Dual presets had no aim at all. The native aim hooks
  did not help either: every one of their PCs — including `kAltStoredHooks`, whose addresses are in
  fact the alt-form Dual yaw/pitch call sites rather than a transform-transition fallback — sits
  inside a branch a Dual preset never enters, except that alt-form pair.
- `WriteAimDelta()` now picks the target from `m_ptrs.dualAim`, resolved once at game join and left
  null on a Touch preset. The Touch write is unchanged; the Dual write applies
  `player+0x3F8`/`+0x3FC` itself so both paths deliver the same magnitude per unit of mouse
  movement. Verified against the ROM: the history fold at `0202A008`–`0202A02C` is a plain sum of
  the four samples, so a single injected sample arrives at `+0x2A`/`+0x2C` one-to-one.
- On a Dual preset the native-hook modes are bypassed and the deltas are left at zero, which also
  stops the alt-form hook from fighting the direct write — it early-outs on a zero delta.
- The ROM rewrites `player+0xE4`/`+0xE8` later in the same frame from its own digital accumulator
  (acceleration, clamp, and a ~0.4x decay when nothing is held), and MelonPrime only writes on
  frames that carry a delta, so the preset's own D-pad/face-button aim keeps working.
- Cost: the branch is a single null test on a Tier 1 pointer. GCC places the Dual write out of line,
  so the Touch path keeps its two 16-bit stores and pays one not-taken branch.

#### ROM facts behind the two aim paths

Cross-checked against the mphCodex `Control/DualAim` package. These constrain any future change here.

- The control record's flags carry two selectors: `0x2` picks Touch versus Dual, `0x20` picks the
  Exact Aim branch. All four standard presets set `0x20`.
- `player+0x3F8`/`+0x3FC` are the **Touch** scales and are **negative**: `0xFFFFF75D` = -0.5398 and
  `0xFFFFFB8E` = -0.2778. Both paths hand the yaw/pitch updaters the same unit, degrees in
  fixed point with `0x1000` = 1°.
- `InputSlot+0x2A`/`+0x2C` is the **sum** of the four history samples, not their average — the
  `lsl #2; asr #2` pair is width adjustment, not a divide. MelonPrime injects one sample and the
  other three are zero, so it arrives one-to-one. This is why the Dual write reuses the Touch scale:
  it makes a Dual preset feel exactly like a Touch one rather than like the native Dual accelerator.
- The native Dual accelerator MelonPrime overrides runs on a max of 8.0° (`0x8000`), a 40% initial
  step, `max/100` = `0x147` = 0.0798° per update, and a 0.399902 release decay. It also has a
  one-update producer/consumer gap: the branch consumes the previous value before producing the
  next.
- Call order differs: Touch runs Pitch then Yaw, Dual runs Yaw then Pitch. MelonPrime writes both
  fields before the frame, so the ROM's own order is preserved either way.
- The Dual branch does not pass the `NoAimInput` (`PlayerFlags1` bit 24) or `AimMinTouchTime` gates
  that the Touch branch applies, so aim is not suppressed there while a HUD rectangle is touched.
- Not covered: Free Camera (`player+0x4D6` ViewType 3, JP1_0 `0201AB8C`) is a third aim path with
  its own Touch and Dual consumers. Its Dual side uses the same `player+0xE4`/`+0xE8` fields but
  produces and consumes them in the same update, unlike the biped branch.

#### Which aim settings still apply on a Dual preset

Checked against the ROM's function boundaries: Pitch is `02027798`..`02027E18` and Yaw is
`02027E1C`..`020285B4`, and **both are called from the Touch branch and the Dual branch alike**.

| Setting | Dual | Why |
|---|---|---|
| `LowLatencyMode` ImmediateSync / MoonLikeAim | works | its hook PCs (`020282C8` / `02028544`) are inside the shared Yaw function |
| `InstantAimFollow` (legacy camera-lock alias) | works | enables the independent `FpsCameraLock` patch at `02028070`..`02028080`, inside shared Yaw |
| Zoom aim scale, aim accumulator, sensitivity | works | host-side, applied before the write |
| `DisableMphAimSmoothing` | no effect, and none needed | the patch rewrites the touch producer's history fold at `02029FE0`/`0202A008`; a Dual preset never reads its output |
| `FpsCameraLock` | works | it patches `02028070`, the Zoom-only gun-vector-to-facing-vector copy inside Yaw, making it unconditional. That removes the ~15.02° free-aim envelope in which the gun leads the body, and the ~9.985%-per-update follow, so it is an FPS camera lock rather than a latency tweak. Note this is a **different** patch from `DisableMphAimSmoothing`, which only disables the touch four-sample filter; mphCodex describes the two as one, which does not match this tree. |

`FpsCameraLock` (`Metroid.Aim.Enable.FpsCameraLock`) used to be reachable only as
`LowLatencyAimMode::InstantAimFollow`, and only while `DisableMphAimSmoothing` was also on. That
buried a camera-behavior change behind two settings about input timing and the touch filter, which
is what mphCodex's design notes call out. It is now its own independent public checkbox, kept
separate from the aim-follow mode and the smoothing setting. The public-facing wording and
translation entries remain attached to the setting. The legacy mode value is not reinterpreted as
`ImmediateSync`; an old config holding it still turns the independent lock on in any build.

| `NativeHookMode` (register injection / PostFold) | not used | every hook PC is inside the touch branch, so a Dual preset never reaches them |

The smoothing row has a consequence worth stating: because a Dual preset is *inherently* unsmoothed,
the host must emit direct-path values there whatever the setting says. Selecting the legacy path
would apply a deadzone and a coarser scale chosen to compensate for DS-side smoothing that is not in
the Dual chain. `AimBypassesDsSmoothing()` is the single predicate for that, true when either the
patch is applied or the preset is Dual.

### 6.2b Control-preset button synthesis

- Everything MelonPrime synthesizes into DS `KEYINPUT`, and every mask the post-poll overlay
  injects, comes from `MelonPrimeCore::m_presetBindings`, resolved once in `HandleGameJoinInit()`.
  Only the low half of each `{uint16 Button; uint16 PressFlags}` entry is a button mask.
- It is resolved from the ROM's **upstream** source, not from the player struct:

  ```text
  LIST_ControlTypeArray[slot]        u8 preset id, 0..3 human, 4 BOT
    -> LIST_ControlPresetTable[id]   static 0x9C record
       -> record +0x04/+0x08/+0x0C/+0x10 move, +0x34 fire, +0x38 jump,
          +0x50 morph boost, +0x7C zoom
  ```

  `player+0x364` holds the same record, but only because player init calls `0200CC7C(player, id)`
  to expand it there — it is downstream state. Reading it at game join races that init, and a
  pre-init read returns zeros, which `PickButton()` would then silently resolve back to the Touch R
  defaults. The id array is what the ROM's own runtime reader consults (JP1_0 `020310A8`) and what
  the WiFi slot-state packet decoder writes, so it is correct for a client and not only for the
  match host. `player+0x364` remains the fallback if the id is unusable.
- Cross-check: the `local_slot_byte` column of the mphCodex control-type map matches
  `LIST_PlayerPos` exactly on all seven ROM versions, so the slot index MelonPrime already uses is
  the same one the ROM's runtime reader indexes this array with.
- This is what makes the non-default presets work. The four presets bind the same action to
  different buttons, so the previous fixed choices only ever matched Touch R:

  | | move | jump | fire | zoom | boost |
  |---|---|---|---|---|---|
  | Touch R | D-pad | A\|B\|X\|Y | L | R | R |
  | Touch L | Y/A/X/B | D-pad | R | L | L |
  | Dual R | D-pad | R | L | Select | R |
  | Dual L | Y/A/X/B | L | R | Select | L |

- `PresetButtonBindings::PickButton()` reduces a binding to **one** button. The ROM only tests
  `binding & field`, so one bit is sufficient, and pressing the whole mask would press buttons the
  preset also binds to other actions (Touch R jump is `A|B|X|Y`). It keeps the historical button
  when the binding contains it, which leaves Touch R bit-for-bit unchanged.
- The per-direction move table is built at game join into `MoveMask[16]` (index bits F/B/L/R,
  opposite pairs cancel), replacing the fixed D-pad `MoveLUT`. The hot path is still a single
  indexed read plus branchless select-masks; measured at 1.379 ns/call versus 1.384 ns/call for the
  old fixed-D-pad version, with bit-for-bit parity on the Touch R defaults across all 16x4x2 input
  combinations.
- **In-game only.** `ProcessMovementOnlyFromReset()`, the out-of-game path that keeps WASD working
  on the Adventure planet/region map and the Hunter License pages, deliberately keeps the fixed
  `InputProjection::MenuMoveMask`: menus navigate on the D-pad whatever the in-game preset is, so
  carrying a left-handed preset's Y/A/X/B mapping into them would break them.
- None of this is gated on `ImmediateInputEdgeOverlay`. The snapshot is taken in
  `HandleGameJoinInit()`, which runs on the in-game rising edge with no feature gate, and the
  always-on legacy `KEYINPUT` synthesis (`ProcessMoveAndButtonsFastImpl`, `ApplyZoomBindingInput`,
  `HandleMorphBallBoost`) reads it directly. The overlay is just one more consumer. The only
  buttons still hardcoded are `INPUT_START` and the UI Left/Right pair, which are menu controls and
  not preset-bound.
- The snapshot also carries `MirrorTouchX`, the left-handed touch layout flag, taken from
  `record[0x00] & 0x200` — the same test the ROM's own runtime layout check makes (Touch R `0x0076`
  and Dual R `0x007C` are normal; Touch L `0x0276` and Dual L `0x027C` are mirrored). The in-match
  HUD rectangles are one shared table for every preset; the ROM mirrors them by
  `centerX = 256 - centerX` in `GetTouchRegionCenter` / `TouchRegionHit`, so any touch point
  MelonPrime synthesizes has to go through `PresetTouchX()` or it only lands on the right-handed
  layouts. This affects the two in-match taps: the legacy transform (region ID4, Morph/Unmorph,
  centre `(232,168)` 48x48, normal X 208..255 versus mirrored X 0..47) and weapon check (region ID3,
  the weapon radial menu). `CENTER_RESET` and `SCAN_VISOR_BUTTON` sit on X=128, where the mirror is
  a no-op. The Adventure dialog points (OK/LEFT/RIGHT/YES/NO) are menu-consumer regions rather than
  in-match HUD rectangles and are left alone.
- Developer builds print the resolved snapshot once per match join
  (`preset move .../... jump ... fire ... zoom ... boost ...`), so a preset that reads back as the
  Touch R defaults when it should not is visible immediately.
- Zoom no longer has a fixed-`INPUT_R` fallback: it always uses the preset button, which is what
  `ZoomInputMethod`'s "new method" used to opt into. That left the option selecting between two
  identical behaviors, so the runtime flag, the "Use New Method for Zoom" checkbox and its
  description were all removed. `ZoomInputMethod::NewPresetBinding` is retained as a retired value
  so it is not reused: an old config holding it behaves as `LegacyFixedR` and is normalized on the
  next settings save. `NewNativeToggle` ("New Method 2") is unaffected.

### 6.3 Native Biped Fire (`BipedFireMethod`, developer-only)
- When enabled (`m_enableNativeBipedFire`, forced off in release), `ProcessMoveAndButtonsFast`
  holds `INPUT_L` released — it does **not** synthesize the legacy fire input. The `kModBits` /
  `fireBit` logic in `ProcessMoveAndButtonsFastImpl` implements this split.
- Shoot instead enters through the post-poll input overlay: the hook registers the same
  `LIST_HookActionConsumerPc` PC as `ImmediateInputEdgeOverlay` and contributes a consistent
  Fire `current` / `pressed` / `released` triple to the single `player+0x464` read/modify/write in
  `ImmediateInputEdgeOverlay_DispatchCheck`. `MelonPrimePatchNativeBipedFireHook.inc` owns only the
  host-side edge latch; it does not call `PlayerFireUpdate`, redirect execution, or touch registers,
  so cooldown, repeat fire, charge, ammo, projectile, HUD, SFX and animation all stay on the ROM's
  own biped action state machine.
- The Fire edge is resolved **once per frame** by `UpdateNativeBipedFireInput()` (called from
  `ApplyPostPollOverlayInput()` on the frame path, before `NDS::RunFrame()`); the hook only projects that
  result onto the binding mask. This is mandatory, not a style choice: the ROM action consumer is
  the entry of the per-player input consumer (`PlayerEntity.Process()` → `ProcessInput()` runs for
  every player entity), so the hook is entered up to four times per frame and only one of those
  entries belongs to the local player. Resolving the edge inside the hook makes an earlier player's
  entry recompute held-vs-held and write `pressed = 0` back over the bit before the local player's
  own entry reads it — `current` survives, so the symptom is "`IsDown` works, nothing ever fires".
- Both latches are invalidated (re-baselined, not zeroed) on game join/leave, focus loss/regain,
  feature toggle-off, savestate load, boot/stop/ROM change, `AIMBLK_NOT_IN_GAME` transitions, a
  change of the ROM's local `Player*` (read once per frame by `ApplyPostPollOverlayInput()` and
  shared by both, so they cannot disagree about the frame), and biped↔alt-form transitions. Resuming
  with the button already held therefore restores `current` without manufacturing a stale `pressed`
  edge; the next real release→press produces the edge.
- The generic `ImmediateInputEdgeOverlay` resolves its edges the same way, in
  `UpdateImmediateInputEdgeOverlayInput()`, but tracks them **per action** (`OVA_*`) rather than per
  binding bit: the binding masks and `m_immediateOverlayPreserveMask` are only final later in the
  frame (`HandleMorphBallBoost()` adds `INPUT_R`), so the hook expands the resolved actions onto
  whatever masks it sees. This is what made the overlay's `pressed` edges host-only before — the
  MPH host is player slot 0, so the first action-consumer entry of the frame was the local player's
  and the edge survived; as a client the following entries erased it first.
- The overlay write is **additive** (`field | injected`), never a replace. It is not the only thing
  driving these bits: for Jump, Zoom and Movement the legacy DS `KEYINPUT` path stays active, so the
  game's own poll already produced correct `current`/`pressed`/`released`, and the ROM binding is not
  even the same bit MelonPrime presses — Touch R binds Jump to `0x0C03` (A/B/X/Y) while the legacy
  path presses B alone. A replacing write cleared the game's own correct edge on every frame the
  overlay latch reported no edge (right after a re-baseline, or when a re-entrant `FrameAdvanceOnce`
  from weapon switch / morph had already consumed it), which is why Jump and Zoom "often did not
  respond". OR-ing can only make an action land earlier, never swallow one. Native Biped Fire is
  unaffected: it suppresses `INPUT_L`, so the polled bits it ORs into are already zero.
- Verified against the ROM disassembly (`mphCodex mnt/data/dumps/mphDump/JP1_0.txt`), not inference:
  `02024174` is `UpdatePlayerActionInput(Player* r0, MphInput* r1)` and `0201042C add r1,r4,#0x464`
  in its caller chain confirms the struct the jump gate reads is `player+0x464`. The input helper
  `02028EE8` maps `0x40000`→`+0x04` pressed, `0x100000`→`+0x08` released, `0x80000`→`+0x0A` repeat,
  no selector→`+0x00` down, with `0x10000` selecting per-case touch bits in `+0x34`. Unlike the fire
  gate, the jump gate does **not** OR in a selector — it uses the binding's own `PressFlags` half.

## 7. Stylus Mode

- `ProcessAimInputStylus()` is straightforward:
  - `TouchScreen(touchX, touchY)` while `emuInstance->isTouching`
  - otherwise `ReleaseScreen()`
- Some operations (for example morph/weapon actions) set `BIT_BLOCK_STYLUS` to avoid interference.

## 8. RawInput Layer Notes (Windows)

- `RawInputWinFilter`:
  - Collects OS events once and registers one per-instance subscription
  - Changes the raw-input target and Qt native filter only when active ownership changes
  - Uses Qt target when Joy2Key is ON, hidden window when OFF
  - Splits `PollAndSnapshot` and `DeferredDrain`
  - Handles `WM_INPUT` in `HiddenWndProc` to avoid loss
- `InputState`:
  - Is owned per subscription, so bindings, edges, and delta cursors cannot cross instances
  - Uses `processRawInputBatched()` for batched reads
  - Prebuilds hotkey masks via `setHotkeyVks()`
  - `snapshotInputFrameNoEdges()` preserves outer `m_hkPrev` state

### 8.1 Windows registration generations

Raw mouse aim deltas, XButton edge state, and wheel impulses share the same
`MelonPrimeInputSubscription::generation`. Every active-registration change is
an input-timeline boundary, including a target-window, Joy2Key, or Qt-filter
change where the capture owner itself does not change. The transaction is:

1. Drain pending `WM_INPUT` using the existing `GetRawInputBuffer` then
   `PeekMessage` order.
2. Mark the snapshot not ready and advance the subscription generation.
3. Unregister and register the new target/filter configuration.
4. Discard old deltas, edge/deferred presses, and wheel impulses.
5. Synchronize the physical button baseline, then publish `baselineReady`.

A frame may use Raw Input buttons, wheel, and aim only when the subscription is
the active owner, its snapshot is baseline-ready, and the snapshot generation
equals the current generation. A held XButton after re-registration is
therefore `down` without `pressed`; a release followed by a new press creates
the next edge. Raw wheel steps are consumed from the same ready snapshot. The
Qt wheel mailbox is consumed only when Raw Input is not the owner, so a single
physical wheel tick cannot be counted twice. The source-level contract check is
`tools/testing/test_mouse_input_savestate_contract.py`.

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
  - keeps a filter-thread-only pointer to the most recently used source entry, so the common
    single-mouse stream avoids repeating the `unordered_map` lookup;
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
- The Qt accumulator is a packed cumulative total with one GUI-thread writer and an
  emulation-thread cursor. Reset stores a separate cumulative boundary instead of clearing the
  producer total; motion arriving after the request and before the next frame is therefore kept.
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
- Only mask bits 0 and 1 are decoded for the normal X/Y path. The packed `raw_values` cursor still
  advances according to set bits, so a missing X value cannot shift Y into the wrong axis.
- If axis 0/1 are absent, the code keeps a conservative first-two-relative-values fallback for
  unusual devices, but normal mouse devices should hit the explicit axis 0/1 path.
- The filter captures only relative motion. Buttons and keyboard state remain owned by Qt/SDL
  hotkey handling to avoid double press edges.
- The XInput filter thread is the only writer of `accX/accY`; emulation threads only acquire-load
  them and maintain separate subscription cursors. The writer therefore uses relaxed load plus
  release store rather than an event-level locked `fetch_add`.
- `absBaseInvalid` is a rare producer flag. RawMotion first performs a relaxed load and only a
  true observation reaches the acquire/release exchange claim. `receivedMotion` likewise
  publishes only its first false-to-true session edge.

Troubleshooting signals:

- Launch from a terminal and look for `[MelonPrime] linux input: XInput2 RawMotion active`.
- That log only proves XInput2 selection succeeded. Aim should still work without using raw deltas,
  because Linux input is mouseMoveEvent accumulation + recenter.
- In a VM, disable host mouse integration or switch the Ubuntu login session to **Ubuntu on Xorg**
  before testing. Wayland sessions are not the primary supported path for FPS aim.
- If the view spins, inspect recenter paths first: the Linux mouseMoveEvent path must ignore
  zero-delta warp events and recenter through `LinuxWarpCursorGlobal`. If the view does not move at
  all, inspect whether `ScreenPanel::mouseMoveEvent` is firing while the core is focused/in-game.

## 10. macOS Raw / Cursor Notes

Since 2026-07-04, macOS in-game aim uses backend-specific cursor handling. macOS has no
`ClipCursor`; containment and hiding are implemented in `ScreenPanel` via
`aimContainmentLocalRect()`, `containAimCursorIfNeeded()`, and `MacWarpCursorGlobal`
(`CGWarpMouseCursorPosition`, no TCC permission — do not use `QCursor::setPos`).

Backend split (see `MelonPrimeRawInputMacFilter.mm`, `IsGcMouseAimActive()`):

- **GCMouse (external USB/BT mouse, macOS 11+)**: Raw deltas are warp-immune (GameController).
  Every mouse is assigned the filter's single serial `handlerQueue` before its value-change
  callback is installed. Fractional residuals and the packed GCMouse cumulative total therefore
  have exactly one serialized writer without an event-level atomic RMW.
  While aim is clipped, `MacSetAimCursorCaptured(true)` disassociates hardware motion from the
  OS cursor (`CGAssociateMouseAndMouseCursorPosition(false)`) and hides it
  (`CGDisplayHideCursor`). Containment warps are skipped — the parked cursor must not be warped
  every frame or it flashes on the DS screens. The Aim frame also suppresses
  `GuiRequestRecenter`; steady raw Aim reaches neither the GUI warp request nor
  `CGWarpMouseCursorPosition`.
- **IOHID (built-in trackpad, trackballs, macOS < 11 fallback)**: Raw deltas via HID, but
  **must not** call `MacSetAimCursorCaptured(true)`. Disassociating the cursor on the trackpad
  path drops Qt `mouseRelease` events and leaves `keyHotkeyMask` stuck (shoot/zoom held). IOHID
  uses containment warps to `aimContainmentLocalRect().center()` instead.
  IOHID publishes to its own packed cumulative total on the worker thread. The worker runloop
  pointer is an acquire/release atomic so stop/wakeup never races a plain pointer publication.
- **QCursor fallback**: When `isAvailable()` is false (no permission, no device). Per-frame
  recenter in `ProcessAimInputMouse` when raw is inactive; panel containment warps otherwise.

The frame-side rule is intentionally source-resolved rather than Apple-wide:
the cached warp decision is false for active GCMouse/IOHID raw input and true
for QCursor fallback. Focus, layout, capture and cursor-mode transitions may still issue
one-shot recenter requests; the zero-warp contract applies to steady raw Aim frames.

Mouse buttons and keyboard hotkeys stay on the Qt path (`EmuInstance::onMousePress` /
`onMouseRelease` → `keyHotkeyMask`). They are intentionally not read from GCMouse/IOHID.

The frame reader acquire-loads the independent GCMouse and IOHID totals, sums each axis modulo
32 bits, and advances only its subscription cursor. This preserves deltas across a backend handoff
without making the two event sources writers of one shared accumulator. Backend
availability is one atomic bitset (`BackendGc`, `BackendHid`): rare lifecycle
transitions use `fetch_or`/`fetch_and`, preventing a concurrent GC connect and
HID close from overwriting each other's state.

Stuck-click recovery (2026-07-04, trackpad report): `EmuInstance::syncMouseHotkeysFromQtButtons()`
clears mouse-mapped hotkey bits when `QGuiApplication::mouseButtons()` shows the button physically
up but a release event was lost. Called from `ScreenPanel` on macOS during press, move, and
`unfocus()` (which also releases a stuck DS touch via `releaseScreen()`). The
five supported mouse-button masks are precomputed on config load, so this
mouse-move recovery path performs fixed mask operations without mapping scans. See
[../../archive/investigations/input/click-handling.md](../../archive/investigations/input/click-handling.md) § "macOS trackpad stuck-click fix".

Troubleshooting:

- Launch from a terminal and look for `[MelonPrime] mac input: GCMouse backend` vs
  `IOHID backend`. Built-in MacBook trackpads use IOHID, not GCMouse.
- Cursor flash on mouse move with an external mouse → verify GCMouse is active and
  `MacSetAimCursorCaptured` is gated on `IsGcMouseAimActive()` only.
- Click stuck down on trackpad → verify cursor disassociation is **not** applied for IOHID;
  confirm `syncMouseHotkeysFromQtButtons` runs on the GUI thread.

## 11. Sensitivity Cache and Recalculation

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
  - shared rationale lives in `docs/features/zoom-status-performance.md`

## 12. Main Config Keys

- `Metroid.Sensitivity.Aim`
- `Metroid.Sensitivity.AimYAxisScale`
- `Metroid.Aim.Adjust`
- `Metroid.Aim.Disable.MphAimSmoothing`
- `Metroid.Aim.Enable.Accumulator`
- `Metroid.Aim.NativeHookMode` — §6.1 (developer-only; forced `0` in release)
- `Metroid.Aim.LowLatencyMode` — §6.2 (`Off` / `ImmediateSync` / `MoonLikeAim`; `InstantAimFollow` is a retained legacy alias and is independent of `DisableMphAimSmoothing`)
- `Metroid.Aim.Enable.FpsCameraLock` — §6.2 (public independent camera-behavior switch)
- `Metroid.Enable.stylusMode`
- `Metroid.Operation.SnapTap`
- `Metroid.Apply.joy2KeySupport`

(Defined in `src/frontend/qt_sdl/MelonPrimeDef.h`)
<!-- MELONPRIME_MORPH_BOOST_MODE_CONTROLS_AIM_V14 -->
### Morph Ball Boost input modes

Internal mode does not read raw movement for swipe acceptance and does not synthesize `altSteerDelta`; the game's internal vector remains authoritative. Custom mode reads the current frame's `m_input.mouseX/Y` for both threshold and direction before `ProcessAimInputMouse()` applies the same sample to aim. The values are read, not consumed or cleared, so no additional aim frame or queue is introduced.
