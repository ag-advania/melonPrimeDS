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

1. Until `BIT_BATTLE_RUNTIME_MODE`: latch when `currentMode == 0x0E && flowState == 0` (live
   match; ignores stale post-match flow left over from the previous round).
2. After latch: read only `battleFlowState` until `flow != 0`.
3. On first end-of-game frame: `Patches_RestoreOnLeave()` and
   `ARM9Hook_SetMatchHooksActive(false)` once (via `PatchLifecycle::RestoreOnMatchEnd()`,
   PatchLifecycleGateway Step 3 Site A), set `BIT_END_OF_GAME_PATCH_RESTORED`.

Menu frames skip all mode/flow reads. During a match after latch: one `flowState` read per frame. Before latch (lobby,
`INIT` set): one `currentMode` read per frame; `battleFlowState` is skipped until
`mode==0x0E`.

`BIT_IN_GAME_INIT` is cleared when `!isInGame` (left legacy in-game gate), not on `isEndOfGame`.
`HandleGameJoinInit` runs on legacy `inGame` rising edge (always when `!BIT_IN_GAME_INIT`) or
unpause re-init when `!BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED`. Leaving
`!isInGame` clears `BIT_END_OF_GAME_PATCH_RESTORED` even if `BIT_IN_GAME_INIT` was already clear
(e.g. unpause during scoreboard), so lobby rematch cannot stall with RESTORED blocking join.
`BIT_END_OF_GAME_PATCH_RESTORED` and `BIT_BATTLE_RUNTIME_MODE` are cleared on game join and when
init is cleared.

## `HandleGameJoinInit()` (join-only patches)

After pointer resolve: `Patches_Apply(PatchSite_GameJoin)` — **InGameAspectRatio only**.
Battle-runtime patches and hooks wait for `HandleBattleRuntimeEnter()`.

## `HandleBattleRuntimeEnter()` (battle patches + match hooks)

On first `mode==0x0E && flow==0` after join (same latch as `BIT_BATTLE_RUNTIME_MODE`):

Via `PatchLifecycle::ApplyOnBattleRuntimeEnter()` (PatchLifecycleGateway Step 3 Site B):

- `Patches_Apply(PatchSite_BattleRuntime)` — OsdColor, LowHpWarning, InstantAimFollow,
  ShowHeadshotOnline, ShowEnemyHpMeterOnline, DisableDoubleDamageMultiplier,
  NoPickingUpSpecificItems.
- `ARM9Hook_SetMatchHooksActive(true)` — all match-scoped instruction hooks.
- If native weapon switch enabled: `WeaponSwitchHook_IsSiteValid()` at `0x02003EA0`.

Per-frame `OsdColor_ApplyOnce` in `RunFrameHook` runs only while `BIT_BATTLE_RUNTIME_MODE`.

## ARM9 match hooks (lifecycle)

- **Install:** `HandleBattleRuntimeEnter` (and `ApplyConfigReload` when `BIT_BATTLE_RUNTIME_MODE`).
- **Clear:** first `flow != 0` after latch (match end) and `!isInGame` (menu leave).
- **Not** left registered in menus between matches.
- Re-attach always calls `SetARM9InstructionHook` + hook-PC write-back so JIT trampolines work
  after a prior match-end `ClearARM9InstructionHook`.

Developer builds (`MELONPRIME_ENABLE_DEVELOPER_FEATURES`): `osdAddMessage` for hook register/clear
and patch registry apply/restore (see `melonprime-patch-system.md`).

## `RunFrameHook()` sequencing (ROM detected)

1. Read `inGame`; set `BIT_IN_GAME` (keep `wasInGame` for rising-edge join).
2. **Join:** `isInGame && !BIT_IN_GAME_INIT` and (`!wasInGame` **or** `!BIT_END_OF_GAME_PATCH_RESTORED`)
   → `HandleGameJoinInit()`. Rising edge always re-inits even if `RESTORED` was stale.
3. **Match-end poll** (only while `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED`):
   latch on `mode==0x0E && flow==0` → `HandleBattleRuntimeEnter()`; then `flow!=0` →
   `Patches_RestoreOnLeave()` + `ARM9Hook_SetMatchHooksActive(false)` + set `RESTORED`.
   Stale `flow!=0` from the previous round at join must **not** latch on `0x0E` alone (that caused
   register+unregister on the same frame on rematch).
4. If `isInGame` → `OsdColor_ApplyOnce`, damage-notify purple.
5. If `!isInGame && (BIT_IN_GAME_INIT || BIT_END_OF_GAME_PATCH_RESTORED)` → clear init/RESTORED,
   deactivate hooks, transient reset — **no** `Patches_RestoreOnLeave`.
6. Focused: `isInGame` → `HandleInGameLogic()`; else → out-of-game patches + settings.
