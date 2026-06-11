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
| `isEndOfGame` | `currentMode == 0x0E && (flowState == 1 \|\| flowState == 2)` |

Addresses: `currentMode`, `battleFlowState` in `MelonPrimeGameRomAddrTable.h`; resolved at ROM
detect into `m_ptrs.currentMode` / `m_ptrs.battleFlowState`.

`flowState == 2` alone is unsafe — always use the `currentMode == 0x0E` guard.

## Patch restore on match end

`RunFrameHook`: when `isEndOfGame && BIT_IN_GAME_INIT` → `Patches_RestoreOnLeave()` (while
`isInGame` may still be true). Not tied to `!isInGame`.

`BIT_IN_GAME_INIT` is cleared when `!isInGame` (left legacy in-game gate), not on `isEndOfGame`.

## `RunFrameHook()` sequencing (ROM detected)

1. `isInGame` from `inGame` flag; `isEndOfGame` from mode/flow; set `BIT_IN_GAME`.
2. If `isInGame && !BIT_IN_GAME_INIT` → `HandleGameJoinInit()`.
3. If `isEndOfGame && BIT_IN_GAME_INIT` → `Patches_RestoreOnLeave()`.
4. If `isInGame` → `OsdColor_ApplyOnce`, damage-notify purple.
5. Else if `BIT_IN_GAME_INIT` → clear init, transient reset (no patch restore).
6. Focused: `isInGame` → `HandleInGameLogic()`; else → out-of-game patches + settings.
