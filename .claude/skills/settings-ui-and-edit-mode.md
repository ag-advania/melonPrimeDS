# Settings UI and Edit Mode

## Settings UI Architecture (`MelonPrimeInputConfig`)

### HUD settings entry points
When adding or modifying a HUD setting, touch all three:

| File | Role | Radar example |
|------|------|---------------|
| `InputConfig/MelonPrimeInputConfig.cpp` | Classic settings dialog - descriptor arrays (`kSecRadar[]`) | `P_BOOL` / `P_INT` / `P_CLR` entries |
| `MelonPrimeHudConfigOnScreenEdit.cpp` | In-game edit side panel - `populate*()` methods | `populateRadar()` with `addCheckBox` / `addSpinBox` / `addColorPicker` |
| `MelonPrimeHudConfigOnScreenDefs.inc` | In-game HUD config screen - element/property definitions | Element definitions for `Radar`, `kPropsRadar[]` |
| `MelonPrimeHudConfigOnScreenSnapshot.inc` | In-game HUD config screen - snapshot/restore/reset coverage | Reset defaults for radar keys |
| `MelonPrimeHudConfigOnScreenDraw.inc` | In-game HUD config screen - element bounds and on-screen drawing | Radar edit rectangle and preview drawing |
| `MelonPrimeHudConfigOnScreenInput.inc` | In-game HUD config screen - mouse/wheel editing behavior | Radar selection, drag, resize, and property clicks |

### Files

| File | Purpose |
|------|---------|
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui` | Qt Designer UI XML |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h` | class declaration, slots, setup helpers, hotkey tables |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | constructor/setup logic, section wiring, most widget initialization |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp` | save logic and reset-to-default handlers |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigPreview.cpp` | live preview rendering, preview apply flow, snapshot/restore |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigInternal.h` | shared UI helpers, presets, color-sync helpers |

### Tab structure

| Tab name | Object name | Content |
|----------|-------------|---------|
| Controls | `tabAddonsMetroid` | Hotkey mappings page 1 |
| Controls 2 | `tabAddonsMetroid2` | Hotkey mappings page 2 |
| Settings | `tabMetroid` | Sensitivity, toggles, hunter license, volume, video, screen sync, in-game aspect ratio, and related settings |
| Custom HUD | `tabCrosshair` | Enable checkbox + Edit HUD Layout button + 5 hierarchical main sections with nested sub-sections and preview widgets synchronized with in-game edit mode |
| Custom HUD Input/Output | `tabCustomHudCode` | TOML export/import of all Custom HUD settings (auto-discovers widgets in `tabCrosshair`) |

### Constructor/setup flow
Current constructor flow in `MelonPrimeInputConfig.cpp` is:
1. `setupKeyBindings()`
2. `setupSensitivityAndToggles()`
3. `setupCollapsibleSections()`
4. `setupCustomHudWidgets()` - programmatic creation of all HUD parameter widgets
5. `setupPreviewConnections()`
6. `setupCustomHudCode()`
7. `snapshotVisualConfig()`

This ordering matters because preview wiring assumes widgets are already initialized.

### Collapsible sections
The settings UI uses toggle buttons and section widgets wired up in `setupCollapsibleSections()`. Toggle state is persisted under `Metroid.UI.Section*` config keys.

Current persisted sections include groups such as:
- input settings / screen sync / cursor clip / in-game aspect ratio
- sensitivity / gameplay / video / volume / license

### Programmatic HUD widget sections
All HUD parameters are accessible via hierarchical collapsible sections in the Custom HUD tab, created programmatically by `setupCustomHudWidgets()` using descriptor arrays (`HudMainSec` / `HudSubSec` / `HudWidgetProp`).

Supported widget types:
- `Bool` (`QCheckBox`)
- `Int` (`QSpinBox`)
- `Float` (`QDoubleSpinBox 0.0-1.0`)
- `String` (`QLineEdit`)
- `Anchor9` (`QComboBox` 9-point)
- `Align3` (`QComboBox` L/C/R)
- `Color3` (3 x `QSpinBox` R/G/B + swatch `QPushButton`)

Main sections:
- `HUD SCALE` - auto-scale enable, text scale, global + per-category auto-scale caps (no preview)
- `CROSSHAIR` - color/outline/dot/T-style properties + sub: Inner Lines, Outer Lines (`CrosshairPreviewWidget`)
- `HP / AMMO` - sub: HP Number Position, Ammo Number Position, Weapon Icon, HP Gauge, Ammo Gauge (`HpAmmoPreviewWidget`)
- `MATCH STATUS HUD` - sub: Score, Rank/Time (nested: Rank, Time Left, Time Limit), Bomb Left, Bomb Icon (`MatchStatusPreviewWidget`)
- `HUD RADAR` - all radar settings (`RadarPreviewWidget`)

Each main section except text scale displays a preview widget on the right side of its expanded section. Preview widgets read directly from config via `EmuInstance`. Previews automatically refresh whenever widget values change via `invalidateHudAndRefreshPreviews()`.

State containers:
- `m_hudWidgets`: config key -> `QWidget*`
- `m_hudPreviews`: preview widget list for bulk refresh
- `m_hudToggles`: toggle buttons

This enables data-driven save/restore/snapshot/TOML-export without manual per-widget code.

Widget `objectName`s are derived from config keys (`.` -> `_`), ensuring TOML auto-discovery (`findChildren<T*>()` in `MelonPrimeInputConfigCustomHudCode.inc`) works automatically.

### Color picker pattern
In the in-game edit mode, color pickers use `QColorDialog` triggered by clicking the Color swatch in the element properties panel or the crosshair panel.

### Preview system
The settings dialog's Custom HUD tab features live preview widgets for each major HUD section except text scale. Preview widgets (`CrosshairPreviewWidget`, `HpAmmoPreviewWidget`, `MatchStatusPreviewWidget`, `RadarPreviewWidget`) read live from config and refresh whenever widget values change. All previews are tracked in `m_hudPreviews` for efficient bulk refresh.

Preview refresh is triggered by two paths:
1. Individual widget signals: each HUD parameter widget's signal handler calls `invalidateHudAndRefreshPreviews()`, which invalidates the runtime HUD config cache and calls `update()` on all preview widgets.
2. Preview apply: `applyVisualPreview()` in `MelonPrimeInputConfigPreview.cpp` writes all widget values to config, invalidates cache, and refreshes previews.

The in-game edit mode overlay provides full-fidelity live preview with drag-and-drop positioning. The settings dialog previews are simplified summaries such as `HP 100` and a gauge bar.

### Preview apply / cancel model
The dialog keeps a visual snapshot through `snapshotVisualConfig()` and restores it with `restoreVisualSnapshot()` when the parent dialog is cancelled.

The snapshot covers:
- `cCustomHud` - the Custom HUD enable bool
- `cAspectRatio` - the in-game aspect ratio enable bool
- `cAspectRatioMode` - the aspect ratio mode combo index
- All programmatic HUD widget values, stored by config key in `m_visualSnapshot`

`InputConfigDialog.cpp` calls `saveConfig()` on accept and `restoreVisualSnapshot()` on cancel.

### Save behavior
`saveConfig()` in `MelonPrimeInputConfigConfig.cpp` is the main commit point for UI state.

It currently saves:
- both hotkey pages (`Keyboard` and `Joystick` subtables)
- gameplay / sensitivity / license / volume / sync settings
- in-game aspect ratio settings
- `Metroid.Visual.CustomHUD` enable bool
- all programmatic HUD widget values via the data-driven loop over `m_hudWidgets`
- all collapsible-section open/closed states (both UI-defined and programmatic HUD sections)

Important side effects in `saveConfig()`:
- if `Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame` changed, windows schedule `panel->updateClipIfNeeded()` via `QTimer::singleShot`
- if `Metroid.Visual.InGameTopScreenOnly` changed, windows schedule `onScreenLayoutChanged()` via Qt queued connection
- `MelonPrime::CustomHud_InvalidateConfigCache()` is called at the end so runtime HUD reads updated values on the next frame

HUD settings are saved from both the settings dialog (`saveConfig()`) and the in-game edit mode (`CustomHud_ExitEditMode(true, cfg)` -> `Config::Save()`). Both write the same config keys. When the dialog reopens, `setupCustomHudWidgets()` reads the latest config values, ensuring sync.

### Reset/default handlers
Default reset from the settings dialog is not implemented. Resetting to defaults happens through the in-game edit mode Reset button (`ResetEditToDefaults()` in `MelonPrimeHudConfigOnScreen.cpp`).

### 9-point anchor widgets
In the settings dialog, 9-point anchors are `QComboBox` widgets with 9 items (Top Left through Bottom Right), created programmatically. In the in-game edit mode, anchors use an embedded `3x3` grid picker.

### Hotkeys
Defined in `EmuInstance.h` and grouped in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`:
- `kMetroidHotkeys` - first controls page (`20` entries)
- `kMetroidHotkeys2` - second controls page (`11` entries)

These replaced the older `hk_tabAddonsMetroid*` naming.

## In-game HUD Edit Mode

### Overview
HUD element positioning, crosshair configuration, and text scaling can also be configured through a visual in-game edit mode overlay (entered via `CustomHud_EnterEditMode()`, triggered by the `Edit HUD Layout` button in the settings dialog). This is the modern editor; the settings dialog provides the classic form-based editor. Both write the same config keys.

### Source location
The in-game edit mode is a unity-include module rooted at `MelonPrimeHudConfigOnScreen.cpp` (included by `MelonPrimeHudRender.cpp`), within the `namespace MelonPrime` block.

File split:

| File | Purpose |
|------|---------|
| `MelonPrimeHudConfigOnScreen.cpp` | Unity entry point plus shared edit-mode state, layout constants, theme colors, and coordinate conversion |
| `MelonPrimeHudConfigOnScreenDefs.inc` | Element/property descriptor types, enum labels, `kProps*` arrays, `kEditElems`, sample preview text |
| `MelonPrimeHudConfigOnScreenSnapshot.inc` | `SnapshotEditConfig()`, `RestoreEditSnapshot()`, `ResetEditToDefaults()` |
| `MelonPrimeHudConfigOnScreenDraw.inc` | `ComputeEditBounds()`, properties panel drawing, live HUD preview, edit overlay drawing |
| `MelonPrimeHudConfigOnScreenInput.inc` | Public edit-mode API, hit testing, mouse press/move/release/wheel handlers |

Do not add these `.inc` files to `CMakeLists.txt`; they are included only through `MelonPrimeHudConfigOnScreen.cpp`.

### Public API

```cpp
void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg);
void CustomHud_ExitEditMode(bool save, Config::Table& cfg);
bool CustomHud_IsEditMode();
void CustomHud_EditMousePress(QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
void CustomHud_EditMouseMove(QPointF pt, Config::Table& cfg);
void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg);
void CustomHud_SetEditSelectionCallback(std::function<void(int)> cb);
int  CustomHud_GetSelectedElement();
```

### Layout (DS-space coordinates)
The edit overlay uses DS-space (`0-255 x 0-191`). Key layout:

Fixed button bar (top):
- Save/Cancel/Reset buttons: `y=1, h=12` -> bottom at `y=13`
- Text Scale / Crosshair buttons: `y=15, h=10` -> bottom at `y=25`

Crosshair panel system (below button bar):
- Main panel: `x=2, y=28, w=82` (10 rows fixed: color + 7 main props + Inner header + Outer header)
- Side panel for Inner/Outer: `x=84, y=28, w=82` (opens to the right when header is clicked, mutually exclusive)
- Crosshair preview: `x=166, y=28, 64x64` (live preview using `CollectArmRects` at scale 2)

Element properties panel:
- Positioned relative to selected element via `ComputePropsPanelRect()` (right or left of element, clamped `py >= 26.0f` to avoid overlapping the button bar)
- Contains Show toggle, Color picker, `3x3` Anchor grid, and element-specific properties
- All property labels use full, unabbreviated names such as `Opacity` and `Suffix`

### Layout constants

```cpp
kPropRowH    = 8.0f    // height of each property row
kPropLabelW  = 52.0f   // label column width (wide for full names)
kPropCtrlW   = 30.0f   // control column width
kPropPanelW  = 86.0f   // total panel width
kCrosshairSidePanelX = kCrosshairPanelX + kPropPanelW + 2.0f  // = 88
kCrosshairPreviewX   = kCrosshairSidePanelX + kPropPanelW + 2.0f // = 176
kCrosshairPreviewSize = 64
```

### 12 editable HUD elements
Defined in `kEditElems[kEditElemCount]`:

| Index | Name | Anchor default | Notes |
|-------|------|----------------|-------|
| 0 | HP | 6 (BL) | Text element |
| 1 | HP Gauge | 6 (BL) | Gauge element with orientation/length/width |
| 2 | Weapon/Ammo | 8 (BR) | Text element |
| 3 | Weapon Icon | 8 (BR) | Icon element (`24x24`, cached icon preview) |
| 4 | Ammo Gauge | 8 (BR) | Gauge element |
| 5 | Match Status | 0 (TL) | Text element |
| 6 | Rank | 0 (TL) | Text element |
| 7 | Time Left | 0 (TL) | Text element |
| 8 | Time Limit | 0 (TL) | Text element |
| 9 | Bomb Left | 8 (BR) | Text element |
| 10 | Bomb Icon | 8 (BR) | Icon element (`16x16`, cached icon preview) |
| 11 | Radar | 2 (TR) | Radar element (circle preview) |

### Element box live previews
Instead of text labels, element boxes render live previews:
- Gauge elements: filled bar at 50% with the gauge color
- Icon elements: cached weapon/bomb icon image drawn into the box
- Radar: circle outline
- Text elements: sample text in the element's color such as `100` for HP and `PWR 50` for weapon/ammo

Element box text uses `elemFont`, which scales with `HudTextScale` (`pixelSize = max(3, 4*tds)`), so text shrinks proportionally when boxes get smaller.

### Preview mode interactivity
In preview mode (`s_editPreviewMode = true`) the overlay renders the live HUD instead of element boxes, but remains interactive:
- Left-click drag moves the element under the cursor
- Right-click selects the element under the cursor and opens its properties panel; right-clicking empty space deselects
- The floating properties panel and crosshair panel work identically in both modes
- Orientation toggle and resize handles are normal-mode-only

### Edit-mode config snapshot
On entering edit mode, `SnapshotEditConfig()` snapshots all relevant config keys. `RestoreEditSnapshot()` restores them on cancel. `ResetEditToDefaults()` resets to factory defaults.

### Crosshair panel details
The crosshair panel has a fixed layout with no scrolling:
- Row 0: Color swatch (clickable -> `QColorDialog`)
- Rows 1-7: Main props (Outline, Outline Opacity, Outline Thick., Center Dot, Dot Opacity, Dot Thick., T-Style)
- Row 8: Inner header (click toggles side panel, closes Outer)
- Row 9: Outer header (click toggles side panel, closes Inner)

Side panel (Inner or Outer) shows 7 property rows: Show, Opacity, Length X, Length Y, Link XY, Thickness, Offset.

### Crosshair preview
The preview box at `(kCrosshairPreviewX, 28, 64, 64)` reads all crosshair config values directly from `cfg` and renders inner/outer arms + center dot using `CollectArmRects` at preview scale `PVS=2`. The preview is clipped to the box and updates live as settings change.
