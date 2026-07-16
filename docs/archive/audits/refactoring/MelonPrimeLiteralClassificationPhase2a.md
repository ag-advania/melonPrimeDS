# MelonPrime Literal Classification Phase 2a

Generated/verified on 2026-07-02 during V3 Phase 2.

## Pre-change Classification

Non-canonical quoted `"Metroid.*"` occurrences under `src/frontend/qt_sdl`, excluding
`MelonPrimeHudPropSchema.inc`, `MelonPrimeOsdColorSchema.inc`, and `MelonPrimeDef.h`:

| Class | Occurrences | Action |
|---|---:|---|
| Existing HUD schema key | 365 | Replace with `MP_HUD_PROP_KEY_*` |
| Existing `CfgKey` | 62 | Replace with `MelonPrime::CfgKey::*` |
| New UI section `CfgKey` | 86 | Add `Metroid.UI.Section*` constants, then replace |
| `Metroid.UI.MenuLanguage` | 6 | Add `CfgKey::MenuLanguage`, then replace |

No `Metroid.Visual.*` key was missing from the HUD schema.

## Post-change Residual

Non-canonical residual budget is 1:

| File | Key | Reason |
|---|---|---|
| `src/frontend/qt_sdl/Config.cpp` | `Metroid.Sensitivity.Aim` | Fixed-size legacy INI migration row; the field is not a plain `const char*` slot, so keeping the literal is the lowest-risk option. |

CI ratchet budget is lowered from 519 to 1.
