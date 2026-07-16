# MelonPrime Gameplay Runtime (No CustomHud)

This document summarizes MelonPrime runtime behavior excluding `MELONPRIME_CUSTOM_HUD`.  
It focuses on in-game flow, weapon/morph handling, gameplay setting application, and ROM detection/address resolution.

## 1. Frame Entry Point

- The main entry is `MelonPrimeCore::RunFrameHook()` (`src/frontend/qt_sdl/MelonPrime.cpp`).
- High-level order:
  1. `UpdateInputState()` captures the frame input snapshot.
  2. `HandleGlobalHotkeys()` handles sensitivity hotkeys.
  3. If ROM is not detected, run `DetectRomAndSetAddresses()`.
  4. If ROM is detected, evaluate `isInGame` (`inGame == 0x0001`) and `isEndOfGame` (mode/flow;
     see §2).
  5. **Join:** `isInGame && !BIT_IN_GAME_INIT` and (`inGame` rising edge **or** unpause with
     `!BIT_END_OF_GAME_PATCH_RESTORED`) → `HandleGameJoinInit()` (aspect-ratio patch, pointer
     resolve). Leaving `!isInGame` clears `RESTORED` even if `INIT` was already clear (e.g.
     unpause during scoreboard).
  6. **Battle runtime enter** while `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED`: first
     `mode==0x0E && flow==0` → `HandleBattleRuntimeEnter()` (battle patches, match hooks,
     weapon-switch trampoline). Then `flow!=0` → `Patches_RestoreOnLeave()` +
     `ARM9Hook_SetMatchHooksActive(false)` (`isInGame` may still be true).
  7. If `isInGame` and `BIT_BATTLE_RUNTIME_MODE`, re-apply OSD color; damage-notify runs whenever
     `isInGame`. (`MELONPRIME_CUSTOM_HUD` builds also call
     `CustomHud_ClampHelmetLayersPreFrame()` here — helmet spawn-flash layer clamp, gated to a
     single static read when the helmet hide patch is not applied; see
     [../../features/hud/custom-hud-helmet-spawn-flash.md](../../features/hud/custom-hud-helmet-spawn-flash.md).)
  8. If `!isInGame` and (`BIT_IN_GAME_INIT` or `BIT_END_OF_GAME_PATCH_RESTORED`), clear flags,
     `ARM9Hook_SetMatchHooksActive(false)`, transient reset — **without** `Patches_RestoreOnLeave`.
  9. If focused and `isInGame`, run `HandleInGameLogic()`.
  10. If focused and not `isInGame`, run the registry out-of-game patch site, then
     `ApplyGameSettingsOnce()`.

## 2. In-Game State Detection

- Detailed notes: [docs/architecture/gameplay/battle-flow-state.md](battle-flow-state.md).

| Flag | Condition | Stored / exposed |
|---|---|---|
| `isInGame` | `(*m_ptrs.inGame) == 0x0001` | `BIT_IN_GAME`, `IsInGame()` |
| `isEndOfGame` | `currentMode == 0x0E && flowState != 0` | local in `RunFrameHook`; `BattleFlow::IsEndOfGame` |

- `isEndOfGame` uses `currentMode` / `battleFlowState` from `MelonPrimeGameRomAddrTable.h`
  (`m_ptrs` resolved at ROM detect). mphCodex:
  `試合中かmenuかの判定/5_End-Match-Detection-Condition-Update-FlowState1Or2-AllVersions/`.
- `BIT_IN_GAME_INIT` is set in `HandleGameJoinInit()` and cleared when `!isInGame`, not when
  `isEndOfGame` becomes true. Hooks gate on `BIT_IN_GAME_INIT`.
- Performance: mode/flow poll only while `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED`
  (single `m_flags.packed` load for the gate). Pre-latch lobby: read `currentMode` only; read
  `battleFlowState` only when `mode==0x0E`. Post-latch live match: read `flowState` only. Menu =
  single `inGame` read. Patch restore is edge-triggered once. Battle patches/hooks and per-frame
  OsdColor skip lobby frames (`BIT_BATTLE_RUNTIME_MODE` gate).

## 3. ROM Detection and Address Resolution

- `DetectRomAndSetAddresses()` (`src/frontend/qt_sdl/MelonPrimeGameRomDetect.cpp`) selects the `RomGroup` using a **hybrid** scheme:
  1. **Primary — checksum (authoritative).** `globalChecksum` (header + ARM9 + ARM7 CRC32, computed once at ROM load in `EmuInstance.cpp`) is matched against `CHECKSUM_TABLE`. A hit picks the `RomGroup` directly, so every shipped revision/variant — including EU1.0 vs EU1.1, whose in-RAM layouts differ by ~0x80 — selects the correct address table regardless of the header revision byte.
  2. **Fallback — NDS header.** When the checksum is unrecognized (trimmed / modified / brand-new dump), it falls back to `MapHeaderToRomGroup(globalGameCode, globalRomVersion)`: gameCode `@0x0C` (`AMHE`=US, `AMHP`=EU, `AMHJ`=JP, `AMHK`=KR) + revision `@0x1E` (0 → x1.0, non-0 → x1.1). The OSD message tags these as `"<name> (variant, CRC 0x........)"`.
- `globalGameCode` / `globalRomVersion` are captured from `cart->GetHeader()` at ROM load alongside `globalChecksum`. The `MphGameCode::*` constants live in `MelonPrimeDef.h`. The load-time "Unknown ROM" warning (`EmuInstance.cpp`) is gameCode-based — a known MPH gameCode with an unknown checksum still detects via the fallback.
- gameCode→region mapping is confirmed against MphRead's version table; MphRead does **not** auto-derive the revision from `@0x1E` (it takes the version as a manual label), which is why the header revision is the fallback, not the primary, key.
- On match, it copies `RomAddresses` into `m_currentRom`.
- It initializes hot addresses in `m_addrHot` and resolves `m_ptrs.inGame` plus mode/flow pointers
  early (read every frame for `isInGame` / `isEndOfGame`).
- It also recalculates aim sensitivity caches after detection.
- ROM address tables are centralized in `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`.

## 4. Game-Join Initialization

- `HandleGameJoinInit()` (`src/frontend/qt_sdl/MelonPrime.cpp`) runs once per in-game join.
- From `playerPos`, it computes:
  - Player-struct offset via `PLAYER_ADDR_INC (0xF30)`
  - Aim-region offset via `AIM_ADDR_INC (0x48)`
- It then resolves `m_ptrs` (`aimX/aimY/weapon/...`) with those offsets.
- It also applies hunter/adventure state, MPH sensitivity, aim-smoothing patch, and in-game aspect ratio patch.
- `Patches_Apply(PatchSite_GameJoin)` — InGameAspectRatio only.
- Battle-runtime patches/hooks: `HandleBattleRuntimeEnter()` on first `mode==0x0E && flow==0`
  (`PatchSite_BattleRuntime`, match hooks, weapon-switch trampoline). See
  [battle-flow-state.md](battle-flow-state.md).

## 5. In-Game Hot Path

- `HandleInGameLogic()` (`src/frontend/qt_sdl/MelonPrimeInGame.cpp`) runs every frame in-game.
- Main steps:
  1. Morph input (`HandleRareMorph`)
  2. Weapon input (`ProcessWeaponSwitch` + `HandleRareWeaponSwitch` when needed)
  3. Adventure UI handling (`HandleAdventureMode`)
  4. Weapon Check touch hold handling
  5. Standard input mapping (`ProcessMoveAndButtonsFast`)
  6. Morph ball boost handling (`HandleMorphBallBoost`)
  7. Aim handling (Stylus or Mouse)

## 6. Weapon Switching Logic

- Implementation: `src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp`
- Input cases:
  - Mouse wheel / Next / Prev
  - Direct hotkeys (Beam, Missile, Weapon1-6, Special)
- Availability checks include:
  - Ownership flag + ammo thresholds + Weavel exception (Battlehammer requirement)
  - Omega Cannon restriction when Omega is active
- After target selection, `SwitchWeapon()` writes RAM state:
  - Updates `weaponChange` and `selectedWeapon`
  - Temporarily adjusts `jumpFlag` in specific states
  - Uses `FrameAdvanceTwice()` to complete the transition

## 7. Gameplay Setting Application Outside Active Gameplay

- Implemented in `ApplyGameSettingsOnce()` (`src/frontend/qt_sdl/MelonPrimeInGame.cpp`).
- Covers:
  - Headphone flag
  - MPH sensitivity
  - Hunter/map unlocks
  - DS firmware name usage
  - Hunter selection / license color
  - SFX/BGM volume
- The concrete operations are in `src/frontend/qt_sdl/MelonPrimeGameSettings.cpp`.
- One-time applications are guarded by `m_appliedFlags`.
- Before this runs, `RunFrameHook()` calls `Patches_Apply(PatchSite_OutOfGameFrame, ctx)` while
  focused and out of game (`!isInGame`). That registry site owns menu/out-of-game static patches such as the
  Wi-Fi bitset fix, firmware language application, and stage matrix expansion. Each entry is
  self-guarded, so the per-frame menu call is a cheap cold-path check.

## 8. In-Game Aspect Ratio Patch

- Implementation: `src/frontend/qt_sdl/MelonPrimePatchAspectRatio.cpp` (under `MELONPRIME_DS`).
- `InGameAspectRatio_ApplyOnce()` rewrites ARM instructions/values per selected mode.
- In `Auto` mode, it reads `ScreenAspectTop` and applies only when needed.
- The patch is registry-managed at `PatchSite_GameJoin`. On stop/reset lifecycle paths,
  `Patches_ResetAll()` calls `InGameAspectRatio_ResetPatchState()`.

## 9. Frame-hook performance habits

When touching `RunFrameHook` or match lifecycle code:

- Batch `StateFlags` checks via one `m_flags.packed` load when multiple bits gate the same cold block.
- Poll game RAM with the minimum reads: `inGame` every frame; `currentMode` before `battleFlowState`;
  after `BIT_BATTLE_RUNTIME_MODE` latch, only `battleFlowState` until match end.
- Defer battle patches, ARM9 hooks, and per-frame OsdColor until `BIT_BATTLE_RUNTIME_MODE` (not join).
- Keep join / battle-enter / restore paths in `COLD_FUNCTION` outlined helpers so the hot path stays lean.
- Per-frame helpers added to the in-game block must gate on cheap state first (static tracker /
  flag bit), never a per-frame `Config::Table` lookup. Example: the Custom HUD helmet layer clamp
  checks `NoHudPatch_GetAppliedMask()` before any RAM read, costs ~5 RAM reads + 2 `ARM9Read32`
  when active, and writes only during the few spawn frames.

## 10. Reference Files

- `src/frontend/qt_sdl/MelonPrime.h`
- `src/frontend/qt_sdl/MelonPrime.cpp`
- `src/frontend/qt_sdl/MelonPrimeBattleFlowState.h`
- `src/frontend/qt_sdl/MelonPrimeInGame.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameSettings.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameRomDetect.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`
- `src/frontend/qt_sdl/MelonPrimeInternal.h`
- `src/frontend/qt_sdl/MelonPrimePatchRegistry.h`
- `src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp`
- `src/frontend/qt_sdl/MelonPrimePatchAspectRatio.cpp`
