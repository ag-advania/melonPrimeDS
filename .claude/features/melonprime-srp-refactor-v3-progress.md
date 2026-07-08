# MelonPrime SRP Refactor v3 — Progress

Branch: `melonprime-srp-refactor-v3`

Base: `highres_fonts_v3`

## PR Status

| PR | Title | Status | Commit |
|---|---|---|---|
| 1 | RuntimeConfigSnapshot | ✅ Pushed | `87ccca2d` |
| 2 | SRP/Performance Audit | ✅ Pushed | `6b0ffdfd` |
| 3 | InputProjection header-only | ✅ Pushed | `babc9445` |
| 4 | ScreenCursorPolicy | ✅ Pushed | `21085bab` |
| 5 | HUD Editor FormBuilder Step 1 | ✅ Pushed | `f8c4cc53` |
| 6 | PatchLifecycleGateway Step 1 | ✅ Pushed | `87a99f4b` |

## PR 1: RuntimeConfigSnapshot

**Added:** `MelonPrimeRuntimeConfig.h/.cpp`

**Changed:** `MelonPrimeLifecycle.cpp`, `MelonPrime.h`, `CMakeLists.txt`

**Behavior:** Config read/clamp separated; apply order unchanged.

## PR 2: SRP/Performance Audit

**Added:**
- `.claude/features/melonprime-srp-performance-contract.md`
- `.claude/skills/audit-melonprime-srp-performance.ps1`

**Changed:** `.github/workflows/build-ubuntu.yml`, `build-windows.yml` (CI wiring)

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

**Not moved:** widget factories (`addCheckBox`, etc.)

## PR 6: PatchLifecycleGateway Step 1

**Added:** `MelonPrimePatchLifecycle.h/.cpp`

**Changed:** `MelonPrimeLifecycle.cpp` — `OnEmuStart` / `ResetRuntimeStateForBoot` / `OnEmuStop`

**Boundary:** DS ARM9 patch lifecycle only; Custom HUD patch state stays in lifecycle.

**Not touched:** `RunFrameHook`, `ApplyConfigReload` patch paths.

## Verification

- macOS Release build: green (`build-mac`)
- Hot path order: unchanged by design
- SRP audit script: ready for Ubuntu/Windows CI once workflow push lands
