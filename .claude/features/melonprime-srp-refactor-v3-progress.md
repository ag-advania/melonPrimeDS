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
| Windows | ✅ PASS | run `28924163273` / job `85807860745` |
| Ubuntu | ✅ PASS | user confirmed (mac/linux also green) |
| macOS | ✅ PASS | user confirmed |
| BSD | ⏳ Confirm before merge | FreeBSD / NetBSD / OpenBSD + artifacts |

### Windows CI (run `28924163273`)

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
- [ ] BSD CI (FreeBSD / NetBSD / OpenBSD)
- [ ] Manual smoke: MPH boot, aim, shoot/zoom, weapon switch, morph boost, Adventure WASD, focus loss/refocus, stop/reset, Custom HUD editor, color picker custom colors

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
[ ] BSD CI success
[ ] Manual smoke (see PR Summary test plan)
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
| 15 | PatchLifecycleGateway Step 3 (design doc only) | Pending |
| 16 | RuntimeConfig cleanup follow-up (naming/comments only) | Pending |

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

Next up: **Phase 15 — PatchLifecycleGateway Step 3 design doc**.

Still deferred (do not touch without a dedicated plan):

```text
RunFrameHook大分割 / ARM9 hook context化 / HUD render unity分割 /
MelonPrimeCore hot state struct抽出 / Screen mouse router全面化 /
PlatformInput raw ownership再設計
```
