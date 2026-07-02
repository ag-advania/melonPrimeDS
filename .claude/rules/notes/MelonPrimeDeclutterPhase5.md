# MelonPrime Declutter Phase 5

Date: 2026-07-02
Plan: V3 Phase 5

Phase 5 is intentionally conservative. It is a decluttering pass, not a
behavioral refactor.

## File Size Snapshot

| File | Lines | Decision |
|---|---:|---|
| `src/frontend/qt_sdl/MelonPrime.cpp` | 984 | Keep as one TU. Added section banners for config/platform setup, lifecycle resets, and the per-frame hook. No `.inc` split until it exceeds the 1,000-line soft threshold with a clear ownership boundary. |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | 1,160 | Keep. Still under the V2 1,200-line target. Remaining direct config reads are migration, developer-only gates, enum transforms, invalidate-coupled keys, preview helpers, or non-Metroid renderer preset keys. No binding-table move was safe in this pass. |
| `src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc` | 1,082 | Keep. Existing banners already separate font ownership, config loading, anchor recompute, and cached config refresh. More splitting would increase unity-fragment ownership cost. |
| `src/frontend/qt_sdl/MelonPrimeHudRenderRuntime.inc` | 1,022 | Keep. Existing banners separate battle HUD cache, dirty rect/runtime state, bomb HUD, and rank/time HUD. No behavior-neutral split target was obvious. |
| `src/frontend/qt_sdl/MelonPrimeLocalization.cpp` | 1,337 | Keep. Table format remains unchanged. Duplicate scan below is informational only. |

## InputConfig Binding Sweep

Remaining non-binding `Get*` / `Set*` paths were reviewed. They are intentionally
outside `m_settingBindings` because at least one of these applies:

- value is a migration or compatibility read (`InstantAimFollow`)
- public/developer build mode changes the saved value
- checkbox UI maps to an enum rather than a direct bool
- save is coupled to old/new invalidation
- key belongs to renderer presets or preview-only temporary config
- widget is dynamic custom-HUD UI handled by the HUD schema path

## Localization Duplicate Sweep

Simple first-field duplicate scan:

| Table | Rows | Duplicate first fields |
|---|---:|---:|
| `kTranslations` | 675 | 5 |
| `kObjectTextTranslations` | 22 | 0 |

Duplicated `kTranslations` fields: `Horizontal`, `Vertical`, `Mode`, `Text`,
`Normal`. These are short UI labels used in multiple contexts, and `Normal`
already maps to two Japanese strings (`通常` and `標準`). Do not mechanically
deduplicate without a context-aware localization pass.
