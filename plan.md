# Plan: Full migration of Custom HUD settings to Edit Mode + Cleanup

## Goal
1. Migrate ALL remaining per-element Custom HUD settings into the in-game edit mode overlay
2. Remove all migrated widgets from the `.ui` file and code (dead code cleanup)
3. Add small legends/labels to the edit mode UI (e.g., on the color swatch, toggles)
4. Custom HUD tab keeps ONLY: Enable checkbox, Font Scale, Crosshair section, Edit HUD Layout button

## Architecture: Properties Panel in Edit Mode

When an element is selected in edit mode, a **properties panel** appears on the right side of the screen (or below the anchor picker). It shows only the properties relevant to that element.

### Property types needed:
- **Bool toggle** (ON/OFF button): auto-color, text show, color overlay, show ordinal
- **Int stepper** (+/- buttons with value): align (0-2), label pos, offsets, anchor points, icon mode, src radius
- **Float stepper** (+/- with value): opacity
- **String display** (read-only in overlay, editable via popup): prefix, suffix, label strings
- **Color sub-picker**: match status sub-colors (4 sub-colors)

### Element property definitions

A new struct `HudEditPropDesc` describes each property:

```cpp
enum class EditPropType { Bool, Int, Float, String, SubColor, Color };
struct HudEditPropDesc {
    const char* label;         // tiny label, e.g. "Auto", "Align", "Pfx"
    EditPropType type;
    const char* cfgKey;        // config key
    int minVal, maxVal;        // for Int/Float steppers
    int step;                  // 0 = default (1 for Int, 5 for Float)
    const char* extra1;        // SubColor/Color: R key
    const char* extra2;        // G key
    const char* extra3;        // B key
};
```

Each `kEditElems[i]` points to a property list via `HudEditElemDesc::props` / `propCount`.

### Properties panel layout

When selected, the props panel floats next to the selected element (positioned by `ComputePropsPanelRect()`):
- Each property is ONE row: `[label] [control]`
- Row height: `kPropRowH = 8.0f` DS-space
- Panel width: `kPropPanelW = 86.0f`
- Label column: `kPropLabelW = 52.0f` (full unabbreviated names)
- Control column: `kPropCtrlW = 30.0f`
- Bool: `[label] [ON/OFF]` — clickable toggle
- Int: `[label] [◀ val ▶]` — click arrows or scroll wheel
- Float: same as Int, value×100, step=5
- String: `[label] [text...]` — click opens QInputDialog
- SubColor: `[label] [■ swatch]` — click opens QColorDialog; checkbox for "use overall color"
- Color: `[label] [■ swatch]` — click opens QColorDialog directly

### Snapshot/Restore

`SnapshotEditConfig()` / `RestoreEditSnapshot()` capture all config keys for all 12 elements' props. `ResetEditToDefaults()` resets all props to factory defaults.

### Preview mode interactivity

In preview mode (`s_editPreviewMode = true`) the overlay renders the live HUD but remains interactive:
- **Left-click drag** — moves the element under the cursor
- **Right-click** — selects element under cursor and opens properties panel; empty space deselects
- Properties panel and crosshair panel work identically in both modes
- Orientation toggle and resize handles are normal-mode-only

## Implementation Steps

### ✅ Step 1: Extend data model
- `HudEditPropDesc` struct and `EditPropType` enum defined in `MelonPrimeHudConfigScreen.cpp`
- Prop arrays defined for all 12 elements (`kPropsHp`, `kPropsHpGauge`, etc.)
- `props` / `propCount` added to `HudEditElemDesc`
- `kEditElems[]` updated with prop pointers

### ✅ Step 2: Extend snapshot/restore/reset
- `SnapshotEditConfig` / `RestoreEditSnapshot` iterate all props by type
- `ResetEditToDefaults` resets all props

### ✅ Step 3: Draw properties panel
- `DrawElemPropsPanel()` helper in `MelonPrimeHudConfigScreen.cpp`
- Called from both normal mode and preview mode paths in `DrawEditOverlay()`
- Panels scroll when row count exceeds `kPropMaxVisible`

### ✅ Step 4: Handle property clicks
- In `CustomHud_EditMousePress`: prop row hit-testing for Bool/Int/Float/String/SubColor/Color
- Mouse wheel (`CustomHud_EditMouseWheel`) scrolls props panel and adjusts Int/Float values

### ✅ Step 5a: Code split
- Edit mode code separated into `MelonPrimeHudConfigScreen.cpp` (unity-build included by `MelonPrimeHudRender.cpp`)
- `MelonPrimeHudRender.cpp` handles only rendering + cache management

### ⬜ Step 5b: Hide/remove remaining settings panel widgets
- Extend `hideEditModeWidgets()` to hide ALL remaining per-element widgets
- Keep: Enable Custom HUD, Font Scale, entire Crosshair section, Edit HUD Layout button
- Hide entire collapsible sections: HP & AMMO, WEAPON ICON, HP GAUGE, AMMO GAUGE, MATCH STATUS, RANK, TIME LEFT, TIME LIMIT, BOMB LEFT, HUD RADAR
- Also hide preview widgets that become useless: `widgetHpAmmoPreview`, `widgetMatchStatusPreview`, `widgetRadarPreview` (keep crosshair preview)
- Remove corresponding `saveConfig()` lines, `applyAndPreview*()` lines, setup code, signal connections
- Remove section toggles for removed sections from `setupCollapsibleSections()`

### ⬜ Step 6: Cleanup .ui file (optional later)
- Remove dead widget XML from `.ui`. Can be done in a follow-up since hiding them works.

## File changes summary

| File | Role |
|------|------|
| `MelonPrimeHudRender.cpp` | Runtime HUD rendering, cache management, no-HUD patching |
| `MelonPrimeHudConfigScreen.cpp` | All edit mode code (unity-build included, NOT in CMakeLists) |
| `MelonPrimeHudRender.h` | Public API surface |
| `MelonPrimeInputConfig.cpp` | Step 5b: extend `hideEditModeWidgets()`, remove setup for migrated widgets |
| `MelonPrimeInputConfigConfig.cpp` | Step 5b: remove `saveConfig()` lines for migrated widgets |
| `MelonPrimeInputConfigPreview.cpp` | Step 5b: remove `applyAndPreview*()` for migrated widgets |
| `MelonPrimeInputConfig.ui` | Step 6: remove dead widget XML |
