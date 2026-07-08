# PatchLifecycleGateway Site D — Investigation Plan

Status: **implemented as hook-only extraction** (commit `6fd63a6e`, "Route
Site D hook deactivation through PatchLifecycle"). `DeactivateHooksOnLeaveInGame`
only calls `ARM9Hook_SetMatchHooksActive(... false ...)`; it does not call
`Patches_RestoreOnLeave`. Flag clearing, transient-input cleanup, HUD
restore, and `m_weaponSwitchPending.Clear()` all remain in `RunFrameHook`,
exactly as this document's §5 non-goals required. The §3 S6/S7 live-match
pass was **not** run before this landed — build, `audit-melonprime-srp-performance.ps1`,
and full 4-platform CI were the verification gate, matching the precedent
set by Sites A/B and the aim-reload `AimSensitivitySnapshot` change.
Gameplay smoke (including the §3 S6/S7 items) remains deferred to a final
release-readiness smoke pass, not blocking this hook-only extraction.

This continues `melonprime_patch_lifecycle_gateway_step3_plan.md` after
Sites A, B, and E all landed (see `melonprime-srp-refactor-v3-progress.md`
Batch 1 / Phase D). Site C stays an explicit non-goal (per-frame
`OsdColor_ApplyOnce` re-evaluation, pattern B, tracked as a performance
question, not a lifecycle-ownership one).

## 1. Where Site D lives today

`RunFrameHook`, `MelonPrime.cpp` (line numbers as of `123d0b2b`):

```cpp
else if (!isInGame
    && (m_flags.test(StateFlags::BIT_IN_GAME_INIT)
        || m_flags.test(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED))) {
    m_flags.clear(StateFlags::BIT_IN_GAME_INIT);
    m_flags.clear(StateFlags::BIT_END_OF_GAME_PATCH_RESTORED);
    m_flags.clear(StateFlags::BIT_BATTLE_RUNTIME_MODE);
#ifdef MELONPRIME_DS
    ARM9Hook_SetMatchHooksActive(
        emuInstance->getNDS(), localCfg, m_currentRom.romGroupIndex,
        this, false, emuInstance);
#endif
    // weaponSwitchPending cleared in the DS block below where ordering matters.
    ResetTransientInputState(TR_OverlayHeld | TR_DirectTransform | TR_BipedFire);
#ifdef MELONPRIME_CUSTOM_HUD
    CustomHud_EnsurePatchRestored(
        emuInstance, localCfg, m_currentRom, m_playerPosition, false);
#endif
#ifdef MELONPRIME_DS
    m_weaponSwitchPending.Clear();
#endif
}
```

## 2. Site A vs Site D — what's actually different

| | Site A (`RestoreOnMatchEnd`, gateway-owned) | Site D (inline, ungated) |
|---|---|---|
| Trigger condition | `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED && BIT_BATTLE_RUNTIME_MODE`, then `flowState != FLOW_ACTIVE_MATCH` | `!isInGame && (BIT_IN_GAME_INIT \|\| BIT_END_OF_GAME_PATCH_RESTORED)` |
| Meaning | The in-RAM battle flow state transitioned out of an active match while the legacy `inGame` flag may still read true (e.g. post-match scoreboard) | The legacy `inGame` RAM flag itself went false — the player actually returned to a menu/left the ROM's in-game state entirely |
| Calls `Patches_RestoreOnLeave`? | **Yes** | **No** |
| Calls `ARM9Hook_SetMatchHooksActive(false)`? | Yes | Yes |
| Clears `BIT_END_OF_GAME_PATCH_RESTORED`? | No — **sets** it | Yes — clears it (along with `BIT_IN_GAME_INIT`, `BIT_BATTLE_RUNTIME_MODE`) |
| Touches HUD (`CustomHud_EnsurePatchRestored`)? | No | Yes (custom-HUD build only) |
| Touches transient input / weapon-switch-pending? | No | Yes |
| Frequency | Once per match-end transition (fires at most once per match) | Fires on every frame the guard condition holds until the flags clear — effectively a debounced "catch-up" cleanup, not a single well-defined edge |

**The two are not the same event.** Site A is "the match ended while we're
still inside the ROM's in-game state." Site D is "we are (or already were,
per stale flags) fully out of the in-game state" — the safety-net case is
when Site A's own trigger condition was skipped entirely (see §2.1).

### 2.1 Why Site D exists as a *separate*, broader net

Site A only fires while `BIT_IN_GAME_INIT` is set and the flow-state poll
is active. If the player leaves the in-game state through a path that never
reaches Site A's trigger — ROM reset mid-match, forced disconnect, a
flow-state value the poll never observes, or leaving before
`BIT_BATTLE_RUNTIME_MODE` was ever set (patches/hooks were never applied in
the first place) — `BIT_IN_GAME_INIT` and/or `BIT_END_OF_GAME_PATCH_RESTORED`
can still be set when `isInGame` goes false. Site D is the fallback that
guarantees hooks get deactivated and lifecycle flags get cleared regardless
of how the player left, even if Site A's own restore never ran.

### 2.2 Why Site D does NOT call `Patches_RestoreOnLeave`

Two overlapping reasons, both intentional (not an oversight):

1. **Normal path**: if Site A already ran for this match, patches are
   already restored (`Patches_RestoreOnLeave` was already called, and every
   `StaticWordPatch`-based module's `RestoreOnce` is itself idempotent/
   self-guarded via its `s_applied` flag). Calling it again in Site D would
   be a harmless no-op in this path.
2. **Never-applied path**: if the player left before `BIT_BATTLE_RUNTIME_MODE`
   was ever set (patches/hooks were never applied — e.g. quit from the
   lobby, or the match-enter latch never fired), there is nothing to
   restore. `Patches_RestoreOnLeave` would still be a safe no-op here too
   (same self-guard mechanism), but calling it changes Site D's contract
   from "hook deactivation only" to "hook deactivation + patch restore,"
   which is a different, larger surface than what Site D was designed to
   guarantee.

So in practice, adding the call would very likely be behavior-neutral
(every restore path is self-guarded), but "very likely neutral" is not the
same as "verified neutral," and Site D runs on every out-of-game frame
while the guard condition holds — not a single edge — so a mistake here has
more exposure than Site A's one-shot call.

## 3. Required verification before touching Site D

Per the smoke checklist convention used throughout this repo's `.claude/`
docs (`melonprime-full-refactor-plan.md` §3):

- **S6 (patch ON/OFF)**: toggle each `PatchSite_BattleRuntime` patch
  (OsdColor, LowHpWarning, InstantAimFollow, ShowHeadshotOnline,
  ShowEnemyHpMeterOnline, DisableDoubleDamageMultiplier,
  NoPickingUpSpecificItems) ON → verify in-game reflection → OFF → verify
  restore, run once *before* any Site D change and once *after*, diff the
  observed behavior.
- **S7 (lifecycle)**: pause/resume, reset, stop→restart, and — specific to
  Site D — the three leave-paths in §2.1 (mid-match reset, forced
  disconnect/menu-quit, quit-before-`BIT_BATTLE_RUNTIME_MODE`) each need a
  before/after comparison to confirm no stale patch state leaks into the
  next match.

This plan does not attempt either pass. It only establishes what "verify
first" (referenced in `melonprime_patch_lifecycle_gateway_step3_plan.md`
§3) concretely means for Site D.

## 4. Is bundling Site D into `PatchLifecycle` still desirable, now that A/B/E are done?

**Weaker case than it looked before Sites A/B/E landed.** The original
motivation ("Site D's `ARM9Hook_SetMatchHooksActive(false)` call could ride
along with Site A's extraction — same underlying primitive, different call
site") assumed Site A hadn't been extracted yet, so bundling them was one
commit doing two things at once. Now that Site A is its own gateway
function, Site D is the *only* remaining inline
`ARM9Hook_SetMatchHooksActive` caller in `RunFrameHook`'s match-lifecycle
region (the other bare mentions are inside `PatchLifecycle.cpp` itself now).
Extracting it would be a small, self-contained move:

```cpp
namespace MelonPrime::PatchLifecycle {

// Step 3 / Site D candidate — deactivates match ARM9 hooks only (no
// Patches_RestoreOnLeave — see melonprime_patch_lifecycle_gateway_site_d_plan.md
// §2.2 for why). The caller still owns clearing BIT_IN_GAME_INIT /
// BIT_END_OF_GAME_PATCH_RESTORED / BIT_BATTLE_RUNTIME_MODE (frame-state
// ownership) and the transient-input / weapon-switch-pending / HUD-patch
// cleanup that runs alongside it in RunFrameHook.
void DeactivateHooksOnLeaveInGame(melonDS::NDS* nds,
                                   EmuInstance* emu,
                                   Config::Table& cfg,
                                   const RomAddresses& rom,
                                   MelonPrimeCore* core);

}
```

Arguments for extracting now:

- Small, single-call-site move — same shape as Site E, the lowest-risk
  extraction so far.
- Closes the "one remaining inline `ARM9Hook_SetMatchHooksActive` call"
  gap, making `PatchLifecycle` the sole owner of all four
  `ARM9Hook_SetMatchHooksActive` call sites in the codebase.

Arguments for leaving it inline:

- Site D's block is not *only* patch/hook lifecycle — it interleaves
  `ResetTransientInputState`, `CustomHud_EnsurePatchRestored`, and
  `m_weaponSwitchPending.Clear()` in a specific order alongside the hook
  deactivation. Pulling just the `ARM9Hook_SetMatchHooksActive` call out
  fragments one conceptual "fully left the game" cleanup sequence across
  two files for a smaller win than Site B's three-call bundle was.
- No live-match S6/S7 pass has been run yet on this exact code path (see
  §3) — extracting cold-path lifecycle code without verification is exactly
  the mistake the original Step 3 plan was written to avoid.

**Implemented as recommended: not bundled.** `DeactivateHooksOnLeaveInGame`
was extracted as its own single-purpose call (hook deactivation only,
exactly matching the signature sketch above) — not merged with the
surrounding transient-input/HUD/weapon-switch cleanup. The §3 S6/S7 pass
was not run first (see the status line at the top); this was a deliberate
choice by the planning owner to gate on build+audit+CI instead, not an
oversight of this document's original recommendation.

## 5. Non-goals

- Does not add `Patches_RestoreOnLeave` to Site D (see §2.2) — confirmed
  still true post-implementation; `DeactivateHooksOnLeaveInGame` contains
  only the `ARM9Hook_SetMatchHooksActive` call.
- Does not touch the flag-clear order, the `ResetTransientInputState` bit
  mask, `CustomHud_EnsurePatchRestored`, or `m_weaponSwitchPending.Clear()`
  ordering — all of that stays exactly where and how it is, confirmed by
  `git diff` review of the implementation commit.
- `DeactivateHooksOnLeaveInGame` is now implemented (§4's sketch became the
  shipped signature verbatim).
