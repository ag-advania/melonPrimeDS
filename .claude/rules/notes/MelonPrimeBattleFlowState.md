# MelonPrime Battle Flow State (`isInGame` / `isEndOfGame`)

## `isInGame` (legacy)

- `(*m_ptrs.inGame) == 0x0001` at ROM row `inGame` (`0x020F0BB0` family).
- Drives `BIT_IN_GAME`, `IsInGame()`, `HandleGameJoinInit`, in-game hot path, menu path.

## `isEndOfGame` (battle flow)

Helpers: `src/frontend/qt_sdl/MelonPrimeBattleFlowState.h` (`BattleFlow::IsEndOfGame`).

mphCodex handoff:
`mphCodex/mnt/data/mphAnalysis/_Commons/試合中かmenuかの判定/5_End-Match-Detection-Condition-Update-FlowState1Or2-AllVersions/`

| Concept | Condition |
|---|---|
| `isEndOfGame` | `currentMode == 0x0E && flowState != 0` |

Addresses: `currentMode`, `battleFlowState` in `MelonPrimeGameRomAddrTable.h`; resolved at ROM
detect into `m_ptrs.currentMode` / `m_ptrs.battleFlowState`.

`flowState != 0` alone is unsafe in menu — always use the `currentMode == 0x0E` guard.
Matches game active-match gates (`flowState == 0` = live match, including START scoreboard).

## Patch restore on match end

`RunFrameHook` (cold path): while `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED`:

1. Until `BIT_BATTLE_RUNTIME_MODE`: read `currentMode` once per frame; latch when `== 0x0E`.
2. After latch: read only `battleFlowState` until `flow != 0`.
3. On first end-of-game frame: `Patches_RestoreOnLeave()` once, set `BIT_END_OF_GAME_PATCH_RESTORED`.

Menu frames skip all mode/flow reads. During a match after latch: one `flowState` read per frame.

`BIT_IN_GAME_INIT` is cleared when `!isInGame` (left legacy in-game gate), not on `isEndOfGame`.
`BIT_END_OF_GAME_PATCH_RESTORED` and `BIT_BATTLE_RUNTIME_MODE` are cleared on game join and when
init is cleared.

## `RunFrameHook()` sequencing (ROM detected)

1. `isInGame` from `inGame` flag; `isEndOfGame` from mode/flow; set `BIT_IN_GAME`.
2. If `isInGame && !BIT_IN_GAME_INIT` → `HandleGameJoinInit()`.
3. If `isEndOfGame && BIT_IN_GAME_INIT` → `Patches_RestoreOnLeave()`.
4. If `isInGame` → `OsdColor_ApplyOnce`, damage-notify purple.
5. Else if `BIT_IN_GAME_INIT` → clear init, transient reset (no patch restore).
6. Focused: `isInGame` → `HandleInGameLogic()`; else → out-of-game patches + settings.
