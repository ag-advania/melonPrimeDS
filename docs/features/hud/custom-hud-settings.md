# Custom HUD settings

This guide covers the complete Custom HUD surface in the MelonPrime input
settings dialog: the enable switch, the generated section editor, live
previews, the in-game layout editor, and the Custom HUD Input/Output tab.

It is a workflow and ownership document, not a duplicate of every property
row. The generated property reference is the authoritative list of keys,
types, defaults, and supported surfaces:
[MelonPrimeHudPropSchemaPhase2a.md](../../generated/hud/MelonPrimeHudPropSchemaPhase2a.md).

## What the Custom HUD changes

The Custom HUD is a host-rendered overlay that can replace selected native HUD
elements and add configurable presentation such as a crosshair, gauges,
weapon information, match status, scoreboard, enemy target, radar, and OSD
colors. It does not alter the game's underlying player, weapon, or match
state.

The main enable key is Metroid.Visual.CustomHUD and its default is false.
Individual sections and elements have separate keys. A section being
collapsed in the dialog is a UI preference; it does not disable the HUD
section's runtime content.

## On-screen edit style

The Custom HUD page also contains the editor-style selector. It is separate
from the generated Metroid.Visual.* property schema:

| Control | Key | Default | Values |
| --- | --- | --- | --- |
| On-Screen Edit Style | Metroid.UI.OnScreenEditStyle | Classic (0) | Classic (0), Retro (1) |

The value is normalized to those two enum values and is resolved when Edit
HUD Layout opens. Classic shows the selected element's property editor in the
host Qt panel. Retro draws the property panel inside the DS-space edit
overlay. The dedicated crosshair editor keeps its own interface in both
styles. If the style is changed after an edit session has started, close or
cancel that session and reopen it to establish the new style.

## User workflow

1. Open the MelonPrime input/settings dialog and select the Custom HUD page.
2. Enable Custom HUD if the overlay is not already active.
3. Expand the relevant main section and then its nested subsection.
4. Change values while using the preview as a quick visual check.
5. Choose the On-Screen Edit Style if the property-panel presentation matters.
6. Use Edit HUD Layout when exact placement or in-game appearance matters.
7. Accept/save the parent dialog to commit the dialog state.
8. Use Custom HUD Input/Output when a configuration must be shared as text.

The dialog preview is intentionally compact. It is useful for checking colors,
visibility, labels, and rough relationships, but it is not a pixel-perfect
replacement for the in-game editor.

## Main section map

The current dialog has 11 main sections. Section open/closed state is
persisted separately from the section's visual properties.

| Main section | What it controls | Preview |
| --- | --- | --- |
| DISABLE DEFAULT HUD | Selective suppression of native HUD elements | None |
| OUTLINE OVERRIDE | Global outline enablement, color, opacity, and thickness policy | None |
| HUD SCALE | Text scale and automatic scaling/cap behavior | None |
| HUD FONT | Bundled MPH font, system font, or font-file selection and style | None |
| CROSSHAIR | Main crosshair, zoom crosshair, inner lines, outer lines, dot, outline, and pixel-position behavior | Crosshair |
| HP / AMMO | HP and ammo labels, gauges, outlines, weapon icon, color ramps, and weapon inventory | HP/ammo |
| MATCH STATUS HUD | Score, rank, time, bomb count, labels, icons, and their outlines | Match status |
| HUD SCOREBOARD | Scoreboard position, dimensions, rows, colors, typography, and slot-stable hunter portraits; see [focused behaviour](custom-hud-scoreboard.md) | Scoreboard |
| HUD ENEMY TARGET | Target visibility, layout, HP mode, text, icon, and outlines | Enemy target |
| HUD RADAR | Radar position, size, opacity, frame, and outline | Radar |
| IN-GAME OSD COLOR | Global OSD color plus message/slot-specific color overrides | None |

The exact property rows under these sections are generated from the schema.
When a setting is missing from the guide, search the generated schema by
section or key rather than creating a second property inventory here.

Each main section has its own persisted disclosure key. These keys control
only whether the section is visible in the dialog:

| Main section | Disclosure key |
| --- | --- |
| DISABLE DEFAULT HUD | Metroid.UI.SectionDisableDefaultHud |
| OUTLINE OVERRIDE | Metroid.UI.SectionHudGlobalOutline |
| HUD SCALE | Metroid.UI.SectionHudTextScale |
| HUD FONT | Metroid.UI.SectionHudFont |
| CROSSHAIR | Metroid.UI.SectionHudCrosshair |
| HP / AMMO | Metroid.UI.SectionHudHpAmmo |
| MATCH STATUS HUD | Metroid.UI.SectionHudMatchStatusGrp |
| HUD SCOREBOARD | Metroid.UI.SectionHudScoreboard |
| HUD ENEMY TARGET | Metroid.UI.SectionHudEnemyTarget |
| HUD RADAR | Metroid.UI.SectionHudRadar |
| IN-GAME OSD COLOR | Metroid.UI.SectionOsdColor |

Nested subsection disclosure keys follow the same Metroid.UI.SectionHud*
contract and are wired by
src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudTables.inc. The
generated property report does not replace this UI wiring map. None of these
disclosure keys changes runtime visibility by itself; use the relevant
Metroid.Visual.* Show/enable property for that.

## Section behavior and useful distinctions

### DISABLE DEFAULT HUD

This section controls which native elements are suppressed when Custom HUD
runtime rendering is active. It is selective: enabling Custom HUD does not
imply that every native element is automatically hidden. The suppression
policy is consumed by the runtime HUD path and should be tested together with
the replacement element that is meant to occupy the same area.

### OUTLINE OVERRIDE

Outline override is a shared presentation policy. Per-element outline
settings can still exist in their own subsections, so diagnose an unexpected
border by checking both the global override and the element's outline group.
Color components are stored as separate R, G, and B values.

### HUD SCALE and HUD FONT

HUD scale changes the coordinate and text-size interpretation used by the
overlay. Automatic scaling has caps so a large host window cannot grow every
element without limit. Font selection has three conceptual modes: the bundled
MPH font, a system family, and a font file. Font style properties such as
weight, italic, underline, and strikeout are independent properties.

The settings-dialog preview may approximate text metrics. If a custom font
changes wrapping, alignment, or overlap, confirm it in the in-game editor and
in the target renderer.

### CROSSHAIR

The crosshair has a main group and nested Zoom Crosshair, Inner Lines, and
Outer Lines groups. Inner and outer arms have independent length, thickness,
opacity, offset, and linked-XY behavior. The center dot has its own visibility,
opacity, thickness, and shape policy.

The high-resolution position and output-pixel deadband controls are separate
from the ordinary crosshair geometry. They affect positional stability and
should be evaluated with a moving aim input, not only with the static dialog
preview. The focused [zoom crosshair reference](../custom-hud-zoom-crosshair.md)
covers the zoom-stage behavior.

### HP / AMMO and MATCH STATUS HUD

These groups are composites. A visible number can depend on the parent group,
the number subsection, its outline subsection, and any value-based color ramp.
Weapon and bomb icons have their own geometry and outline controls. For
troubleshooting, enable the parent first, then the child subsection, then
check the element's Show property and color/opacity.

### HUD SCOREBOARD, HUD ENEMY TARGET, and HUD RADAR

These sections render composite elements rather than a single label. Their
preview widgets show the layout relationship, while the runtime implementation
decides which live game data is available in the current mode. The focused
[Enemy Target reference](custom-hud-enemy-target.md) and the runtime reference
are the right places for mode/data limitations.

The radar is a bottom-screen-derived overlay presented with the host HUD
composition. Its position and frame settings are not the same thing as the
game's native touch-screen radar state.

### IN-GAME OSD COLOR

The global color is the fallback policy. Message and slot subsections can
override it when their own use-global-color switch is disabled. The OSD
schema owns the list of literal and slot-based rows; keep that list in the
generated schema and related OSD implementation docs.

## Dialog preview versus in-game edit mode

The settings dialog builds widgets from descriptor tables. Supported widget
families include booleans, integer and floating-point values, strings,
anchors, alignments, RGB colors, font controls, opacity controls, and
specialized enum controls.

The dialog preview widgets refresh when a HUD widget changes. They read the
current configuration and invalidate the runtime HUD cache for the preview
path. This makes them good for interactive feedback, but they intentionally
use simplified samples for text, gauges, icons, and composite elements.

The in-game editor operates in DS-space coordinates and provides the
full-fidelity overlay context. It supports element selection, dragging,
property panels, anchor editing, crosshair sub-panels, and reset/cancel/save.
The editor currently exposes 16 editable HUD elements. Their complete
descriptor list and coordinate details remain in
[Settings UI and Edit Mode](../../development/ui/settings-and-edit-mode.md).

Use the dialog for broad property editing and TOML exchange. Use the in-game
editor for placement, overlap, and renderer-visible appearance.

## Save, cancel, and reset semantics

There are three different kinds of state:

| State | During dialog editing | On cancel | On accept/save |
| --- | --- | --- | --- |
| HUD widget values | Applied to the local preview/config path | Restored from the visual snapshot | Saved through the data-driven widget loop |
| Section disclosure state | UI-only open/closed state | Follows the dialog's existing snapshot behavior | Persisted with the other disclosure keys |
| In-game edit mode values | Owned by the edit-mode state and config | Edit-mode Cancel restores its edit snapshot | Edit-mode Save writes the same config keys |

The parent dialog's cancel path is a visual snapshot rollback, not a universal
rollback for every setting in the whole MelonPrime dialog. Some non-HUD
settings have immediate global, save-data, or runtime side effects; consult
the [MelonPrime Settings coverage map](../melonprime-settings.md) before
assuming Cancel reverses them.

The Sensitivity-section reset button is not the Custom HUD reset. Custom HUD
reset is owned by the in-game editor's Reset action and restores the schema
defaults for its edit surface.

## TOML Input/Output

The Custom HUD Input/Output tab exports the current widgets under these TOML
tables:

- check_boxes for boolean controls;
- combo_boxes for ordinary combo-box indices;
- font_combo_boxes for selected font family names;
- spin_boxes for integer controls;
- double_spin_boxes for floating-point controls;
- sliders for slider values; and
- line_edits for text and font-file path values.

The exporter discovers widgets from tabCrosshair by object name. Programmatic
widget names are derived from configuration keys by replacing periods with
underscores. This means the exchange format follows the dialog's current
widget surface without a separately maintained list.

Import is deliberately tolerant of unknown keys: only a matching widget with
the expected TOML value type is changed. Malformed TOML produces an error
status and does not count as a successful import. An import changes the
dialog/preview state; use the parent Save/OK path when the result should
become the persistent configuration.

Do not hand-edit generated object names or copy a full generated schema into
a sample TOML. Generate an export from the version of the dialog that will
consume it.

## Common diagnosis

| Symptom | Check |
| --- | --- |
| The Custom HUD is invisible | Metroid.Visual.CustomHUD, the parent section Show switch, and whether the current mode has HUD data |
| A native element remains visible | DISABLE DEFAULT HUD for that element and whether the runtime suppression path is active |
| Dialog preview differs from gameplay | Font metrics, HUD scale, renderer path, in-game editor placement, and mode-specific data |
| A child control appears ineffective | Parent section disclosure is not the same as enablement; check each parent Show/enable property |
| Imported TOML appears unchanged | Table name, object-name key, value type, and whether the parent dialog was saved |
| A color picker palette was lost | [Color-dialog preferences](../custom-hud-color-dialog-prefs.md); palette slots are UI helper state, not HUD RGB values |
| Edit-mode Cancel did not affect another setting | Edit-mode snapshot covers the HUD edit surface; it is not a whole-dialog transaction |

## Ownership and source map

The implementation is split by responsibility:

| Responsibility | Owner |
| --- | --- |
| Keys, types, defaults, dialog/edit/runtime surface metadata | src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc |
| Dialog section wiring | src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudTables.inc |
| Generated dialog property rows | src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigHudDialogProps.inc |
| Dialog construction and preview refresh | src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigCustomHudBuild.inc and MelonPrimeInputConfigPreview.cpp |
| TOML exchange | src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigCustomHudCode.inc |
| In-game edit descriptors and input | MelonPrimeHudConfigOnScreenEditProps.inc and the OnScreenEdit include fragments |
| Runtime rendering and visibility | MelonPrimeHudRender*.inc and MelonPrimeHudRender.cpp |
| Shared color-dialog palette | MelonPrimeColorDialogPrefs.cpp/.h |

The engineering-level lifecycle and generated-file rules are maintained in
[settings-and-edit-mode.md](../../development/ui/settings-and-edit-mode.md).
The runtime ownership and cache boundaries are maintained in
[custom-hud-runtime.md](../../development/hud/custom-hud-runtime.md).

## Verification checklist

- Enable and disable Custom HUD, then verify native suppression and replacement
  elements independently.
- Exercise every main section and at least one nested subsection.
- Compare a dialog preview with the in-game editor for position, font,
  outline, gauge, icon, and composite elements.
- Save, reopen, and confirm widget values and section disclosure state.
- Cancel after changing visual values and confirm the visual snapshot restores
  the previous saved values.
- Export TOML, import it into a fresh dialog, and verify every widget family.
- Test malformed TOML, unknown keys, out-of-range values, and font-file paths.
- Run the generated-schema parity audits after schema changes.
- Record build-only, static, and runtime results separately.
