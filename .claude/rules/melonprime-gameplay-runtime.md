# MelonPrime Gameplay Runtime (No CustomHud)

This document summarizes MelonPrime runtime behavior excluding `MELONPRIME_CUSTOM_HUD`.  
It focuses on in-game flow, weapon/morph handling, gameplay setting application, and ROM detection/address resolution.

## 1. Frame Entry Point

- The main entry is `MelonPrimeCore::RunFrameHook()` (`src/frontend/qt_sdl/MelonPrime.cpp`).
- High-level order:
  1. `UpdateInputState()` captures the frame input snapshot.
  2. `HandleGlobalHotkeys()` handles sensitivity hotkeys.
  3. If ROM is not detected, run `DetectRomAndSetAddresses()`.
  4. If ROM is detected, evaluate the `inGame` flag.
  5. On game join, run `HandleGameJoinInit()` to resolve player-relative pointers.
  6. If focused and in-game, run `HandleInGameLogic()`.
  7. If not in-game, run `ApplyGameSettingsOnce()`.

## 2. ROM Detection and Address Resolution

- `DetectRomAndSetAddresses()` (`src/frontend/qt_sdl/MelonPrimeGameRomDetect.cpp`) matches `globalChecksum` against the ROM table.
- On match, it copies `RomAddresses` into `m_currentRom`.
- It initializes hot addresses in `m_addrHot` and resolves `m_ptrs.inGame` early.
- It also recalculates aim sensitivity caches after detection.
- ROM address tables are centralized in `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`.

## 3. Game-Join Initialization

- `HandleGameJoinInit()` (`src/frontend/qt_sdl/MelonPrime.cpp`) runs once per in-game join.
- From `playerPos`, it computes:
  - Player-struct offset via `PLAYER_ADDR_INC (0xF30)`
  - Aim-region offset via `AIM_ADDR_INC (0x48)`
- It then resolves `m_ptrs` (`aimX/aimY/weapon/...`) with those offsets.
- It also applies hunter/adventure state, MPH sensitivity, aim-smoothing patch, and in-game aspect ratio patch.

## 4. In-Game Hot Path

- `HandleInGameLogic()` (`src/frontend/qt_sdl/MelonPrimeInGame.cpp`) runs every frame in-game.
- Main steps:
  1. Morph input (`HandleRareMorph`)
  2. Weapon input (`ProcessWeaponSwitch` + `HandleRareWeaponSwitch` when needed)
  3. Adventure UI handling (`HandleAdventureMode`)
  4. Weapon Check touch hold handling
  5. Standard input mapping (`ProcessMoveAndButtonsFast`)
  6. Morph ball boost handling (`HandleMorphBallBoost`)
  7. Aim handling (Stylus or Mouse)

## 5. Weapon Switching Logic

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

## 6. Gameplay Setting Application Outside Active Gameplay

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

## 7. In-Game Aspect Ratio Patch

- Implementation: `src/frontend/qt_sdl/MelonPrimePatch.cpp` (under `MELONPRIME_DS`).
- `InGameAspectRatio_ApplyOnce()` rewrites ARM instructions/values per selected mode.
- In `Auto` mode, it reads `ScreenAspectTop` and applies only when needed.
- On stop/reset lifecycle paths, `InGameAspectRatio_ResetPatchState()` clears patch state.

## 8. Reference Files

- `src/frontend/qt_sdl/MelonPrime.h`
- `src/frontend/qt_sdl/MelonPrime.cpp`
- `src/frontend/qt_sdl/MelonPrimeInGame.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameSettings.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameRomDetect.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`
- `src/frontend/qt_sdl/MelonPrimeInternal.h`
- `src/frontend/qt_sdl/MelonPrimePatch.cpp`
