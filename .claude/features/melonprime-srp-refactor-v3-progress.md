# MelonPrime SRP Refactor v3 — Progress

Branch: `melonprime-srp-refactor-v3`

Base: `highres_fonts_v3`

## PR Status

| PR | Title | Status | Commit |
|---|---|---|---|
| 1 | RuntimeConfigSnapshot | ✅ Pushed | `87ccca2d` |
| 2 | SRP/Performance Audit | ✅ Local (push blocked*) | `6b0ffdfd` |
| 3 | InputProjection header-only | ✅ Local | (see git log) |
| 4 | ScreenCursorPolicy | ✅ Local | (see git log) |
| 5 | HUD Editor FormBuilder Step 1 | ✅ Local | (see git log) |
| 6 | PatchLifecycleGateway Step 1 | ✅ Local | (see git log) |

\* PR2 includes `.github/workflows/*` edits. Cursor OAuth cannot push workflow files
without `workflow` scope. Push locally from your terminal, then wire CI:

```bash
git push origin melonprime-srp-refactor-v3
```

Pending CI wiring (apply manually if not in pushed commit):

```yaml
# build-ubuntu.yml + build-windows.yml audit step
./.claude/skills/audit-melonprime-srp-performance.ps1
```

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
