# MelonPrime SRP Refactor v3 — Progress

Branch: `melonprime-srp-refactor-v3`

Base: `highres_fonts_v3`

## PR Status

| PR | Title | Status | Commit |
|---|---|---|---|
| 1 | RuntimeConfigSnapshot | ✅ Done | (pending push) |
| 2 | SRP/Performance Audit | ⏳ Pending | |
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
