# PatchLifecycleGateway Step 3 — Design Plan (doc only)

Status: Site E implemented (2026-07-08, `melonprime-srp-refactor-v3-progress.md`
Phase D). Sites A and B are still **design only, not implemented** — see §3.

Steps 1–2 of `MelonPrimePatchLifecycle` (`MelonPrimePatchLifecycle.h/.cpp`)
already own the **cold, out-of-frame** patch/hook lifecycle:
`OnEmuStart` / `ResetRuntimeStateForBoot` / `OnEmuStop` (Step 1,
`ResetForEmuStart` / `ResetForBoot` / `RestoreForEmuStop`) and
`ApplyConfigReload`'s patch/hook reapply (Step 2, `ReapplyForConfigReload`).
`RunFrameHook` itself has **not** been touched by either step.

This plan inventories the patch/hook call sites that remain inline inside
`RunFrameHook` (and its cold helper `HandleBattleRuntimeEnter`), so a future
Step 3 can extract them one call site at a time instead of all at once.

## 1. Current call-site map (`MelonPrime.cpp`, `RunFrameHook` + helpers)

| # | Site | Line(s) | What it does | Gated by |
|---|---|---|---|---|
| A | Match-end restore | `MelonPrime.cpp:344-353` | `Patches_RestoreOnLeave(ctx)` + `ARM9Hook_SetMatchHooksActive(..., false, ...)`, then sets `BIT_END_OF_GAME_PATCH_RESTORED` | `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED && BIT_BATTLE_RUNTIME_MODE`, then `flowState != FLOW_ACTIVE_MATCH` |
| B | Battle-runtime enter | `MelonPrime.cpp:341` → `HandleBattleRuntimeEnter()` (`MelonPrime.cpp:610-622`) | `Patches_Apply(PatchSite_BattleRuntime, ctx)` + `ARM9Hook_SetMatchHooksActive(..., true, ...)` + (if native weapon switch) `WeaponSwitchHook_IsSiteValid(...)`, then sets `BIT_BATTLE_RUNTIME_MODE` | First `mode==0x0E && flow==0` while `BIT_IN_GAME_INIT && !BIT_END_OF_GAME_PATCH_RESTORED && !BIT_BATTLE_RUNTIME_MODE` |
| C | Per-frame OSD re-apply | `MelonPrime.cpp:358-361` | `OsdColor_ApplyOnce(emuInstance, localCfg, m_currentRom)` — **not** registry-driven (pattern B: re-evaluated every frame because the game overwrites the RAM it patches) | `isInGame && BIT_BATTLE_RUNTIME_MODE` |
| D | Leave-in-game hook deactivate | `MelonPrime.cpp:375-399` | `ARM9Hook_SetMatchHooksActive(..., false, ...)` (no `Patches_RestoreOnLeave` — deliberately, see `notes/MelonPrimeBattleFlowState.md`), plus `ResetTransientInputState(...)`, `CustomHud_EnsurePatchRestored(...)`, `m_weaponSwitchPending.Clear()` | `!isInGame && (BIT_IN_GAME_INIT \|\| BIT_END_OF_GAME_PATCH_RESTORED)` |
| E | Out-of-game per-frame patch | `MelonPrime.cpp:412-419` | `Patches_Apply(PatchSite_OutOfGameFrame, ctx)` — self-guarded registry entries (FixWifi / UseFirmwareLanguage / ExpandStageMatrix) | `focused && !isInGame` |

Site A and Site D are **both** hook-deactivation sites but are not the same
call: A fires once, on the match-end poll transition, restores static
patches, and sets `BIT_END_OF_GAME_PATCH_RESTORED`. D fires once, on the
`!isInGame` transition (leaving in-game entirely — including from the
post-A scoreboard, or from a `!BIT_IN_GAME_INIT` reset path that never
reached A), and does **not** call `Patches_RestoreOnLeave` again (already
restored by A, or never applied because the player left before B ran). This
asymmetry is intentional (documented in
`notes/MelonPrimeBattleFlowState.md`) and must not be collapsed into a
single call during extraction.

## 2. What each site would need to become gateway-safe

| Site | Needed inputs already available at call site | New `PatchLifecycle` function shape (illustrative, not final) |
|---|---|---|
| A | `nds`, `emuInstance`, `localCfg`, `m_currentRom`, `this` (for `ARM9Hook_SetMatchHooksActive`) | `PatchLifecycle::RestoreOnMatchEnd(nds, emu, cfg, rom, core)` → `Patches_RestoreOnLeave` + `ARM9Hook_SetMatchHooksActive(false)` |
| B | same, plus `m_enableNativeWeaponSwitch` | `PatchLifecycle::ApplyOnBattleRuntimeEnter(nds, emu, cfg, rom, core, nativeWeaponSwitch)` → `Patches_Apply(BattleRuntime)` + `ARM9Hook_SetMatchHooksActive(true)` + conditional `WeaponSwitchHook_IsSiteValid` |
| C | — | **Not a Step 3 candidate.** Pattern B (per-frame RAM re-evaluation) is intentionally outside the registry; V5/V6 performance work already tracks whether this can become edge-triggered (see `melonprime-performance.md` Invalidation Ledger) — that is a performance change, not a lifecycle-ownership change, and stays out of this gateway. |
| D | `nds`, `emuInstance`, `localCfg`, `this` — but this site also touches non-patch state (`ResetTransientInputState`, `CustomHud_EnsurePatchRestored`, `m_weaponSwitchPending.Clear()`) that is **not** patch-registry/ARM9-hook state | Only the `ARM9Hook_SetMatchHooksActive(false)` call is a gateway candidate here; the rest stays inline in `RunFrameHook` (mixing concerns into one gateway call would blur the "DS ARM9 patch lifecycle only" boundary Step 1 established) |
| E | `nds`, `emuInstance`, `localCfg`, `m_currentRom` | `PatchLifecycle::ApplyOutOfGameFrame(nds, emu, cfg, rom)` → `Patches_Apply(PatchSite_OutOfGameFrame, ctx)` |

## 3. Recommended order (if/when Step 3 is implemented)

Per the continuation plan's own risk assessment (medium–high; do not rush):

```text
1. doc only (this file)
2. [DONE 2026-07-08] Site E (out-of-game per-frame patch) — simplest: no
   state-flag side effects beyond the registry's own self-guards, runs
   every out-of-game frame regardless of match history, easiest to verify
   with S6. Implemented as PatchLifecycle::ApplyOutOfGameFrame(); see
   melonprime-srp-refactor-v3-progress.md Phase D.
3. [DONE 2026-07-08] Site A (match-end restore) — single well-defined
   transition edge; the `BIT_END_OF_GAME_PATCH_RESTORED` flag write stayed
   in RunFrameHook (state-flag ownership, not patch-lifecycle ownership).
   Implemented as `PatchLifecycle::RestoreOnMatchEnd()`, wrapping
   `Patches_RestoreOnLeave` + `ARM9Hook_SetMatchHooksActive(false)` — see
   `melonprime-srp-refactor-v3-progress.md` Batch 1 Site A.
4. [DONE 2026-07-08] Site B (battle-runtime enter) — implemented as
   `PatchLifecycle::ApplyOnBattleRuntimeEnter(...)`, pulling in
   `WeaponSwitchHook_IsSiteValid` alongside the patch/hook pair rather than
   partially extracting (it is a public static `MelonPrimeCore` method, so
   calling it from `PatchLifecycle.cpp` — which already includes
   `MelonPrime.h` and takes a `MelonPrimeCore*` — introduced no ownership
   coupling). `HandleBattleRuntimeEnter()` remains a single cold outlined
   function and still owns setting `BIT_BATTLE_RUNTIME_MODE` — see
   `melonprime-srp-refactor-v3-progress.md` Batch 1 Site B.
```

Both extractions were verified as byte-for-byte call-order-preserving via
`git diff` review (same calls, same order, same arguments, only the
call site changed), plus `audit-melonprime-srp-performance.ps1` and a
macOS launch/close smoke. Full 4-platform CI ran at the batch boundary —
see the progress doc's Batch 1 CI section.

Site D's `ARM9Hook_SetMatchHooksActive(false)` call could ride along with
Site A's extraction (same underlying primitive, different call site) — but
only after confirming with S6/S7 that both extracted call sites still fire
in exactly the same frame-relative order as today. This is a "verify
before doing" note, not a decision to defer D's extraction indefinitely.

## 4. What must not change

Per `melonprime-srp-performance-contract.md`'s `RunFrameHook` order table
(steps 10-13 cover A/B/C in that ordering) — any Step 3 extraction must
preserve:

- The **19-step RunFrameHook order** documented in the performance
  contract, unchanged.
- The **A-before-D-in-different-transitions** asymmetry (A fires on the
  match-end poll edge; D fires on the `!isInGame` edge; they are not
  interchangeable and D does not re-call `Patches_RestoreOnLeave`).
- `HandleBattleRuntimeEnter()` remaining a single cold, outlined function
  (do not inline it back into `RunFrameHook`, and do not split its body
  across `MelonPrime.cpp` and `MelonPrimePatchLifecycle.cpp` without
  moving the weapon-switch trampoline call too).
- `Patches_Apply(PatchSite_OutOfGameFrame, ctx)` staying self-guarded and
  cheap (Site E is polled every out-of-game focused frame — this is by
  design per `melonprime-gameplay-runtime.md` §7, not a bug to "fix" by
  gating it further in this phase).

## 5. Verification requirements for a future Step 3 PR

Any Step 3 implementation PR must:

1. Touch **one site at a time** (E, then A, then B — see §3), each its own
   commit.
2. Run `audit-melonprime-srp-performance.ps1` (RunFrameHook order review is
   manual grep, not CI-enforced — cross-check against §4 by hand).
3. Pass manual smoke S6 (patch ON/OFF → in-game reflect → OFF → restore)
   and S7 (pause/resume, reset, stop→restart — patch state must not leak)
   from `completed/melonprime-full-refactor-plan.md` §3, plus S2/S3
   (join, weapon switch) for site B.
4. Diff `RunFrameHook`'s generated code is out of scope for a Windows-only
   comparison here (this is C++-level call reshuffling inside an already
   cold-outlined helper for B, and a direct-call replacement for A/E — not
   a hot-path change), but the call **order** relative to the surrounding
   `RunFrameHook` steps must be reviewed by hand against §4's list.

## 6. Explicit non-goals for Step 3

```text
- Do not touch Site C (OsdColor per-frame re-apply) — performance-tracked
  elsewhere, not a lifecycle-ownership change.
- Do not move HUD-owned restore (CustomHud_EnsurePatchRestored) into
  PatchLifecycle — Step 1 already scoped PatchLifecycle to "DS ARM9 patch
  lifecycle only; Custom HUD patch state stays in lifecycle."
- Do not move ResetTransientInputState or m_weaponSwitchPending.Clear()
  into PatchLifecycle — these are input/runtime state, not patch state.
- Do not reorder RunFrameHook's 19 documented steps.
```
