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
| Custom HUD | `tabCrosshair` | Crosshair, HP/ammo, match status, rank/time, bomb-left HUD, and radar settings |

### Constructor/setup flow
Current constructor flow in `MelonPrimeInputConfig.cpp` is:
1. `setupHiddenLabels()`
2. `setupKeyBindings()`
3. `setupSensitivityAndToggles()`
4. `setupMatchStatusHud()`
5. `setupCollapsibleSections()`
6. `setupHpAmmoHud()`
7. `setupCrosshair()`
8. `setupPreviewConnections()`
9. `setupRadar()`
10. `snapshotVisualConfig()`
11. initial preview refreshes

This ordering matters because preview wiring assumes widgets are already initialized.

### Collapsible Sections
The settings/custom HUD UI uses toggle buttons and section widgets wired up in `setupCollapsibleSections()`. Toggle state is persisted under `Metroid.UI.Section*` config keys.

Current persisted sections include groups such as:
- crosshair / inner / outer
- HP-ammo / weapon icon / gauges
- match-status / rank-time / bomb-left / radar
- input settings / screen sync / cursor clip / in-game aspect ratio
- sensitivity / gameplay / video / volume / license

### Color Picker Pattern
Color pickers use `QPushButton` plus `QColorDialog`, coordinated with preset combos, RGB spin boxes, and hex editors through `setupColorButton()` and helper sync functions.

Important behavior:
- color buttons write values directly into local config immediately
- many non-color widgets are only persisted on `saveConfig()`
- some sub-colors support an “Overall” mode where the runtime HUD inherits the main match-status color

### Preview system
Live preview rendering is split by feature area:
- `widgetCrosshairPreview`
- `widgetHpAmmoPreview`
- `widgetMatchStatusPreview`
- `widgetRadarPreview`

Preview implementation notes:
- `loadHudFont()` loads the same `:/mph-font` resource used by the in-game HUD preview
- previews render against a simulated 256x192 DS top-screen area scaled to the preview widget
- `updateHpAmmoPreview()` and `updateMatchStatusPreview()` intentionally mirror runtime layout rules such as text alignment, gauge anchor calculations, and 9-point anchor application
- `updateRadarPreview()` is only an approximate visualization of destination placement/size/opacity; it does not sample the real bottom-screen texture
- each preview function contains a local lambda (`applyAnchorPreview` in `updateHpAmmoPreview`, `applyAnchor2` in `updateMatchStatusPreview`) that mirrors `ApplyAnchor()` from `MelonPrimeCustomHud.cpp`

### Preview apply / cancel model
The dialog keeps a visual snapshot through `snapshotVisualConfig()` and restores it with `restoreVisualSnapshot()` when the parent dialog is cancelled.

This matters because:
- preview interactions may write temporary values into local config before the user presses OK
- `restoreVisualSnapshot()` is the guard that puts both widget state and config-backed visual values back when cancelling the dialog
- `InputConfigDialog.cpp` calls `saveConfig()` on accept and `restoreVisualSnapshot()` on cancel

### Save behavior
`saveConfig()` in `MelonPrimeInputConfigConfig.cpp` is the main commit point for UI state.

It currently saves:
- both hotkey pages (`Keyboard` and `Joystick` subtables)
- gameplay / sensitivity / license / volume / sync settings
- in-game aspect ratio settings
- all Custom HUD values
- all collapsible-section open/closed states

Important side effects in `saveConfig()`:
- if `Metroid.Visual.ClipCursorToBottomScreenWhenNotInGame` changed, windows schedule `panel->updateClipIfNeeded()` via `QTimer::singleShot`
- `MelonPrime::CustomHud_InvalidateConfigCache()` is called at the end so runtime HUD reads updated values on the next frame

### Reset/default handlers
Default-reset entry points live in `MelonPrimeInputConfigConfig.cpp`:
- `resetCrosshairDefaults()`
- `resetHpAmmoDefaults()`
- `resetMatchStatusDefaults()`
- `resetRankTimeDefaults()`

Useful note:
- these functions update both widgets and, in some cases, config-backed color values so previews stay consistent immediately

### Hotkeys
Defined in `EmuInstance.h` and grouped in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`:
- `kMetroidHotkeys` — first controls page (`20` entries)
- `kMetroidHotkeys2` — second controls page (`11` entries)

These replaced the older `hk_tabAddonsMetroid*` naming.

### 9-point anchor widgets in the UI
Every HUD position section in `MelonPrimeInputConfig.ui` has a `QComboBox` named `comboMetroid*Anchor` with 9 items (Top Left → Bottom Right). The adjacent X/Y spinboxes are labelled **Offset X / Offset Y** (not Position X/Y) to reflect that they are offsets from the anchor.

Current anchor combo widget names:
| Element | Widget name |
|---------|-------------|
| HP text | `comboMetroidHudHpAnchor` |
| HP gauge (independent pos) | `comboMetroidHudHpGaugePosAnchor` |
| Weapon/Ammo text | `comboMetroidHudWeaponAnchor` |
| Weapon icon (independent pos) | `comboMetroidHudWeaponIconPosAnchor` |
| Ammo gauge (independent pos) | `comboMetroidHudAmmoGaugePosAnchor` |
| Match Status | `comboMetroidHudMatchStatusAnchor` |
| Rank | `comboMetroidHudRankAnchor` |
| Time Left | `comboMetroidHudTimeLeftAnchor` |
| Time Limit | `comboMetroidHudTimeLimitAnchor` |
| Bomb Left text | `comboMetroidHudBombLeftAnchor` |
| Bomb Left icon (independent pos) | `comboMetroidHudBombLeftIconPosAnchor` |
| Radar overlay | `comboMetroidBtmOverlayAnchor` |

Note: `comboMetroidHudHpGaugeAnchor`, `comboMetroidHudAmmoGaugeAnchor` etc. are **gauge-relative-to-text** anchors (a different 5-point system), not the 9-point screen anchors.

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
- UI form (`MelonPrimeInputConfig.ui`): all position rows renamed "Offset X/Y", anchor combos inserted at row 1 of each section, row indices of subsequent widgets shifted accordingly
- Preview lambdas (`applyAnchorPreview`, `applyAnchor2`) in `MelonPrimeInputConfigPreview.cpp` mirror the runtime logic
- Snapshot/restore in `snapshotVisualConfig` / `restoreVisualSnapshot` covers all new anchor combo widgets
