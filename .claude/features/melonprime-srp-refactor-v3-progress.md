# MelonPrime SRP Refactor v3 — Progress

Branch: `melonprime-srp-refactor-v3`

Base: `highres_fonts_v3`

## PR Status

| PR | Title | Status | Commit |
|---|---|---|---|
| 1 | RuntimeConfigSnapshot | ✅ Done | 87ccca2d |
| 2 | SRP/Performance Audit | ✅ Done | (pending push) |
| 3 | InputProjection header-only | ⏳ Pending | |
| 4 | ScreenCursorPolicy | ⏳ Pending | |
| 5 | HUD Editor FormBuilder Step 1 | ⏳ Pending | |
| 6 | PatchLifecycleGateway Step 1 | ⏳ Pending | |

## PR 1: RuntimeConfigSnapshot

**Goal:** Separate config read/clamp/feature gate from `ReloadConfigFlags()`.

**Added:**
- `src/frontend/qt_sdl/MelonPrimeRuntimeConfig.h`
- `src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp`

**Changed:**
- `MelonPrimeLifecycle.cpp` — `LoadRuntimeConfigSnapshot` + `ApplyRuntimeConfigSnapshot`
- `MelonPrime.h` — private `ApplyRuntimeConfigSnapshot`
- `CMakeLists.txt`

**Behavior:** No intentional behavior change; apply order preserved.

## PR 2: SRP/Performance Audit

**Goal:** Detect SRP boundary violations and Screen.cpp patch/hook dependency regressions.

**Added:**
- `.claude/features/melonprime-srp-performance-contract.md`
- `.claude/skills/audit-melonprime-srp-performance.ps1`

**Changed:**
- `.github/workflows/build-ubuntu.yml` — audit job
- `.github/workflows/build-windows.yml` — audit step

**Notes:** `IsPlatformRawAimActive` in Screen.cpp emits manual-review warnings only (not fail).
