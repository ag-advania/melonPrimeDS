# MelonPrimeDS — Context for Claude Code Sessions

## Project Overview
MelonPrimeDS is a fork of melonDS tailored for **Metroid Prime Hunters DS** with mouse/keyboard-focused FPS controls and game-specific emulator/UI patches.

Primary target: **Windows**. The repo has also been used with **MinGW cross-compilation from Ubuntu WSL**.

---

## Key Custom Features Implemented

### 1. Bottom Screen Radar Overlay (`MELONPRIME_CUSTOM_HUD`)
Displays a circular crop of the DS bottom screen on top of the rendered top screen.

**Two render paths:**
- **OpenGL path** (`Screen.cpp` + `main_shaders.h`): samples the bottom screen from the texture array and clips it to a circular overlay.
- **Software path** (`MelonPrimeCustomHud.cpp`): `DrawBottomScreenOverlay()` draws a cropped `QImage` region with `QPainter` and a circular clip path.

**Current source region behavior:**
- X center is fixed at `128`
- Y center is **hunter-specific** via `kBtmOverlaySrcCenterY[]` in `MelonPrimeConstants.h`
- Radius is configurable via `Metroid.Visual.BtmOverlaySrcRadius` (default `46`)

**Config keys** (`Instance*.Metroid.Visual.*`):
| Key | Default | Description |
|-----|---------|-------------|
| `BtmOverlayEnable` | false | Enable overlay |
| `BtmOverlayAnchor` | 2 (TR) | 9-point anchor for overlay destination |
| `BtmOverlayDstX` | 190 | X offset from anchor |
| `BtmOverlayDstY` | 0 | Y offset from anchor |
| `BtmOverlayDstSize` | 64 | Output size of the circular overlay |
| `BtmOverlayOpacity` | 0.85 | Opacity (`0.0-1.0`) |
| `BtmOverlaySrcRadius` | 46 | Source radius on the DS bottom screen |

### 2. Custom HUD System
Drawn with `QPainter` over the top screen buffer. The current HUD system includes:
- **Crosshair** with inner/outer arms, outline, center dot, T-style, opacity, thickness, and XY-length controls
- **HP HUD** with text, optional auto-color, and optional gauge
- **Weapon / Ammo HUD** with text, optional weapon icon, optional icon tint overlay, and optional ammo gauge
- **Match Status HUD** with per-mode labels and separate overall/label/value/separator/goal colors
- **Rank / Time HUD** for multiplayer ranking and timer display
- **Bomb Left HUD** for alt-form bomb count, with optional text and bomb icon
- **Radar overlay** described above

Implementation details worth knowing:
- `MelonPrimeCustomHud.cpp` caches HUD config and several rendered assets to avoid repeated per-frame work.
- Weapon icons and bomb icons are cached and optionally tint-overlaid.
- The custom HUD also applies a **No HUD patch** to suppress parts of the game's native HUD while custom elements are active.

### 3. In-game Aspect Ratio Patch
`MelonPrimePatch.cpp` patches the game's projection/scaling setup so gameplay FOV better matches the emulator aspect ratio.

**Config keys:**
- `Metroid.Visual.InGameAspectRatio`
- `Metroid.Visual.InGameAspectRatioMode`

**Modes:**
- `0 = Auto`
- `1 = 5:3`
- `2 = 16:10`
- `3 = 16:9`
- `4 = 21:9`

`Auto` reads the current top-screen aspect ratio from window config and only applies a patch when needed.

### 4. MelonPrime Internal Helpers
`MelonPrimeInternal.h` now contains shared low-level helpers used across the custom gameplay code:
- `BitScanFwd()` / `BitScanRev()` wrappers
- strict-aliasing-safe RAM accessors `Read8/16/32()` and `Write8/16/32()`
- shared constants such as `PLAYER_ADDR_INC`, `AIM_ADDR_INC`, `RAM_MASK`, and touch UI coordinates

### 5. In-game Logic Structure
`MelonPrimeInGame.cpp` contains the current hot/cold-split gameplay update path:
- `MelonPrimeCore::HandleInGameLogic()` is the hot per-frame entry point
- rare actions are outlined into cold helpers such as morph, weapon-check, and adventure UI handlers
- movement/buttons, morph-ball boost, and mouse/stylus aim are handled separately for better hot-path locality

---

## MelonPrimeCustomHud Details

### Source files and responsibilities
| File | Responsibility |
|------|----------------|
| `src/frontend/qt_sdl/MelonPrimeCustomHud.h` | public API surface |
| `src/frontend/qt_sdl/MelonPrimeCustomHud.cpp` | runtime HUD rendering, cache management, no-HUD patching, match-state caching |
| `src/frontend/qt_sdl/MelonPrimeConstants.h` | hunter-specific radar source Y positions and related constants |
| `src/frontend/qt_sdl/Screen.cpp` | calls `CustomHud_Render()` and OpenGL radar overlay path |

### Runtime entry points
`CustomHud_Render()` is the main per-frame entry point.

Current high-level flow inside `CustomHud_Render()`:
1. Return immediately if not in-game.
2. If custom HUD is disabled, restore the native HUD patch state and exit.
3. Ensure icon caches are loaded.
4. Apply the no-HUD patch.
5. Refresh cached HUD config when invalidated.
6. Read current gameplay values from RAM.
7. Hide HUD entirely for certain gameplay states.
8. Draw HP, bomb-left, match-status, rank/time.
9. If first-person, additionally draw weapon/ammo, crosshair, and radar overlay.

### HUD hide rules
`CustomHud_ShouldHideForGameplayState()` currently hides the HUD when:
- START is pressed
- player HP is zero
- game-over flag is active

`CustomHud_ShouldDrawRadarOverlay()` is currently just the inverse of that shared hide check.

### Match-state cache
`CustomHud_OnMatchJoin()` is called from `MelonPrime.cpp` on match join. It caches multiplayer state that would otherwise be repeatedly decoded every frame:
- battle mode
- goal value
- time-limit minutes
- team-play flag
- team-assignment nibble
- whether the mode is time-based

This cached state is later used by:
- `DrawMatchStatusHud()`
- `DrawRankAndTime()`

### Patch and cache lifecycle
Important lifecycle helpers:
- `CustomHud_ResetPatchState()` resets no-HUD patch tracking, HUD config cache, and battle-state cache. Call on emu stop/reset.
- `CustomHud_InvalidateConfigCache()` marks the HUD config cache dirty. It is called after settings are saved and also from preview/apply paths.

Current cached data inside `MelonPrimeCustomHud.cpp` includes:
- weapon icon images
- bomb icon images
- tinted icon variants
- outline image buffer for crosshair rendering
- text measurement / text bitmap caches
- `CachedHudConfig` (contains `HpHudConfig`, `WeaponHudConfig`, `CrosshairHudConfig`, `MatchStatusHudConfig`, `BombLeftHudConfig`, `RankTimeHudConfig`, `RadarOverlayConfig`)
- `BattleMatchState`

### CachedHudConfig struct notes
Each sub-struct stores both **raw anchor + offset** values and **computed final coordinates**. The raw values are loaded once per config change in `Load*Config()`. Final positions are recomputed by `RecomputeAnchorPositions(topStretchX)` whenever config changes **or** `topStretchX` changes (window resize). This ensures anchored elements track the actual visible screen edges in widescreen/narrow views.

Key struct fields:
- `HpHudConfig`: `hpAnchor`, `hpOfsX/Y` (raw), `hpX/Y` (final); `hpGaugePosAnchor`, `hpGaugePosOfsX/Y`, `hpGaugePosX/Y`
- `WeaponHudConfig`: `wpnAnchor`, `wpnOfsX/Y`, `wpnX/Y`; `iconPosAnchor`, `iconPosOfsX/Y`, `iconPosX/Y`; `ammoGaugePosAnchor`, `ammoGaugePosOfsX/Y`, `ammoGaugePosX/Y`
- `MatchStatusHudConfig`: `matchStatusAnchor`, `matchStatusOfsX/Y`, `matchStatusX/Y`
- `BombLeftHudConfig`: `bombLeftAnchor`, `bombLeftOfsX/Y`, `bombLeftX/Y`; `bombIconPosAnchor`, `bombIconPosOfsX/Y`, `bombIconPosX/Y`
- `RankTimeHudConfig`: `rankAnchor/OfsX/Y/X/Y`, `timeLeftAnchor/OfsX/Y/X/Y`, `timeLimitAnchor/OfsX/Y/X/Y`
- `RadarOverlayConfig`: `radarAnchor`, `radarOfsX/Y`, `radarDstX/Y`
- `CachedHudConfig`: `lastStretchX` tracks the `topStretchX` used for the last position computation

### High-resolution HUD rendering
The HUD overlay is rendered into a hi-res buffer matching the actual screen output size (not DS-native 256×192).

Key design points:
- `Overlay[0]` is sized `ceil(hudScale * topStretchX * 256) × ceil(hudScale * 192)` pixels.
- `CustomHud_Render()` receives `hudScale` (`scaleY = output height / 192`) and `topStretchX` (`scaleX / scaleY`: `>1` widescreen, `<1` narrow window, `=1` exact 4:3).
- The painter uses `scale(hudScale, hudScale)` then `translate((topStretchX-1)*128, 0)`. DS Y is always scaled by `scaleY`; DS X effectively scales by `scaleX` via the translate.
- All HUD element positions are specified in DS-space (integers, 0–255 / 0–191) and scale correctly with the window without any manual correction.
- Icons are drawn with `drawImage(QRectF(x, y, icon.width(), icon.height()), icon)` so they scale with the painter transform.
- The font is always rendered at `kCustomHudFontSize = 6` px (optimal glyph quality for `mph.ttf`). Visual text size is controlled separately by `Metroid.Visual.HudTextScale` (percentage, default 100). Text bitmaps are drawn scaled via `DrawCachedText(..., tds)` where `tds = textScalePct / 100.0f`.
- The crosshair reads `cx/cy` from RAM in DS-space (0–255) and requires **no** manual `topStretchX` correction — the painter transform handles it.

### 9-point anchor system
All HUD text/icon positions use a 9-point anchor + offset model (implemented in the `highres_fonts` branch):

```
0=Top Left    1=Top Center    2=Top Right
3=Middle Left 4=Middle Center 5=Middle Right
6=Bottom Left 7=Bottom Center 8=Bottom Right
```

- Anchor base coordinates (DS canvas 256×192):
  - X: col 0 → 0, col 1 → 128, col 2 → 256
  - Y: row 0 → 0, row 1 → 96, row 2 → 192
- The stored `*X`/`*Y` config values are **offsets** from the anchor base, not absolute positions.
- `ApplyAnchor(anchor, ofsX, ofsY, outX, outY)` in `MelonPrimeCustomHud.cpp` computes final DS-space coordinates at config-load time (inside `Load*Config()`). Draw functions receive pre-computed positions.
- Every HUD element has a `*Anchor` config key (default values in `Config.cpp`). Default anchors:
  - HP: 6 (BL), Weapon/Ammo: 8 (BR), WeaponIconPos: 8 (BR)
  - HpGaugePos: 6 (BL), AmmoGaugePos: 8 (BR)
  - MatchStatus: 0 (TL), Rank: 0 (TL), TimeLeft: 0 (TL), TimeLimit: 0 (TL)
  - BombLeft: 8 (BR), BombLeftIconPos: 8 (BR)
  - Radar: 2 (TR)
- Preview functions (`updateHpAmmoPreview`, `updateMatchStatusPreview`, `updateRadarPreview`) each contain a local `applyAnchorPreview` / `applyAnchor2` lambda mirroring the same logic.

### Runtime HUD behavior details
Useful implementation notes for future edits:
- HP and ammo gauges support both anchored positioning relative to text (via `HudHpGaugeAnchor`) and fully independent absolute positioning (mode 1, via `HudHpGaugePosAnchor` + offset).
- Match-status colors use an “invalid `QColor` means inherit overall color” convention in cached config.
- Bomb-left text and icon are separate toggles.
- Bomb-left is only drawn for Samus/Sylux alt-form cases, but the settings UI always previews it.
- Time-left is read from raw frame-based RAM time and converted to seconds.
- Time-limit is derived from battle settings / cached match-join state and formatted as `M:00`.

### No-HUD patch
The runtime custom HUD disables pieces of the original game HUD by writing ARM NOPs to ROM-version-specific addresses.

Relevant details:
- patch table lives in `MelonPrimeCustomHud.cpp`
- keyed by `romGroupIndex`
- guarded by `s_hudPatchApplied`
- restored automatically when custom HUD is no longer active

### Radar overlay note
The software radar overlay path takes `hunterID` and uses `kBtmOverlaySrcCenterY[hunterID]` to pick the crop center. That means the source crop is intentionally not identical for all hunters.

---

## Settings UI Architecture (`MelonPrimeInputConfig`)

### Files
| File | Purpose |
|------|---------|
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui` | Qt Designer UI XML |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h` | class declaration, slots, setup helpers, hotkey tables |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | constructor/setup logic, section wiring, most widget initialization |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp` | save logic and reset-to-default handlers |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigPreview.cpp` | live preview rendering, preview apply flow, snapshot/restore |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigInternal.h` | shared UI helpers, presets, color-sync helpers |

### Tab Structure
| Tab name | Object name | Content |
|----------|-------------|---------|
| Controls | `tabAddonsMetroid` | Hotkey mappings page 1 |
| Controls 2 | `tabAddonsMetroid2` | Hotkey mappings page 2 |
| Settings | `tabMetroid` | Sensitivity, toggles, hunter license, volume, video, screen sync, in-game aspect ratio, and related settings |
| Custom HUD | `tabCrosshair` | Enable checkbox + Edit HUD Layout button (all crosshair/HP/ammo/etc. settings migrated to in-game edit mode) |

### Constructor/setup flow
Current constructor flow in `MelonPrimeInputConfig.cpp` is:
1. `setupKeyBindings()`
2. `setupSensitivityAndToggles()`
3. `setupCollapsibleSections()`
4. `setupPreviewConnections()`
5. `setupCustomHudCode()`
6. `snapshotVisualConfig()`

This ordering matters because preview wiring assumes widgets are already initialized.

### Collapsible Sections
The settings UI uses toggle buttons and section widgets wired up in `setupCollapsibleSections()`. Toggle state is persisted under `Metroid.UI.Section*` config keys.

Current persisted sections include groups such as:
- input settings / screen sync / cursor clip / in-game aspect ratio
- sensitivity / gameplay / video / volume / license

(Crosshair / HP-ammo / match-status / rank-time / bomb-left / radar sections have been removed from the dialog — they are now configured exclusively via in-game edit mode.)

### Color Picker Pattern
In the in-game edit mode, color pickers use `QColorDialog` triggered by clicking the Color swatch in the element properties panel or the crosshair panel.

### Preview system
The settings dialog's preview widgets (`widgetCrosshairPreview`, `widgetHpAmmoPreview`, `widgetMatchStatusPreview`, `widgetRadarPreview`) have been removed. All preview functionality is now live in the in-game edit mode overlay.

### Preview apply / cancel model
The dialog keeps a visual snapshot through `snapshotVisualConfig()` and restores it with `restoreVisualSnapshot()` when the parent dialog is cancelled.

The snapshot now only covers 3 fields:
- `cCustomHud` — the Custom HUD enable bool
- `cAspectRatio` — the in-game aspect ratio enable bool
- `cAspectRatioMode` — the aspect ratio mode combo index

`InputConfigDialog.cpp` calls `saveConfig()` on accept and `restoreVisualSnapshot()` on cancel.

### Save behavior
`saveConfig()` in `MelonPrimeInputConfigConfig.cpp` is the main commit point for UI state.

It currently saves:
- both hotkey pages (`Keyboard` and `Joystick` subtables)
- gameplay / sensitivity / license / volume / sync settings
- in-game aspect ratio settings
- `Metroid.Visual.CustomHUD` enable bool
- all collapsible-section open/closed states

Important side effects in `saveConfig()`:
- if `Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame` changed, windows schedule `panel->updateClipIfNeeded()` via `QTimer::singleShot`
- `MelonPrime::CustomHud_InvalidateConfigCache()` is called at the end so runtime HUD reads updated values on the next frame

Note: all crosshair, HP/ammo, match-status, rank/time, bomb-left, radar, and text-scale settings are saved directly from the in-game edit mode (via `CustomHud_ExitEditMode(true, cfg)` → `Config::Save()`), NOT from the settings dialog.

### Reset/default handlers
Default-reset entry points have been removed from the settings dialog. Resetting to defaults now happens through the in-game edit mode's Reset button (`ResetEditToDefaults()` in `MelonPrimeCustomHud.cpp`).

### Hotkeys
Defined in `EmuInstance.h` and grouped in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`:
- `kMetroidHotkeys` — first controls page (`20` entries)
- `kMetroidHotkeys2` — second controls page (`11` entries)

These replaced the older `hk_tabAddonsMetroid*` naming.

### 9-point anchor widgets in the UI
The 9-point anchor combos have been removed from the settings dialog. All HUD element positioning (anchor + offset) is now done exclusively through the in-game edit mode's properties panels, which include an embedded 3×3 anchor grid picker.

---

## In-game HUD Edit Mode

### Overview
All HUD element positioning, crosshair configuration, and text scaling are now configured exclusively through an in-game edit mode overlay (entered via `CustomHud_EnterEditMode()`, triggered by the "Edit HUD Layout" button in the settings dialog).

### Source location
All edit-mode code lives in `MelonPrimeCustomHud.cpp`, within the `namespace MelonPrime` block.

### Public API
```cpp
void CustomHud_EnterEditMode(EmuInstance* emu, Config::Table& cfg);
void CustomHud_ExitEditMode(bool save, Config::Table& cfg);
bool CustomHud_IsEditMode();
void CustomHud_EditMousePress(QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
void CustomHud_EditMouseMove(QPointF pt, Config::Table& cfg);
void CustomHud_EditMouseRelease(QPointF pt, Qt::MouseButton btn, Config::Table& cfg);
void CustomHud_EditMouseWheel(QPointF pt, int delta, Config::Table& cfg);
void CustomHud_SetSelectionCallback(std::function<void(int)> cb);
```

### Layout (DS-space coordinates)
The edit overlay uses DS-space (0–255 × 0–191). Key layout:

**Fixed button bar (top):**
- Save/Cancel/Reset buttons: `y=1, h=12` → bottom at `y=13`
- Text Scale / Crosshair buttons: `y=15, h=10` → bottom at `y=25`

**Crosshair panel system (below button bar):**
- Main panel: `x=2, y=28, w=82` (10 rows fixed: color + 7 main props + Inner header + Outer header)
- Side panel for Inner/Outer: `x=84, y=28, w=82` (opens to the right when header is clicked, mutually exclusive)
- Crosshair preview: `x=166, y=28, 64×64` (live preview using `CollectArmRects` at scale 2)

**Element properties panel:**
- Positioned relative to selected element via `ComputePropsPanelRect()` (right or left of element, clamped `py >= 26.0f` to avoid overlapping button bar)
- Contains Show toggle, Color picker, 3×3 Anchor grid, and element-specific properties
- All property labels use full, unabbreviated names (e.g., "Opacity" not "Opac", "Suffix" not "Sfx")

### Layout constants
```cpp
kPropRowH    = 7.0f    // height of each property row
kPropLabelW  = 40.0f   // label column width (wide for full names)
kPropCtrlW   = 38.0f   // control column width
kPropPanelW  = 82.0f   // total panel width
kCrosshairSidePanelX = kCrosshairPanelX + kPropPanelW + 2.0f  // = 84
kCrosshairPreviewX   = kCrosshairSidePanelX + kPropPanelW + 2.0f // = 168
kCrosshairPreviewSize = 64
```

### 12 editable HUD elements
Defined in `kEditElems[kEditElemCount]` array:
| Index | Name | Anchor default | Notes |
|-------|------|----------------|-------|
| 0 | HP | 6 (BL) | Text element |
| 1 | HP Gauge | 6 (BL) | Gauge element with orientation/length/width |
| 2 | Weapon/Ammo | 8 (BR) | Text element |
| 3 | Weapon Icon | 8 (BR) | Icon element (24×24, cached icon preview) |
| 4 | Ammo Gauge | 8 (BR) | Gauge element |
| 5 | Match Status | 0 (TL) | Text element |
| 6 | Rank | 0 (TL) | Text element |
| 7 | Time Left | 0 (TL) | Text element |
| 8 | Time Limit | 0 (TL) | Text element |
| 9 | Bomb Left | 8 (BR) | Text element |
| 10 | Bomb Icon | 8 (BR) | Icon element (16×16, cached icon preview) |
| 11 | Radar | 2 (TR) | Radar element (circle preview) |

### Element box live previews
Instead of text labels, element boxes render live previews:
- **Gauge elements**: filled bar at 50% with the gauge color
- **Icon elements**: cached weapon/bomb icon image drawn into the box
- **Radar**: circle outline
- **Text elements**: sample text in the element's color (e.g., "100" for HP, "PWR 50" for weapon/ammo)

Element box text uses `elemFont` which scales with `HudTextScale` (`pixelSize = max(3, 4*tds)`), so text shrinks proportionally when boxes get smaller.

### Edit-mode config snapshot
On entering edit mode, `SnapshotEditConfig()` snapshots all relevant config keys. `RestoreEditSnapshot()` restores them on cancel. `ResetEditToDefaults()` resets to factory defaults.

### Crosshair panel details
The crosshair panel has a fixed layout (no scrolling needed):
- Row 0: Color swatch (clickable → `QColorDialog`)
- Rows 1–7: Main props (Outline, Outline Opacity, Outline Thick., Center Dot, Dot Opacity, Dot Thick., T-Style)
- Row 8: Inner header (click toggles side panel, closes Outer)
- Row 9: Outer header (click toggles side panel, closes Inner)

Side panel (Inner or Outer) shows 7 property rows: Show, Opacity, Length X, Length Y, Link XY, Thickness, Offset.

### Crosshair preview
The preview box at `(kCrosshairPreviewX, 28, 64, 64)` reads all crosshair config values directly from `cfg` and renders inner/outer arms + center dot using `CollectArmRects` at preview scale `PVS=2`. The preview is clipped to the box and updates live as settings change.

---

## MelonPrimeCustomHud.h — Public API

All functions are inside `namespace MelonPrime` and guarded by `#ifdef MELONPRIME_CUSTOM_HUD`.

```cpp
void CustomHud_Render(
    EmuInstance* emu,
    Config::Table& localCfg,
    const RomAddresses& rom,
    const GameAddressesHot& addrHot,
    uint8_t playerPosition,
    QPainter* topPaint,
    QPainter* btmPaint,
    QImage* topBuffer,
    QImage* btmBuffer,
    bool isInGame,
    float topStretchX = 1.0f,
    float hudScale = 1.0f
);

bool CustomHud_IsEnabled(Config::Table& localCfg);
bool CustomHud_ShouldHideForGameplayState(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);
bool CustomHud_ShouldDrawRadarOverlay(EmuInstance* emu, const RomAddresses& rom, uint8_t playerPosition);
void CustomHud_ResetPatchState();
void CustomHud_InvalidateConfigCache();
void CustomHud_OnMatchJoin(uint8_t* ram, const RomAddresses& rom);
void DrawBottomScreenOverlay(Config::Table& localCfg, QPainter* topPaint, QImage* btmBuffer, uint8_t hunterID);
```

Notes:
- `DrawBottomScreenOverlay()` takes `hunterID` so the source crop can use hunter-specific radar centers.
- `CustomHud_Render()` handles more than crosshair/HP/ammo now: it also covers match status, rank/time, bomb-left HUD, and radar overlay.

---

## Shader Files
`src/frontend/qt_sdl/main_shaders.h`:
- `kScreenVS` / `kScreenFS` — standard screen renderer
- `kBtmOverlayVS` / `kBtmOverlayFS` — radar overlay shader path used when `MELONPRIME_CUSTOM_HUD` is enabled

`Screen.h` / `Screen.cpp` also hold the OpenGL-side overlay shader program, uniforms, and draw setup.

---

## Config System
Default values live in `src/frontend/qt_sdl/Config.cpp`.

MelonPrime-specific settings are primarily stored under:
- `Instance*.Metroid.*` for per-instance values
- `Metroid.UI.Section*` for persisted UI section state

Important current groups include:
- `Metroid.Visual.CustomHUD`
- `Metroid.Visual.HudTextScale` — text visual scale in percent (default 100, font always 6px)
- `Metroid.Visual.Crosshair*`
- `Metroid.Visual.HudHp*` — includes `HudHpAnchor` (default 6=BL), `HudHpX/Y` (offsets), `HudHpGaugePosAnchor` (default 6=BL)
- `Metroid.Visual.HudWeapon*` — includes `HudWeaponAnchor` (default 8=BR), `HudWeaponIconPosAnchor` (default 8=BR)
- `Metroid.Visual.HudAmmo*` — includes `HudAmmoGaugePosAnchor` (default 8=BR)
- `Metroid.Visual.HudMatchStatus*` — includes `HudMatchStatusAnchor` (default 0=TL)
- `Metroid.Visual.HudRank*` — includes `HudRankAnchor` (default 0=TL)
- `Metroid.Visual.HudTimeLeft*` — includes `HudTimeLeftAnchor` (default 0=TL)
- `Metroid.Visual.HudTimeLimit*` — includes `HudTimeLimitAnchor` (default 0=TL)
- `Metroid.Visual.HudBombLeft*` — includes `HudBombLeftAnchor` (default 8=BR), `HudBombLeftIconPosAnchor` (default 8=BR)
- `Metroid.Visual.BtmOverlay*` — includes `BtmOverlayAnchor` (default 2=TR)
- `Metroid.Visual.InGameAspectRatio*`

When touching UI or runtime HUD behavior, always check both:
- `Config.cpp` for defaults
- `MelonPrimeInputConfigConfig.cpp` for save/reset coverage

---

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- `MelonPrimeCustomHud.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Active branch: `highres_fonts`
Current work is on the `highres_fonts` branch. Main changes relative to `master`:
- Full **9-point anchor system** for all HUD element positions (described above)
- All `*X`/`*Y` HUD config values are now **offsets from anchor**, not absolute DS-space coordinates
- `ApplyAnchor()` helper in `MelonPrimeCustomHud.cpp` — called once per element in `Load*Config()`, transparent to draw functions
- **In-game HUD edit mode** — full drag-and-drop editor with properties panels, crosshair panel with side panels, live previews (described in "In-game HUD Edit Mode" section above)
- **Settings dialog simplified** — Custom HUD tab stripped to Enable checkbox + Edit button; all HUD element configuration moved to in-game edit mode
- Snapshot/restore in `snapshotVisualConfig` / `restoreVisualSnapshot` simplified to only 3 fields: `cCustomHud`, `cAspectRatio`, `cAspectRatioMode`
- Property labels use full unabbreviated names throughout the edit mode UI
- Element boxes show live previews (gauge bars, cached icons, sample text) instead of static text labels
- Element box font scales with `HudTextScale` setting
