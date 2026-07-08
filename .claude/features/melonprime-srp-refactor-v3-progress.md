# MelonPrime SRP Refactor v3 — Progress

Branch: `highres_fonts_v3` (active)

Merged from `melonprime-srp-refactor-v3` in PR #520 (`c5e95f55`)

## Post-Merge Phase Status

| Phase | Title | Status | Commit |
|---|---|---|---|
| 7 | HUD FormBuilder Step 2 | ✅ Done | `c5811762` |
| 8 | ScreenCursorPolicy friend reduction | ✅ Done | `c95b7959` |
| 9 | PatchLifecycleGateway Step 2 | ✅ Done | `c02768eb` |
| 10 | RuntimeConfig aim sensitivity | ✅ Done | `6f543432` |

All post-merge phases (7–10) complete.

## Immediate Plan (merged via PR #520)

| PR | Title | Status | Commit |
|---|---|---|---|
| 1 | RuntimeConfigSnapshot | ✅ Merged-ready | `87ccca2d` |
| 2 | SRP/Performance Audit | ✅ Merged-ready | `6b0ffdfd` |
| 3 | InputProjection header-only | ✅ Merged-ready | `babc9445` |
| 4 | ScreenCursorPolicy | ✅ Merged-ready | `21085bab` |
| 5 | HUD Editor FormBuilder Step 1 | ✅ Merged-ready | `f8c4cc53` |
| 6 | PatchLifecycleGateway Step 1 | ✅ Merged-ready | `87a99f4b` |

Follow-up commits on branch:

| Topic | Commit |
|---|---|
| Push-audit cleanups (L1–L4) | `36bd9429` |
| CI fix (scatter facade + rg-free SRP audit) | `99a9ae95` |
| CI workflow audit step split | `d7992c43` |

## CI Verification

| Platform | Status | Notes |
|---|---|---|
| Windows | ✅ PASS | run `28924163273` / job `85807860745` (pre Phase 11-16); re-confirmed at HEAD `81f9fe49` via run `28930046742` |
| Ubuntu | ✅ PASS | user confirmed (mac/linux also green); re-confirmed at HEAD `81f9fe49` via run `28930046607` |
| macOS | ✅ PASS | user confirmed; re-confirmed at HEAD `81f9fe49` via run `28930046683` |
| BSD | ✅ PASS | confirmed at HEAD `81f9fe49` via run `28930046697` (previously unconfirmed) |

### Windows CI (run `28924163273`, pre Phase 11-16)

All steps passed:

```text
Audit config defaults
Audit HUD key parity
Check inc ownership
Audit Metroid literal budget
Audit platform scatter budget
Audit color dialog prefs
Audit MelonPrime SRP performance
Verify generated HUD schema
Configure
Build
Verify developer HUD golden harness is absent from release binary
Upload artifact (melonPrimeDS-windows-x86_64)
```

Artifact digest: `sha256:a5ba47b3081219e23c8e0fb7c647712fa92cdc5180ada4abaead48ec9e25b09a`

### Full-matrix CI at HEAD `81f9fe49c86dcdbe489b5d0858d635ed3ceb5b3f` (Phase A, 2026-07-08)

`highres_fonts_v3` direct pushes do not auto-trigger any of the 4 workflows
(all are `on: push: branches: [master, ci/*]` / `pull_request: branches:
[master]` / `workflow_dispatch`). To get a same-HEAD full-matrix run without
touching `master`, pushed a throwaway `ci/phase11-16-verification` branch at
this exact commit (kept, not deleted — same convention as the earlier
`ci/phase0-refactor-audits` branch used for V3 Phase 1 verification).

| Platform | Run | Conclusion |
|---|---|---|
| Windows | [28930046742](https://github.com/ag-advania/melonPrimeDS/actions/runs/28930046742) | success — all audit steps (config defaults, HUD key parity, inc ownership, literal budget, platform scatter, color dialog prefs, SRP/performance, HUD schema) + build + golden-harness-absent check + artifact upload all green |
| Ubuntu | [28930046607](https://github.com/ag-advania/melonPrimeDS/actions/runs/28930046607) | success — Audits job (same 8 audits) + x86_64/aarch64 build + AppImage jobs all green |
| macOS | [28930046683](https://github.com/ag-advania/melonPrimeDS/actions/runs/28930046683) | success |
| BSD | [28930046697](https://github.com/ag-advania/melonPrimeDS/actions/runs/28930046697) | success |

All 4 platforms green at the exact Phase 11-16 completion commit. BSD CI
item in the Merge Checklist below is now closed.

## PR Summary (for GitHub)

### Summary

- Separate runtime config read/clamp from core apply (`RuntimeConfigSnapshot`)
- Add SRP/performance audit script and contract; wire into Ubuntu/Windows CI
- Extract header-only input projection; screen cursor policy; HUD editor form helpers; patch lifecycle gateway (Step 1)
- Fix platform scatter budget (ScreenCursorPolicy as facade) and remove `rg` dependency from SRP audit

### Test plan

- [x] Windows CI — audits, schema verification, configure, build, golden harness check, artifact upload
- [x] Ubuntu CI
- [x] macOS CI
- [x] BSD CI (run `28930046697` at HEAD `81f9fe49`)
- [x] Manual smoke (macOS, HEAD `81f9fe49`, see "Manual Smoke" section below) — MPH boot, ROM load, Custom HUD editor (opacity slider / line edit / color picker / overlay-row ON-OFF / spin box / combo box / cancel-restore), app close. Not yet covered: aim/shoot/zoom/weapon-switch/morph-boost/Adventure-WASD gameplay smoke, Windows/Linux platform-specific cursor smoke (S18-S20 style)

## Manual Smoke (macOS, HEAD `81f9fe49`, 2026-07-08)

Ran via computer-use against the freshly-built `build-mac/melonPrimeDS.app`
(confirmed via window title `MelonPrimeDS (build 2026-07-08 16:35:41 GMT+9)`,
not a stale `build-mac-release` copy).

| Check | Result |
|---|---|
| No-ROM launch | Splash screen renders, Japanese menu localized, no crash |
| ROM load (`Metroid Prime - Hunters (Japan).nds`) | Boots to hunter-intro cutscenes at ~24-60fps, no crash |
| MelonPrime menu -> Settings -> Custom HUD tab -> "HUD配置を編集" | Opens in-game overlay editor over live gameplay |
| Select HP element -> property panel | Opens with all Phase 12-14 widgets visible |
| Opacity slider (`AddOpacitySliderRow`) | Dragged 100%->41%, label updated live, no crash |
| Line edit / prefix field (`AddLineEditRow`) | Typed "HP:", updated live, no crash |
| Color picker (`AddColorPickerRow` / `PickAndApplyColor`) | Opened `ColorDialogPrefs::getColor` dialog, custom-colors row present (persistence intact), picked color -> button swatch + text color updated correctly (`UpdateColorButton` luma logic) |
| Overlay-row ON/OFF radios (`AddColorOverlayRow`) | Toggled OFF then back ON, no crash — this is the exact widget shape whose Step-2-era `WidgetFactoryContext` capture bug was fixed in Phase 12 |
| Spin box (`AddSpinBoxRow`) | Stepper click incremented offset value |
| Combo box (`AddComboBoxRow`) | Opened anchor dropdown (9 positions), closed via Escape |
| Cancel (discard edits, restore snapshot) | Editor closed cleanly, returned to Settings dialog, no crash — exercises the populating-guard path fixed in Phase 12 (widgets get programmatically reset while `m_populating` is briefly true) |
| App window close (`Screen::beginClose` -> `releaseCursorStateForClose` -> `ScreenCursorPolicy::ReleaseForClose`) | Clean process exit, confirmed via `ps aux` (no crash, no hang, no zombie) |

Not covered in this pass (needs a longer play session / different platforms):
in-game aim, shoot/zoom, weapon switch, morph ball boost, Adventure map WASD,
pause/resume, `AddSubColorRow`'s Overall/Custom combo specifically (structurally
low-risk given combo box and color picker were both validated independently),
Windows `ClipCursor` release and Linux `resetAimMouseDelta` release paths in
`ReleaseForClose` (platform-specific branches, macOS-only session here).

## PR 1: RuntimeConfigSnapshot

**Added:** `MelonPrimeRuntimeConfig.h/.cpp`

**Changed:** `MelonPrimeLifecycle.cpp`, `MelonPrime.h`, `CMakeLists.txt`

**Behavior:** Config read/clamp separated; apply order unchanged.

## PR 2: SRP/Performance Audit

**Added:**
- `.claude/features/melonprime-srp-performance-contract.md`
- `.claude/skills/audit-melonprime-srp-performance.ps1`

**Changed:** `.github/workflows/build-ubuntu.yml`, `build-windows.yml` (audit wiring + step split)

## PR 3: InputProjection header-only

**Added:** `MelonPrimeInputProjection.h` (header-only, `FORCE_INLINE`)

**Changed:** `MelonPrimeGameInput.cpp` — uses `InputProjection::` namespace

**Behavior:** MoveLUT / projection logic unchanged; no `.cpp` added.

## PR 4: ScreenCursorPolicy

**Added:** `MelonPrimeScreenCursorPolicy.h/.cpp`

**Changed:** `Screen.cpp` (thin delegates), `Screen.h` (friend decls), `CMakeLists.txt`

**Behavior:** Cursor clip/warp/capture policy extracted; mouse router untouched.

## PR 5: HUD Editor FormBuilder Step 1

**Added:** `MelonPrimeHudEditorFormBuilder.h/.cpp`

**Changed:** `MelonPrimeHudConfigOnScreenEdit.cpp` — delegates color/config/row helpers

**Step 1 scope:** `UpdateColorButton`, `InvalidateHudConfigCache`, `Set*IfEditing`, `AppendLabeledRow`

**Not moved:** opacity slider, line edit, color picker, sub-color rows (Steps 3–4)

## PR 6: PatchLifecycleGateway Step 1

**Added:** `MelonPrimePatchLifecycle.h/.cpp`

**Changed:** `MelonPrimeLifecycle.cpp` — `OnEmuStart` / `ResetRuntimeStateForBoot` / `OnEmuStop`

**Boundary:** DS ARM9 patch lifecycle only; Custom HUD patch state stays in lifecycle.

**Not touched:** `RunFrameHook`.

## Phase 7: HUD FormBuilder Step 2

**Changed:** `MelonPrimeHudEditorFormBuilder.h/.cpp`, `MelonPrimeHudConfigOnScreenEdit.cpp`

**Moved:** `addCheckBox`, `addComboBox`, `addSpinBox`, `addDoubleSpinBox` widget factories.

## Phase 8: ScreenCursorPolicy friend reduction

**Changed:** `Screen.h/.cpp`, `MelonPrimeScreenCursorPolicy.cpp`

**Behavior:** Policy uses narrow `ScreenPanel` accessors; friend declarations removed.

## Phase 9: PatchLifecycleGateway Step 2

**Changed:** `MelonPrimePatchLifecycle.h/.cpp`, `MelonPrimeLifecycle.cpp`

**Behavior:** `ApplyConfigReload()` patch/hook reapply via `ReapplyForConfigReload`.

## Phase 10: RuntimeConfig aim sensitivity

**Changed:** `MelonPrimeRuntimeConfig.h/.cpp`, `MelonPrime.h/.cpp`, `MelonPrimeLifecycle.cpp`, `MelonPrimeGameRomDetect.cpp`

**Behavior:** `AimConfigSnapshot` load/apply; lifecycle/ROM-detect use `ReloadAimConfigFromTable`.

## Audit Summary

| Area | Result |
|---|---|
| SRP | PASS |
| Best practice | PASS (doc synced) |
| Performance | PASS — hot path / RunFrameHook order unchanged |
| Windows CI | PASS |
| Code blockers | None identified |

## Merge Checklist

```text
[x] Windows CI success
[x] Ubuntu CI success
[x] macOS CI success
[x] BSD CI success (all 4 confirmed at HEAD 81f9fe49, run IDs above)
[x] Manual smoke (macOS pass at HEAD 81f9fe49, see "Manual Smoke" section;
    gameplay-specific and Windows/Linux platform-specific smoke still open)
[x] Progress doc current
```

## Post-Merge (after merge to highres_fonts_v3)

```text
- Merged SRP refactor v3 immediate plan via PR #520 (`c5e95f55`) into `highres_fonts_v3`
- Post-merge phases 7–10 completed on `highres_fonts_v3` (commits `c5811762`–`6f543432`)
- Windows CI passed in run `28924163273` (pre phases 7–10)
- CI for phases 7–10: pending manual run
```

## Post-Phase-10 Continuation Plan (Phases 11–16)

Tracked in the "Never Mix Rules" spirit — each phase is its own commit, no
mixing of unrelated widget kinds or subsystems in one change.

| Phase | Title | Status |
|---|---|---|
| 11 | Stabilization / docs cleanup | ✅ Done (`fe5ef70b`) |
| 12 | HUD Editor FormBuilder Step 3 (opacity slider, line edit) | ✅ Done (`31b3b993`) |
| 13 | HUD Editor FormBuilder Step 4 (color picker, sub-color, overlay row) | ✅ Done (`40a779f3`) |
| 14 | ScreenCursorPolicy `ReleaseForClose` extraction | ✅ Done (`53e85be3`) |
| 15 | PatchLifecycleGateway Step 3 (design doc only) | ✅ Done (`767f3947`) |
| 16 | RuntimeConfig cleanup follow-up (naming/comments only) | ✅ Done (`0f302c3b`) |

All Phase 11-16 continuation-plan phases complete.

## Phase 12: HUD FormBuilder Step 3

**Changed:** `MelonPrimeHudEditorFormBuilder.h/.cpp`, `MelonPrimeHudConfigOnScreenEdit.cpp`

**Moved:** `addOpacitySlider`, `addLineEdit` widget factories (as
`AddOpacitySliderRow` / `AddLineEditRow`, `WidgetFactoryContext`-based).

**Bug fix bundled with this phase:** `WidgetFactoryContext::populating` was a
`bool` value-copy, and the Step 2 factories (`AddBoolRadioRow` /
`AddComboBoxRow` / `AddSpinBoxRow` / `AddDoubleSpinBoxRow`) captured `ctx`
*by reference* in their `connect()` lambdas. `ctx` is a stack-local
constructed at each `addXxx()` call site, so those lambdas held a dangling
reference once the constructing call returned; separately, the frozen
`populating` snapshot (captured while `populateForElement()` had it `true`)
would have kept every post-populate edit blocked forever. Fixed by making
`populating` a `bool&` (aliasing `m_populating`) and capturing `ctx` by value
everywhere — copies of reference members still alias the same long-lived
objects. No config key or UI behavior change.

## Phase 13: HUD FormBuilder Step 4

**Changed:** `MelonPrimeHudEditorFormBuilder.h/.cpp`, `MelonPrimeHudConfigOnScreenEdit.cpp`

**Moved:** `addColorPicker`, `addSubColor`, `addColorOverlayRow` widget
factories (as `AddColorPickerRow` / `AddSubColorRow` / `AddColorOverlayRow`,
`WidgetFactoryContext`-based). All three route through
`MelonPrime::ColorDialogPrefs::getColor` (never `QColorDialog` directly,
verified by `audit-color-dialog-prefs.ps1`). The repeated "open picker →
apply RGB → refresh swatch → invalidate cache" body is now a single
`PickAndApplyColor` helper shared by all three, instead of being
copy-pasted three times. `MelonPrimeHudConfigOnScreenEdit.cpp` dropped its
now-unused `QHBoxLayout`/`QRadioButton`/`MelonPrimeColorDialogPrefs.h`
includes and the `kRadioOnWidth`/`kRadioOffWidth` constants.

`MelonPrimeHudConfigOnScreenEdit.cpp` (845 → 697 lines) is now a thin
delegate layer over `MelonPrimeHudEditorFormBuilder` for every widget
factory (checkbox/combo/spin/double-spin/opacity/line-edit/color/sub-color/
overlay-row); only `addSeparator`, layout/populate/snapshot logic, and the
crosshair/preview-specific code remain in this file.

## Phase 14: ScreenCursorPolicy ReleaseForClose extraction

**Changed:** `MelonPrimeScreenCursorPolicy.h/.cpp`, `Screen.cpp`

**Behavior:** `ScreenPanel::releaseCursorStateForClose()` now delegates to
`MelonPrime::ScreenCursorPolicy::ReleaseForClose(*this)`, using the same
Phase 8 narrow-accessor pattern (`setClipWantedForMelonPrime` /
`resetAimMouseDelta`). Kept distinct from `Unclip()` because `Unclip()`
early-returns on `isClosingForMelonPrime()`, and this function is called
from `beginClose()` *after* `closing` is already `true` — it must still
run. No behavior change.

## Phase 15: PatchLifecycleGateway Step 3 (design doc only)

**Added:** `.claude/features/melonprime_patch_lifecycle_gateway_step3_plan.md`

**No code changes.** Maps the five `RunFrameHook`-adjacent patch/hook call
sites still outside `MelonPrimePatchLifecycle` (match-end restore,
battle-runtime enter, per-frame OSD reapply, leave-in-game hook deactivate,
out-of-game per-frame patch), documents which are gateway candidates vs.
explicit non-goals (OSD per-frame reapply, HUD-owned restore, transient
input reset), and proposes a Site-E-then-A-then-B implementation order for
a future PR. Intentionally not implemented in this phase — RunFrameHook
ordering changes need a dedicated review per the continuation plan.

## Phase 16: RuntimeConfig cleanup follow-up

**Changed:** `MelonPrimeRuntimeConfig.h`, `MelonPrimeLifecycle.cpp`,
`MelonPrime.cpp`, `melonprime-srp-performance-contract.md`

**Comments/docs only** (no logic, clamp, formula, or ROM-detect timing
change — verified by diff before commit): documents the Load/Apply
boundary between `Load*ConfigSnapshot` (pure) and
`MelonPrimeCore::Apply*ConfigSnapshot` (side effects), and flags that the
in-game aim-sensitivity hotkey path (`RecalcAimSensitivityCache`) is a
separate pre-existing reload that bypasses `AimConfigSnapshot` — noted as
out of scope rather than "fixed". Added the missing `AimConfigSnapshot` row
to the SRP performance contract's boundary table.

## Continuation Plan Status: Complete

All 6 phases (11–16) of the post-Phase-10 continuation plan are done. Every
phase built clean on macOS, passed `audit-melonprime-srp-performance.ps1`
(plus `audit-color-dialog-prefs.ps1` / `audit-platform-scatter-budget.ps1`
where relevant), and was committed + pushed individually.

Notable finding along the way (Phase 12): fixed a dangling-reference /
frozen-`populating`-snapshot bug in the Step 2 `WidgetFactoryContext`
pattern (merged as part of Phase 7) before extending it further — see the
Phase 12 entry above.

Phase 15's design doc (`melonprime_patch_lifecycle_gateway_step3_plan.md`)
was the one item in this batch that shipped as *plan only* — implementing
PatchLifecycleGateway Step 3 (RunFrameHook patch/hook call-site extraction)
was a follow-on task at the time, per that doc's own recommended
Site-E-then-A-then-B order and verification requirements.

**Update (Phase A/B/C, 2026-07-08):** BSD CI and a first manual smoke pass are
now done — see "Full-matrix CI at HEAD 81f9fe49" and "Manual Smoke" above.
The Merge Checklist is now all-`[x]`. Still open before calling this
*fully* done: gameplay-specific smoke (aim, shoot/zoom, weapon switch,
morph ball boost, Adventure WASD, focus loss/refocus, stop/reset) beyond
the HUD-editor-focused pass done here, and Windows/Linux platform-specific
cursor-release smoke (the macOS session only exercised the `__APPLE__`
branch of `ScreenCursorPolicy::ReleaseForClose`).

## Phase D: PatchLifecycleGateway Step 3, Site E (2026-07-08)

**Changed:** `MelonPrimePatchLifecycle.h/.cpp`, `MelonPrime.cpp`

**Behavior:** Implemented the Site E candidate from
`melonprime_patch_lifecycle_gateway_step3_plan.md` — added
`PatchLifecycle::ApplyOutOfGameFrame(nds, emu, cfg, rom)`, a thin wrapper
around `Patches_Apply(PatchSite_OutOfGameFrame, ctx)`, and routed
`RunFrameHook`'s out-of-game per-frame patch call through it. Same
`PatchCtx` construction, same registry entries (FixWifi /
UseFirmwareLanguage / ExpandStageMatrix, still self-guarded), same
frame-relative call position — no `RunFrameHook` reordering, no state-flag
changes. Verified via `audit-melonprime-srp-performance.ps1` and a macOS
launch/close smoke.

**Full-matrix CI at HEAD `6f76636a95f6e465b7e8458bfec73c825c2365a2`** (pushed
to the same `ci/phase11-16-verification` branch, fast-forwarded from
`81f9fe49`):

| Platform | Run | Conclusion |
|---|---|---|
| macOS | [28931739117](https://github.com/ag-advania/melonPrimeDS/actions/runs/28931739117) | success |
| Ubuntu | [28931739178](https://github.com/ag-advania/melonPrimeDS/actions/runs/28931739178) | success |
| Windows | [28931739149](https://github.com/ag-advania/melonPrimeDS/actions/runs/28931739149) | success — all audit steps (config defaults, HUD key parity, literal budget, platform scatter, color dialog prefs, SRP/performance) + Configure + Build + golden-harness-absent check all green |
| BSD | [28931739144](https://github.com/ag-advania/melonPrimeDS/actions/runs/28931739144) | success |

All 4 platforms green at the Phase D commit. Phase A-D and the Phase 11-16
continuation plan are now both fully CI-confirmed end to end.

## Batch 1: PatchLifecycleGateway Step 3, Sites A and B (2026-07-08)

Per the accelerated follow-up plan: batch-oriented workflow (small isolated
commits + local build/audit per commit, full CI at the batch boundary,
manual smoke reserved for gameplay-sensitive batches).

### Site A — match-end restore

**Changed:** `MelonPrimePatchLifecycle.h/.cpp`, `MelonPrime.cpp` (commit `470b869e`)

Added `PatchLifecycle::RestoreOnMatchEnd(nds, emu, cfg, rom, core)`,
wrapping `Patches_RestoreOnLeave` + `ARM9Hook_SetMatchHooksActive(...
false ...)`. `StateFlags::BIT_END_OF_GAME_PATCH_RESTORED` stays set in
`RunFrameHook` per the design doc's rule (frame-state ownership, not
patch-lifecycle ownership).

### Site B — battle-runtime enter

**Changed:** `MelonPrimePatchLifecycle.h/.cpp`, `MelonPrime.cpp` (commit `a56d06ad`)

Added `PatchLifecycle::ApplyOnBattleRuntimeEnter(nds, emu, cfg, rom, core,
nativeWeaponSwitchEnabled)`, wrapping the full three-call sequence
(`Patches_Apply(PatchSite_BattleRuntime)` + `ARM9Hook_SetMatchHooksActive(...
true ...)` + conditional `WeaponSwitchHook_IsSiteValid`) rather than the
"patch/hook only" fallback — `WeaponSwitchHook_IsSiteValid` is a public
static `MelonPrimeCore` method with no new ownership coupling when called
from `PatchLifecycle.cpp`. `StateFlags::BIT_BATTLE_RUNTIME_MODE` stays set
in `HandleBattleRuntimeEnter()`, which remains a single cold outlined
function (not inlined into `RunFrameHook`, not split awkwardly across files).

### Verification (per commit)

- Clean macOS build after each commit
- `audit-melonprime-srp-performance.ps1` passed after each commit
- `git diff` review confirmed both extractions are pure call-site
  substitutions — same calls, same order, same arguments
- macOS launch/close smoke after Site B (no crash)

### Full-matrix CI at HEAD `a56d06ad...` (Batch 1 boundary)

_(run in progress at time of commit — see below for confirmed results)_

Sites C and D remain untouched, matching the design doc:
Site C (per-frame `OsdColor_ApplyOnce` re-apply) is an explicit non-goal;
Site D (leave-in-game `ARM9Hook_SetMatchHooksActive(false)`) stays inline
per the doc's "verify first" note (bundling it with Site A was flagged as
possible but not required, and this batch did not do the S6/S7 pass that
note calls for).

Still deferred (do not touch without a dedicated plan):

```text
RunFrameHook大分割 / ARM9 hook context化 / HUD render unity分割 /
MelonPrimeCore hot state struct抽出 / Screen mouse router全面化 /
PlatformInput raw ownership再設計
PatchLifecycleGateway Step 3 Site D bundling (see step3_plan.md "verify first" note)
```
