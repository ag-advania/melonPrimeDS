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
enum class EditPropType { Bool, Int, Float, String, SubColor };
struct HudEditPropDesc {
    const char* label;         // tiny label, e.g. "Auto", "Align", "Pfx"
    EditPropType type;
    const char* cfgKey;        // config key
    int minVal, maxVal;        // for Int/Float steppers
    // For SubColor: cfgKey = overall bool key, extra keys for R/G/B
    const char* extraKey1;     // R key (SubColor) or string for labels
    const char* extraKey2;     // G key
    const char* extraKey3;     // B key
};
```

Each `kEditElems[i]` will point to a property list:
```cpp
struct HudEditElemDesc {
    // ... existing fields ...
    const HudEditPropDesc* props;  // nullable, array of properties
    int propCount;
};
```

### Properties panel layout

When selected, the props panel renders below the anchor picker + toggle/swatch row:
- Each property is ONE row: `[label] [control]`
- Row height: ~8px DS-space (tiny text)
- Panel width: ~60px
- Bool: `[label] [ON/OFF]` — clickable toggle
- Int: `[label] [◀ val ▶]` — click left/right arrows to decrement/increment
- Float: same as Int but step = 0.05
- String: `[label] [text...]` — shows truncated text, click opens QInputDialog
- SubColor: `[label] [■ swatch]` — click opens QColorDialog (like existing color picker), plus a small "OVR" toggle for "use overall color"

### Legends in the overlay

Add small labels (using `smallFont`, 4px) next to existing controls:
- Next to the **ON/OFF toggle**: small "Show" label above/next to it
- Next to the **color swatch**: small "Color" label above/next to it
- Next to the **orientation toggle**: small "Orient" label above/next to it
- Next to the **resize handles**: small "Size" text label

On the Save/Cancel/Reset buttons: already have text labels.

### Snapshot/Restore

The snapshot system already captures all existing edit-mode keys. For new migrated properties:
- Extend `SnapshotEditConfig` and `RestoreEditSnapshot` to also capture all `props[]` keys
- Bool props: use `s_editSnapshotBools` tracking
- Float props: need `s_editSnapshotDoubles` map (new, since current snapshot is int-only)
- String props: need `s_editSnapshotStrings` map (new)

### Reset

`ResetEditToDefaults()` already resets position/anchor/show/color from defaults. Extend it to also reset all props.

## Implementation Steps

### Step 1: Extend data model
- Add `HudEditPropDesc` struct and `EditPropType` enum
- Define prop arrays for each of the 12 elements
- Add `props` and `propCount` to `HudEditElemDesc`
- Update `kEditElems[]` with prop pointers

### Step 2: Extend snapshot/restore/reset
- Add `s_editSnapshotDoubles` (map<string, double>) and `s_editSnapshotStrings` (map<string, string>)
- In `SnapshotEditConfig`: iterate props[], capture each by type
- In `RestoreEditSnapshot`: restore props by type
- In `ResetEditToDefaults`: reset props from default config

### Step 3: Draw properties panel
- In `DrawEditOverlay`, after the anchor picker / toggle / swatch section:
  - Draw a small dark panel for the selected element's properties
  - For each prop, draw label + control in one row
  - Add legends (tiny text labels) next to existing controls (color swatch, toggle, orient)

### Step 4: Handle property clicks
- In `CustomHud_EditMousePress`:
  - After existing Priority 3 (visibility/color):
  - Check if click hits a prop row
  - Bool: toggle the value
  - Int/Float: check if click is on left arrow (decrement) or right arrow (increment)
  - String: open QInputDialog::getText()
  - SubColor: open QColorDialog, or toggle OVR

### Step 5: Hide/remove remaining settings panel widgets
- Extend `hideEditModeWidgets()` to also hide ALL remaining per-element widgets
- This includes: text prefix/suffix fields, align combos, auto-color checkboxes, icon mode/offset widgets, gauge offset/anchor widgets, match status sub-colors + label strings, radar opacity/srcRadius, bomb text show, rank prefix/suffix/ordinal
- Keep: Enable Custom HUD, Font Scale, entire Crosshair section, Edit HUD Layout button
- Hide entire collapsible sections that become empty: HP & AMMO, WEAPON ICON, HP GAUGE, AMMO GAUGE, MATCH STATUS, RANK, TIME LEFT, TIME LIMIT, BOMB LEFT, HUD RADAR
- Also hide preview widgets that become useless: widgetHpAmmoPreview, widgetMatchStatusPreview, widgetRadarPreview (keep crosshair preview)
- Remove all corresponding `saveConfig()` lines, `applyAndPreview*()` lines, setup code, signal connections
- Remove section toggles for removed sections from `setupCollapsibleSections()`

### Step 6: Cleanup .ui file (optional later)
- Remove dead widget XML from .ui. Can be done in a follow-up since hiding them works.

## File changes summary

| File | Changes |
|------|---------|
| `MelonPrimeCustomHud.cpp` | New PropDesc struct/data, extended snapshot/restore/reset, new DrawPropsPanel(), new prop click handling, labels on existing controls |
| `MelonPrimeCustomHud.h` | No API changes needed (internal) |
| `MelonPrimeInputConfig.cpp` | Extended hideEditModeWidgets(), remove setup code for all migrated widgets, remove empty section toggles |
| `MelonPrimeInputConfigConfig.cpp` | Remove remaining saveConfig() lines for migrated widgets, remove reset handlers for migrated widgets |
| `MelonPrimeInputConfigPreview.cpp` | Remove remaining applyAndPreview*() lines for migrated widgets, simplify snapshot/restore |
