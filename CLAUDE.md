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
- **Software path** (`MelonPrimeHudRender.cpp`): `DrawBottomScreenOverlay()` draws a cropped `QImage` region with `QPainter` and a circular clip path. A radar frame SVG (`res/assets/radar/Rader.svg`) is drawn behind the crop with an independently configurable color (`BtmOverlayRadarColor*`), and the HUD outline is applied to it.

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
| `BtmOverlayRadarColorR` | 185 | Radar frame SVG tint color red (independent) |
| `BtmOverlayRadarColorG` | 0 | Radar frame SVG tint color green |
| `BtmOverlayRadarColorB` | 5 | Radar frame SVG tint color blue |
| `BtmOverlayRadarColorUseHunter` | false | Use current hunter's color instead of manual frame color |
| `BtmOverlayFrameOutlineEnable` | true | Enable/disable SVG frame outline behind radar |

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
- `MelonPrimeHudRender.cpp` caches HUD config and several rendered assets to avoid repeated per-frame work.
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

`MelonPrimeConstants.h` defines:
- `HunterId` enum (Samus=0 through Weavel=6)
- `kBtmOverlaySrcCenterY[]` — per-hunter radar source Y positions
- `kHunterFrameColor[]` — per-hunter radar frame colors (RGB packed as `0xRRGGBB`)

### 5. In-game Logic Structure
`MelonPrimeInGame.cpp` contains the current hot/cold-split gameplay update path:
- `MelonPrimeCore::HandleInGameLogic()` is the hot per-frame entry point
- rare actions are outlined into cold helpers such as morph, weapon-check, and adventure UI handlers
- movement/buttons, morph-ball boost, and mouse/stylus aim are handled separately for better hot-path locality

---

## MelonPrimeHudRender Details

### Source files and responsibilities
| File | Responsibility |
|------|----------------|
| `src/frontend/qt_sdl/MelonPrimeHudRender.h` | public API surface |
| `src/frontend/qt_sdl/MelonPrimeHudRender.cpp` | runtime HUD rendering, cache management, no-HUD patching, match-state caching |
| `src/frontend/qt_sdl/MelonPrimeHudConfigScreen.cpp` | in-game HUD layout editor (unity-build included by `MelonPrimeHudRender.cpp`, **not** in CMakeLists) |
| `src/frontend/qt_sdl/MelonPrimeHudEditSidePanel.cpp` | in-game HUD element properties side panel — `populate*()` functions define per-element settings (unity-build included by `MelonPrimeHudConfigScreen.cpp`) |
| `src/frontend/qt_sdl/MelonPrimeHudEditSidePanel.h` | side panel class declaration |
| `src/frontend/qt_sdl/MelonPrimeConstants.h` | hunter-specific radar source Y positions and related constants |
| `src/frontend/qt_sdl/Screen.cpp` | calls `CustomHud_Render()` and OpenGL radar overlay path |

### Runtime entry points
`CustomHud_Render()` is the main per-frame entry point.

Current high-level flow inside `CustomHud_Render()`:
1. If edit mode, draw element overlay and return immediately.
2. Return immediately if not in-game.
3. If custom HUD is disabled, restore the native HUD patch state and exit.
4. Apply the no-HUD patch.
5. Refresh cached HUD config when invalidated (or recompute anchors if `topStretchX` changed).
6. Read current gameplay values from RAM.
7. Hide HUD entirely for certain gameplay states.
8. Set up painter (scale + translate + font); P-9 caches `QFontMetrics` on first call.
9. Draw HP, bomb-left, match-status, rank/time.
10. If first-person, additionally draw weapon/ammo, crosshair, and radar overlay.

Icon caches are loaded lazily inside `DrawWeaponAmmo()` / `DrawBombLeft()` via `EnsureIconsLoaded()` / `EnsureBombIconsLoaded()`, not as a separate top-level step.

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

Current cached data inside `MelonPrimeHudRender.cpp` includes:
- weapon icon images
- bomb icon images
- tinted icon variants
- outline image buffer for crosshair rendering
- text measurement / text bitmap caches
- `CachedHudConfig` (contains `HpHudConfig`, `WeaponHudConfig`, `CrosshairHudConfig`, `MatchStatusHudConfig`, `BombLeftHudConfig`, `RankTimeHudConfig`, `RadarOverlayConfig`, `HudOutlineConfig`)
- `BattleMatchState`
- P-9: `s_frameFm` / `s_frameFpx` — frame-level `QFontMetrics` cache (constructed once on first call, shared by all draw sub-functions via statics; avoids 5+ `p->fontMetrics()` copies per frame)
- P-11: `CrosshairHudConfig::chInnerColor/chOuterColor/chDotColor` — pre-computed arm/dot colors with alpha set at config load time (avoids per-frame `QColor` copy + `setAlphaF`)
- Radar frame SVG (`s_radarFrame` / `s_radarFrameTinted` / `s_radarFrameOutline`) — loaded once via `loadSvgToHeight()`, tinted and outline-colored images cached separately; re-tinted on color change

### CachedHudConfig struct notes
Each sub-struct stores both **raw anchor + offset** values and **computed final coordinates**. The raw values are loaded once per config change in `Load*Config()`. Final positions are recomputed by `RecomputeAnchorPositions(topStretchX)` whenever config changes **or** `topStretchX` changes (window resize). This ensures anchored elements track the actual visible screen edges in widescreen/narrow views.

Key struct fields:
- `HpHudConfig`: `hpAnchor`, `hpOfsX/Y` (raw), `hpX/Y` (final); `hpGaugePosAnchor`, `hpGaugePosOfsX/Y`, `hpGaugePosX/Y`
- `WeaponHudConfig`: `wpnAnchor`, `wpnOfsX/Y`, `wpnX/Y`; `iconPosAnchor`, `iconPosOfsX/Y`, `iconPosX/Y`; `ammoGaugePosAnchor`, `ammoGaugePosOfsX/Y`, `ammoGaugePosX/Y`
- `MatchStatusHudConfig`: `matchStatusAnchor`, `matchStatusOfsX/Y`, `matchStatusX/Y`
- `BombLeftHudConfig`: `bombLeftAnchor`, `bombLeftOfsX/Y`, `bombLeftX/Y`; `bombIconPosAnchor`, `bombIconPosOfsX/Y`, `bombIconPosX/Y`
- `RankTimeHudConfig`: `rankAnchor/OfsX/Y/X/Y`, `timeLeftAnchor/OfsX/Y/X/Y`, `timeLimitAnchor/OfsX/Y/X/Y`
- `RadarOverlayConfig`: `radarAnchor`, `radarOfsX/Y`, `radarDstX/Y`
- `CachedHudConfig`: `lastStretchX` tracks the `topStretchX` used for the last position computation; `lastHudScale` tracks hudScale for outline thickness conversion
- `CrosshairHudConfig`: additionally stores `chInnerColor`, `chOuterColor`, `chDotColor` (P-11)

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
- `ApplyAnchor(anchor, ofsX, ofsY, outX, outY)` in `MelonPrimeHudRender.cpp` computes final DS-space coordinates at config-load time (inside `Load*Config()`). Draw functions receive pre-computed positions.
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
- patch table lives in `MelonPrimeHudRender.cpp`
- keyed by `romGroupIndex`

### Performance optimizations (P-9 through P-12)

Applied optimizations in `MelonPrimeHudRender.cpp`:

| ID | Description | Impact | When |
|---|---|---|---|
| P-9 | Frame-level `QFontMetrics` cache (`s_frameFm`/`s_frameFpx`) — constructed once, shared by all draw sub-functions via statics | Eliminates 5+ `p->fontMetrics()` / `p->font().pixelSize()` copies per frame | Per frame |
| P-10 | `HpGaugeColor()` returns `const QColor&` with `static const` threshold colors | Eliminates per-call `QColor(255,0,0)` / `QColor(255,165,0)` construction | Per frame (HP ≤ 50) |
| P-11 | Pre-computed crosshair arm/dot colors with alpha in `CrosshairHudConfig` | Eliminates 3 `QColor` copies + `setAlphaF()` per frame | Per frame |
| P-12 | Separable max-filter dilation (`DilateSeparableTinted`) — two-pass horizontal+vertical max replaces O(R²) naive kernel with O(R) per pixel | ~1.5× faster for R=1, ~3× for R=3 | Config changes / editor |
- guarded by `s_hudPatchApplied`
- restored automatically when custom HUD is no longer active

### Radar overlay note
The software radar overlay path takes `hunterID` and uses `kBtmOverlaySrcCenterY[hunterID]` to pick the crop center. That means the source crop is intentionally not identical for all hunters.

The radar frame SVG (`res/assets/radar/Rader.svg`, Qt resource `:/mph-icon-radar-frame`) is drawn as a **background** behind the crop circle. The SVG's 76×76 viewBox matches the actual bottom screen radar art size in DS pixels (**not** the crop circle diameter). Sizing uses proportional mapping: `kRadarArtSize * dstSize / (srcRadius * 2)` DS pixels, so the frame scales with the overlay but does not match the crop circle boundary. The frame is always shown when the radar is visible. The SVG frame outline can be independently toggled via `BtmOverlayFrameOutlineEnable` (default `true`).

**Radar color** is independently configurable via `Metroid.Visual.BtmOverlayRadarColor[R/G/B]` (default `#B90005` = SVG's original stroke color). The **HUD Outline** settings (`HudOutlineColor*`, `HudOutlineThickness`, `HudOutlineOpacity`) are applied to the frame: a cached outline-colored copy (`s_radarFrameOutline`) is drawn expanded behind the tinted frame (`s_radarFrameTinted`). All three cached images (`s_radarFrame`, `s_radarFrameTinted`, `s_radarFrameOutline`) are invalidated on size/color change.

---

## Settings UI Architecture (`MelonPrimeInputConfig`)

### HUD settings entry points
When adding or modifying a HUD setting, touch all three:
| File | Role | Radar example |
|------|------|---------------|
| `InputConfig/MelonPrimeInputConfig.cpp` | Classic settings dialog — descriptor arrays (`kSecRadar[]`) | `P_BOOL`/`P_INT`/`P_CLR` entries |
| `MelonPrimeHudEditSidePanel.cpp` | In-game edit side panel — `populate*()` methods | `populateRadar()` with `addCheckBox`/`addSpinBox`/`addColorPicker` |
| `MelonPrimeHudConfigScreen.cpp` | In-game HUD config screen — element/property definitions | Element definitions and reset defaults |

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
| Custom HUD | `tabCrosshair` | Enable checkbox + Edit HUD Layout button + 5 hierarchical main sections with nested sub-sections and preview widgets (synchronized with in-game edit mode) |
| Custom HUD Input/Output | `tabCustomHudCode` | TOML export/import of all Custom HUD settings (auto-discovers widgets in `tabCrosshair`) |

### Constructor/setup flow
Current constructor flow in `MelonPrimeInputConfig.cpp` is:
1. `setupKeyBindings()`
2. `setupSensitivityAndToggles()`
3. `setupCollapsibleSections()`
4. `setupCustomHudWidgets()` — programmatic creation of all HUD parameter widgets
5. `setupPreviewConnections()`
6. `setupCustomHudCode()`
7. `snapshotVisualConfig()`

This ordering matters because preview wiring assumes widgets are already initialized.

### Collapsible Sections
The settings UI uses toggle buttons and section widgets wired up in `setupCollapsibleSections()`. Toggle state is persisted under `Metroid.UI.Section*` config keys.

Current persisted sections include groups such as:
- input settings / screen sync / cursor clip / in-game aspect ratio
- sensitivity / gameplay / video / volume / license

### Programmatic HUD Widget Sections
All HUD parameters are accessible via hierarchical collapsible sections in the Custom HUD tab, created programmatically by `setupCustomHudWidgets()` using descriptor arrays (`HudMainSec` / `HudSubSec` / `HudWidgetProp`). Supported widget types: `Bool` (QCheckBox), `Int` (QSpinBox), `Float` (QDoubleSpinBox 0.0-1.0), `String` (QLineEdit), `Anchor9` (QComboBox 9-point), `Align3` (QComboBox L/C/R), `Color3` (3× QSpinBox R/G/B + swatch QPushButton).

Main sections (with nested sub-sections):
- **TEXT SCALE** — single text scale property (no preview)
- **CROSSHAIR** — color/outline/dot/T-style properties + sub: Inner Lines, Outer Lines (preview: CrosshairPreviewWidget)
- **HP / AMMO** — sub: HP Number Position, Ammo Number Position, Weapon Icon, HP Gauge, Ammo Gauge (preview: HpAmmoPreviewWidget)
- **MATCH STATUS HUD** — sub: Score, Rank/Time (nested: Rank, Time Left, Time Limit), Bomb Left, Bomb Icon (preview: MatchStatusPreviewWidget)
- **HUD RADAR** — all radar settings (preview: RadarPreviewWidget)

Each main section (except TEXT SCALE) displays a preview widget on the right side of its expanded section. Preview widgets are instances of specialized classes (`CrosshairPreviewWidget`, `HpAmmoPreviewWidget`, `MatchStatusPreviewWidget`, `RadarPreviewWidget`) that render a live visualization by reading directly from config via `EmuInstance`. Previews automatically refresh whenever widget values change via `invalidateHudAndRefreshPreviews()`.

All widgets are stored in `m_hudWidgets` (config key → QWidget*), all preview widgets in `m_hudPreviews` (list for bulk refresh), and toggle buttons in `m_hudToggles`. This enables data-driven save/restore/snapshot/TOML-export without manual per-widget code.

Widget `objectName`s are derived from config keys (dots → underscores), ensuring TOML auto-discovery (`findChildren<T*>()` in `MelonPrimeInputConfigCustomHudCode.inc`) works automatically.

### Color Picker Pattern
In the in-game edit mode, color pickers use `QColorDialog` triggered by clicking the Color swatch in the element properties panel or the crosshair panel.

### Preview system
The settings dialog's Custom HUD tab features live preview widgets for each major HUD section (except TEXT SCALE). Preview widgets (`CrosshairPreviewWidget`, `HpAmmoPreviewWidget`, `MatchStatusPreviewWidget`, `RadarPreviewWidget`) read live from config and refresh whenever user changes widget values. All previews are tracked in `m_hudPreviews` for efficient bulk refresh.

Preview refresh is triggered by two paths:
1. **Individual widget signals** — each HUD parameter widget's signal handler calls `invalidateHudAndRefreshPreviews()`, which invalidates the runtime HUD config cache and calls `update()` on all preview widgets
2. **Preview apply** — `applyVisualPreview()` in `MelonPrimeInputConfigPreview.cpp` writes all widget values to config, invalidates cache, and refreshes previews

The in-game edit mode overlay provides full-fidelity live preview with drag-and-drop positioning. The settings dialog's previews are simplified summaries (e.g., "HP 100" and gauge bar rather than full game context).

### Preview apply / cancel model
The dialog keeps a visual snapshot through `snapshotVisualConfig()` and restores it with `restoreVisualSnapshot()` when the parent dialog is cancelled.

The snapshot covers:
- `cCustomHud` — the Custom HUD enable bool
- `cAspectRatio` — the in-game aspect ratio enable bool
- `cAspectRatioMode` — the aspect ratio mode combo index
- All ~130 programmatic HUD widget values (stored by config key in `m_visualSnapshot`)

`InputConfigDialog.cpp` calls `saveConfig()` on accept and `restoreVisualSnapshot()` on cancel.

### Save behavior
`saveConfig()` in `MelonPrimeInputConfigConfig.cpp` is the main commit point for UI state.

It currently saves:
- both hotkey pages (`Keyboard` and `Joystick` subtables)
- gameplay / sensitivity / license / volume / sync settings
- in-game aspect ratio settings
- `Metroid.Visual.CustomHUD` enable bool
- all programmatic HUD widget values (via data-driven loop over `m_hudWidgets`)
- all collapsible-section open/closed states (both UI-defined and programmatic HUD sections)

Important side effects in `saveConfig()`:
- if `Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame` changed, windows schedule `panel->updateClipIfNeeded()` via `QTimer::singleShot`
- if `Metroid.Visual.InGameTopScreenOnly` changed, windows schedule `onScreenLayoutChanged()` via Qt queued connection
- `MelonPrime::CustomHud_InvalidateConfigCache()` is called at the end so runtime HUD reads updated values on the next frame

Note: HUD settings are saved from **both** the settings dialog (`saveConfig()`) and the in-game edit mode (`CustomHud_ExitEditMode(true, cfg)` → `Config::Save()`). Both write the same config keys. When the dialog reopens, `setupCustomHudWidgets()` reads the latest config values, ensuring sync.

### Reset/default handlers
Default-reset from the settings dialog is not implemented (no per-section reset buttons). Resetting to defaults happens through the in-game edit mode's Reset button (`ResetEditToDefaults()` in `MelonPrimeHudConfigScreen.cpp`).

### 9-point anchor widgets
In the settings dialog, 9-point anchors are QComboBox widgets with 9 items (Top Left through Bottom Right), created programmatically. In the in-game edit mode, anchors use an embedded 3×3 grid picker.

### Hotkeys
Defined in `EmuInstance.h` and grouped in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`:
- `kMetroidHotkeys` — first controls page (`20` entries)
- `kMetroidHotkeys2` — second controls page (`11` entries)

These replaced the older `hk_tabAddonsMetroid*` naming.

---

## In-game HUD Edit Mode

### Overview
HUD element positioning, crosshair configuration, and text scaling can also be configured through a visual in-game edit mode overlay (entered via `CustomHud_EnterEditMode()`, triggered by the "Edit HUD Layout" button in the settings dialog). This is the "modern" editor; the settings dialog provides the "classic" form-based editor. Both write the same config keys.

### Source location
All edit-mode code lives in `MelonPrimeHudConfigScreen.cpp` (unity-build included by `MelonPrimeHudRender.cpp`), within the `namespace MelonPrime` block.

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
kPropRowH    = 8.0f    // height of each property row
kPropLabelW  = 52.0f   // label column width (wide for full names)
kPropCtrlW   = 30.0f   // control column width
kPropPanelW  = 86.0f   // total panel width
kCrosshairSidePanelX = kCrosshairPanelX + kPropPanelW + 2.0f  // = 88
kCrosshairPreviewX   = kCrosshairSidePanelX + kPropPanelW + 2.0f // = 176
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

### Preview mode interactivity
In preview mode (`s_editPreviewMode = true`) the overlay renders the live HUD instead of element boxes, but remains interactive:
- **Left-click drag** — moves the element under the cursor (same as normal mode)
- **Right-click** — selects the element under the cursor and opens its properties panel; right-clicking empty space deselects
- The floating properties panel and crosshair panel work identically in both modes
- Orientation toggle and resize handles are normal-mode-only (not shown in preview mode)

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

## MelonPrimeHudRender.h — Public API

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

`src/frontend/qt_sdl/OSD_shaders.h`:
- `kScreenVS_OSD` / `kScreenFS_OSD` — OSD (on-screen display) + HUD overlay shader. Has `#ifdef MELONPRIME_DS` branches (VS: `uTexScale` is `vec2` vs `float`; FS: texture swizzle removed by OPT-TX1)

### main_shaders.h ↔ Screen.cpp coupling
**Shaders and C++ are tightly coupled** — always check both sides when modifying either.

| Shader (main_shaders.h) | C++ (Screen.cpp) | Coupling |
|---|---|---|
| `kBtmOverlayFS` `uniform vec3 uPalette[15]` | `initOpenGL()` OPT-SH1 block | Radar palette (15 colors). Shader `PALETTE_SIZE` must match C++ array size and `glUniform3fv` count |
| `kBtmOverlayFS` `uniform float uOpacity` etc. | `drawScreen()` radar draw block | `btmOverlayOpacityULoc` etc. — uniform locations correspond to shader uniform names |
| `kBtmOverlayVS` `uniform vec2 uScreenSize` etc. | `initOpenGL()` `glGetUniformLocation` calls | Uniform locations fetched at init and stored as member variables |

### OSD_shaders.h ↔ Screen.cpp coupling
| Shader (OSD_shaders.h) | C++ (Screen.cpp) | Coupling |
|---|---|---|
| `kScreenFS_OSD` texture read | `glTexImage2D` / `glTexSubImage2D` format arg | MelonPrime: `GL_BGRA` upload + no swizzle (OPT-TX1). Non-MelonPrime: `GL_RGBA` upload + `.bgra` swizzle |

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
- `MelonPrimeHudRender.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
- `MelonPrimeHudConfigScreen.cpp` is a unity-build include (pulled in by `MelonPrimeHudRender.cpp`); do **not** add it to CMakeLists
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Active branch: `highres_fonts_v2`
Current work is on the `highres_fonts_v2` branch. Main changes relative to `master`:
- Full **9-point anchor system** for all HUD element positions (described above)
- All `*X`/`*Y` HUD config values are now **offsets from anchor**, not absolute DS-space coordinates
- `ApplyAnchor()` helper in `MelonPrimeHudRender.cpp` — called once per element in `Load*Config()`, transparent to draw functions
- **In-game HUD edit mode** — full drag-and-drop editor with properties panels, crosshair panel with side panels, live previews; preview mode supports drag + right-click editing (described in "In-game HUD Edit Mode" section above)
- Edit mode code split into `MelonPrimeHudConfigScreen.cpp` (unity-build included by `MelonPrimeHudRender.cpp`)
- **Classic settings dialog restored** — Custom HUD tab organized into 5 hierarchical main sections (Crosshair, HP/Ammo, Match Status HUD, HUD Radar, Text Scale) with nested sub-sections. Each main section features a live preview widget on the right (except Text Scale) that refreshes automatically as users modify settings
- **Programmatic widget architecture** — descriptor-driven (`HudMainSec`/`HudSubSec`/`HudWidgetProp`) widget creation in `setupCustomHudWidgets()`, enabling data-driven save/restore/TOML-export. Preview refresh via `invalidateHudAndRefreshPreviews()` member method
- Snapshot/restore covers all HUD widgets plus 3 global fields
- Property labels use full unabbreviated names throughout the edit mode UI
- Element boxes show live previews (gauge bars, cached icons, sample text) instead of static text labels
- Element box font scales with `HudTextScale` setting
