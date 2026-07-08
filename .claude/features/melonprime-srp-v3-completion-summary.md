# MelonPrime SRP v3 — Completion Summary

Concise developer-facing summary of the whole SRP v3 effort (Immediate Plan
→ Phase 7-16 continuation → Phase A-D → Batch 1-6 → Post-Batch-6 cleanup).
The full blow-by-blow history, commit hashes, and per-phase CI run IDs live
in `melonprime-srp-refactor-v3-progress.md`; this document is the "read
this first" overview.

## What SRP v3 was solving

Five subsystems had grown responsibilities beyond a single clear owner:
runtime config loading was tangled with applying it into `MelonPrimeCore`;
cursor clip/warp/capture logic and Qt mouse-event routing lived in the same
file; the in-game HUD editor's widget-construction code was duplicated
across every property type; and `RunFrameHook`'s patch/ARM9-hook lifecycle
calls were inlined directly at every call site instead of going through a
single gateway.

## What was split (new units, by area)

| Area | New unit(s) | What it owns now |
|---|---|---|
| Runtime config | `MelonPrimeRuntimeConfig.h/.cpp` — `RuntimeConfigSnapshot`, `AimConfigSnapshot` | Pure `Load*ConfigSnapshot(Config::Table&)` read+clamp; `MelonPrimeCore::Apply*ConfigSnapshot(...)` is the only place that writes into core state |
| Input projection | `MelonPrimeInputProjection.h` (header-only) | Hotkey → down/press bit projection, `FORCE_INLINE`, zero new abstraction cost on the hot path |
| Screen cursor | `MelonPrimeScreenCursorPolicy.h/.cpp` | Cursor clip/warp/capture/confinement policy: `ClipCenter1px`, `Unclip`, `UpdateClipIfNeeded`, `ContainAimCursorIfNeeded`, `ReleaseForClose`, `ConfineToBottomScreen` |
| HUD editor widgets | `MelonPrimeHudEditorFormBuilder.h/.cpp` | Every property-panel widget factory: checkbox/combo/spin/double-spin/opacity-slider/line-edit/color-picker/sub-color/color-overlay-row, all `WidgetFactoryContext`-based |
| Patch/hook lifecycle | `MelonPrimePatchLifecycle.h/.cpp` | `ResetForEmuStart` / `ResetForBoot` / `RestoreForEmuStop` / `ReapplyForConfigReload` / `ApplyOutOfGameFrame` (Site E) / `RestoreOnMatchEnd` (Site A) / `ApplyOnBattleRuntimeEnter` (Site B) / `DeactivateHooksOnLeaveInGame` (Site D) / `DeactivateHooksForRomDetect` (ROM-detect cold path). `PatchLifecycle` owns every direct `ARM9Hook_SetMatchHooksActive` call site in `qt_sdl` |

`MelonPrimeHudConfigOnScreenEdit.cpp` is now a thin delegate layer over
`MelonPrimeHudEditorFormBuilder` for every widget factory — only
`addSeparator`, layout/populate/snapshot logic, and crosshair/preview-
specific code remain in that file.

Internally, `MelonPrimePatchLifecycle.cpp` now funnels match-hook activation/
deactivation through a private helper; public gateway APIs are unchanged.

## What was deliberately kept inline (and why)

| Item | Why it stayed |
|---|---|
| `RunFrameHook`'s 19-step order | Documented contract in `melonprime-srp-performance-contract.md`; hot path, no new abstraction cost allowed |
| Site C — per-frame `OsdColor_ApplyOnce` re-apply | Explicit non-goal from the start — pattern B (game overwrites the RAM it patches), tracked separately as a performance question, not a lifecycle-ownership one |
| Site D surrounding cleanup | Only hook deactivation moved to `PatchLifecycle` (`DeactivateHooksOnLeaveInGame`). Flag clears, transient input reset, `CustomHud_EnsurePatchRestored`, and `m_weaponSwitchPending.Clear()` stay inline in `RunFrameHook` — frame-state and per-subsystem ownership, not patch-lifecycle ownership |
| `ARM9 hook context化` | Deferred — no batch touched ARM9 hook dispatch internals |
| HUD render unity split | Deferred — `MelonPrimeHudRender*.inc` unity-fragment structure is untouched |
| `MelonPrimeCore` hot state struct extraction | Deferred — member layout in `MelonPrime.h` is untouched |
| Screen mouse router rewrite | Deferred — `mousePressEvent`/`mouseMoveEvent`/`mouseReleaseEvent` routing in `Screen.cpp` is untouched |
| `PlatformInput` raw ownership redesign | Deferred — `MelonPrimePlatformInput.h`'s raw-filter ownership model is untouched |
| `shouldConfineCursorToBottomScreen()` (the predicate) | Batch 5 moved the *clip implementation* (`ConfineToBottomScreen`); the decision of whether to confine stays a `ScreenPanel` method, already exposed via the existing `...ForPolicy()` narrow accessor |

## Notable findings along the way

- **Phase 12** — extending the Step 2 `WidgetFactoryContext` pattern surfaced
  a real dangling-reference bug in already-merged code: Step 2's factories
  captured `ctx` (a stack-local) by reference in `connect(...)` lambdas, and
  `populating` was a frozen value-copy rather than a live reference. Fixed
  by making `populating` a `bool&` and capturing `ctx` by value everywhere.
- **Batch 4** — the aim config reload paths audit found
  `MelonPrimeCore::ApplyAimAdjustSetting` had zero call sites anywhere in
  `src/` (superseded by the `AimConfigSnapshot` path). Removed in the
  Post-Batch-6 cleanup response — this dead-code item is closed.
- **Batch 5** — extracting `ConfineToBottomScreen` found a byte-identical
  duplicate helper (`getVirtualScreenRectForBottomClip` in `Screen.cpp` vs.
  the pre-existing `getVirtualScreenRect` in `MelonPrimeScreenCursorPolicy.cpp`)
  and an inline `!isVisible()||!window()||!window()->isActiveWindow()` check
  that duplicated the existing `isActiveVisibleWindowForMelonPrime()`
  accessor. Both were deduplicated as part of the move (same expressions,
  not new behavior).

## CI verification history

Every code-changing phase/batch was confirmed on a throwaway `ci/*` branch
(direct pushes to `highres_fonts_v3` don't auto-trigger CI — only
`master`/`ci/*` pushes and PRs into `master` do), each kept rather than
deleted, matching the project's existing convention:

| Branch | Covers | Result |
|---|---|---|
| `ci/phase0-refactor-audits` | Pre-SRP-v3 V3 Phase 1 baseline | success (historical) |
| `ci/phase11-16-verification` | Phase 11-16 continuation + Phase D (Site E) | success, all 4 platforms, both HEADs tested |
| `ci/phase-d-sites-a-b-verification` | Batch 1 (PatchLifecycleGateway Sites A/B) | success, all 4 platforms |
| `ci/batch5-screencursorpolicy-verification` | Batch 5 (`ConfineToBottomScreen`) | success, all 4 platforms |

Batch 5 final run IDs at HEAD `0e91db0d` — Windows
[28940048680](https://github.com/ag-advania/melonPrimeDS/actions/runs/28940048680)
(all audit steps including `Audit platform scatter budget`, the key check
for this platform-`#ifdef`-touching change, + build all green), Ubuntu
[28940048728](https://github.com/ag-advania/melonPrimeDS/actions/runs/28940048728),
macOS [28940048822](https://github.com/ag-advania/melonPrimeDS/actions/runs/28940048822),
BSD [28940048796](https://github.com/ag-advania/melonPrimeDS/actions/runs/28940048796)
— all success.

Doc-only / comment-only batches (Batch 2, Batch 3, Batch 4) did not require
a fresh full-matrix CI run per the accelerated plan's CI Strategy — local
build + the relevant `audit-*.ps1` scripts were sufficient.

`59c229f9` closed out Batches 1-6 (historical reference only). A
Post-Batch-6 cleanup response in reply to an external push audit landed on
top of that:

```text
Post-Batch-6 cleanup response:
- Removed dead ApplyAimAdjustSetting.
- Rechecked Site B flag ordering and confirmed no issue.
- Made completion summary self-contained.
- Recorded gameplay smoke handoff.
- Added Site D investigation doc.
- Added aim reload outcome C design note.
```

**Latest close-out HEAD: `e233f7f6eae0124e657ee8cd7715f3e89a3a3b3d`** — see
`melonprime-srp-refactor-v3-progress.md`'s "Post-Batch-6 Cleanup Response"
section for the exact commit list.

## Manual smoke coverage (what's verified vs. still open)

Done (macOS, via computer-use against a real MPH ROM):
- No-ROM launch/splash, ROM load, in-game HUD editor open
- Every `HudEditorForm` widget: opacity slider, line edit, color picker
  (+ custom-colors persistence), overlay-row ON/OFF, spin box, combo box
- Cancel/restore path (populating-guard re-entry)
- App window close (`ReleaseForClose`)

Still open (flagged, not blocking, per the accelerated plan's "manual smoke
for gameplay-sensitive batches only" standard). A live gameplay pass
(loading Adventure Mode via computer-use) was attempted 2026-07-08 but
handed off to the user instead — they closed the automated session and
opted to run this pass themselves rather than have it done via
computer-use, given the time cost of blind-clicking through DS touch-menu
navigation:
- In-game aim, shoot/zoom, weapon switch, morph ball boost, Adventure map
  WASD, pause/resume, stop/reset — none of these were exercised end-to-end
  in a live match, so PatchLifecycleGateway Sites A/B (match-end restore,
  battle-runtime enter) have build+audit+CI verification but not a live
  match-transition smoke pass
- Windows `ClipCursor` and Linux `resetAimMouseDelta` branches of
  `ReleaseForClose` / `ConfineToBottomScreen` (macOS session only exercises
  the cross-platform arrow-cursor branch)
- `AddSubColorRow`'s Overall/Custom combo specifically (low risk — combo
  box and color picker were each validated independently)

## Deferred (do not touch without a dedicated plan)

```text
RunFrameHook large split
ARM9 hook contextization
HUD render unity split
MelonPrimeCore hot state struct extraction
Screen mouse router rewrite
PlatformInput raw ownership redesign
PatchLifecycleGateway Site C (explicit non-goal)
Aim config reload path unification outcome B (see
  melonprime_aim_config_reload_paths_audit.md — outcome C shipped as
  AimSensitivitySnapshot)
```
