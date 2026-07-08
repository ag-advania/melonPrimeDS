# Custom HUD Color Dialog Preferences

## Purpose

Persist `QColorDialog` **custom color slots** (the 16 swatches at the bottom of the
picker) across app restarts for all Custom HUD color pickers.

This is **UI helper state**, not HUD runtime configuration:

- HUD element colors continue to save as `Metroid.Visual.*ColorR/G/B` (unchanged).
- Custom palette history saves to global config as `MelonPrime.ColorDialog.CustomColors`.
- `MelonPrimeHudPropSchema.inc` is intentionally **out of scope**.

## Current Architecture

| File | Responsibility |
|---|---|
| `MelonPrimeColorDialogPrefs.h` | Public API: `getColor()` only; thin header (`QColor`, `QString`, `QWidget` forward decl) |
| `MelonPrimeColorDialogPrefs.cpp` | `QColorDialog`, persistence, macOS `DontUseNativeDialog`, validation |
| Call sites | Pick a color; write RGB keys to local/instance config; invalidate HUD cache |

Public API:

```cpp
namespace MelonPrime::ColorDialogPrefs {
    QColor getColor(QWidget* parent, const QColor& initial, const QString& title);
}
```

Internal helpers (unnamed namespace in `.cpp` only):

- `loadPersistedCustomColors()` — restore slots from global config
- `saveCurrentCustomColors()` — write slots + `Config::Save()` on color confirm
- `colorDialogOptions()` — macOS uses `QColorDialog::DontUseNativeDialog`

## Storage Spec

**Location:** `Config::GetGlobalTable()` (app-wide, not per ROM/window/instance)

**Key:** `MelonPrime.ColorDialog.CustomColors`

**Format:** TOML string array of `#RRGGBB` (no alpha)

```toml
[MelonPrime.ColorDialog]
CustomColors = ["#ff0000", "#00ff00", "#112233"]
```

**Load rules:**

- Skip load when key absent (`HasKey()` — do not create empty array on first run)
- Accept only 7-char `#RRGGBB` hex; ignore invalid entries without deleting TOML

**Save rules:**

- Save only when user confirms a color (`picked.isValid()`)
- Cancel does not call `Config::Save()`
- Slot indices 0..15 map to `QColorDialog::customColor(i)`

## Platform Notes

| Platform | Dialog | Custom slots |
|---|---|---|
| Windows | Native Qt dialog (default) | `setCustomColor` / `customColor` |
| Linux (X11/Wayland) | Native Qt dialog (default) | Same as Windows |
| macOS | **Qt non-native** (`DontUseNativeDialog`) | Native macOS picker ignores `setCustomColor` |

Platform `#ifdef` stays inside `MelonPrimeColorDialogPrefs.cpp` only. Call sites
must not branch on OS.

## Call Site Inventory

All Custom HUD color pickers route through `ColorDialogPrefs::getColor()`:

| Location | Count | Parent widget |
|---|---|---|
| `MelonPrimeHudConfigOnScreenEdit.cpp` | 3 | `this` (floating edit panel) |
| `MelonPrimeHudConfigOnScreenInput.inc` | 3 | `nullptr` (in-game overlay) |
| `InputConfig/MelonPrimeInputConfigCustomHudBuild.inc` | 1 | `this` (settings dialog) |

Includes:

- `MelonPrimeHudRender.cpp` — unity host for on-screen input fragment
- `MelonPrimeHudConfigOnScreenEdit.cpp`
- `InputConfig/MelonPrimeInputConfig.cpp`

CMake: `MelonPrimeColorDialogPrefs.cpp` in `SOURCES_QT_SDL`.

## Audit Results (2026-07-08)

Status: **implementation complete; refactor phase 2 complete**

### PASS — SRP / encapsulation

- [x] Public header exposes `getColor()` only
- [x] `<QColorDialog>` included only in `MelonPrimeColorDialogPrefs.cpp`
- [x] `loadPersistedCustomColors` / `saveCurrentCustomColors` not exported
- [x] `<QtGlobal>` explicit for `Q_OS_MAC`
- [x] Load uses `HasKey()` before `GetArray()` (no empty key on first launch)
- [x] HUD RGB keys unchanged; helper `Config::Save()` is palette-only

### PASS — grep ratchet

```bash
rg "QColorDialog::getColor" src/frontend/qt_sdl
# => MelonPrimeColorDialogPrefs.cpp only

rg "#include <QColorDialog>" src/frontend/qt_sdl
# => MelonPrimeColorDialogPrefs.cpp only

rg "loadCustomColors|saveCustomColors|loadPersistedCustomColors|saveCurrentCustomColors" src/frontend/qt_sdl
# => MelonPrimeColorDialogPrefs.cpp only (internal names)
```

### PASS — unrelated audits

- Platform scatter budget: 22/22 (localization excluded; no new `__APPLE__` in call sites)
- `MelonPrimeHudPropSchema.inc`: unchanged (correct)

### PASS — build

- macOS dev build green after phase 1 + phase 2

### Known non-issues

- On-screen editor passes `parent = nullptr` — acceptable for overlay; modal still works
- `MelonPrimeHudConfigOnScreenInput.inc` uses unqualified `ColorDialogPrefs::` inside
  `namespace MelonPrime` — equivalent to `MelonPrime::ColorDialogPrefs::`
- Re-entrant guard `s_colorDialogOpen` remains in draw/input fragments (separate concern)

## Completed Refactoring Phases

### Phase 1 — persistence + call-site migration

- Added `MelonPrimeColorDialogPrefs` helper
- Migrated Edit panel, on-screen input, settings dialog swatch
- Shared palette across all Custom HUD pickers

### Phase 2 — header/cpp cleanup

- Removed public `loadCustomColors` / `saveCustomColors`
- Moved persistence into unnamed namespace
- Forward-declared `QWidget`; dropped header dependency on `QColorDialog`
- Added `HasKey()` guard on load

## Follow-Up Refactoring (Optional)

Prioritized backlog if further cleanup is desired. None are blockers.

### P1 — Documentation drift (low effort)

- [x] Add this feature note
- [ ] Keep `settings-ui-and-edit-mode.md` color picker section aligned (references helper, not raw `QColorDialog`)

### P2 — Re-entrancy guard consolidation (medium)

`MelonPrimeHudConfigOnScreenDraw.inc` defines `static bool s_colorDialogOpen`.
`MelonPrimeHudConfigOnScreenInput.inc` sets/clears it around every dialog open.

**Idea:** add an internal RAII guard inside `getColor()` when `parent == nullptr`
(overlay path), or expose a thin `tryGetColor()` that returns invalid when already
open. Keeps overlay input code from duplicating guard logic.

**Risk:** settings dialog path uses `parent != nullptr`; guard semantics differ —
needs careful design before moving.

### P3 — CI grep ratchet (low)

Add a lightweight check to Windows/Ubuntu audit jobs (or a new skill script):

```bash
rg -n "QColorDialog::getColor" src/frontend/qt_sdl \
  | rg -v "MelonPrimeColorDialogPrefs.cpp" && exit 1 || exit 0
```

Prevents future direct `QColorDialog` usage outside the helper.

### P4 — Empty palette key cleanup (low)

After `saveCurrentCustomColors()`, if all 16 slots are invalid, the TOML key may
exist as an empty array. Optional: remove `MelonPrime.ColorDialog.CustomColors`
when nothing valid remains. Cosmetic only.

### P5 — Load caching (defer)

`loadPersistedCustomColors()` runs on every `getColor()` call. Cost is negligible
(16 strings). Cache with a static `bool loaded` only if profiling shows dialog
open latency matters.

### Explicit non-goals

- Do **not** move custom colors into `MelonPrimeHudPropSchema.inc`
- Do **not** save HUD RGB via helper `Config::Save()` lifecycle
- Do **not** force `DontUseNativeDialog` on Windows/Linux
- Do **not** add `Q_OS_*` branches to `Screen.cpp`, Edit panel, or InputConfig

## Verification Checklist

### Automated

```bash
# Encapsulation
rg "QColorDialog::getColor" src/frontend/qt_sdl
rg "#include <QColorDialog>" src/frontend/qt_sdl

# CMake
rg "MelonPrimeColorDialogPrefs.cpp" src/frontend/qt_sdl/CMakeLists.txt

# Build (platform-specific)
cmake --build build-mac --parallel          # macOS
cmake --build build/release-mingw-x86_64 --config Release  # Windows
```

### Manual

1. Open Custom HUD settings swatch → add custom color → confirm → quit app
2. Confirm `melonDS.toml` contains `[MelonPrime.ColorDialog] CustomColors = [...]`
3. Restart → open on-screen editor color picker → same custom slots visible
4. Cancel color dialog → confirm TOML not updated spuriously
5. Insert invalid TOML values → app ignores bad entries, loads valid `#RRGGBB`

### macOS-specific

- Qt (non-native) color dialog shows custom color row
- Persisted slots survive restart

## Related Commits

- `e5b7420f` — initial persistence + call-site migration
- `e81e6545` — header/cpp refactor (`getColor()` only, `HasKey` load guard)

## Related Docs

- [settings-ui-and-edit-mode.md](../skills/settings-ui-and-edit-mode.md) — Custom HUD settings / edit mode UI
- [custom-hud-runtime.md](../skills/custom-hud-runtime.md) — HUD runtime ownership (RGB config, not palette)
- [custom-hud-zoom-crosshair.md](custom-hud-zoom-crosshair.md) — crosshair color keys in HUD schema
