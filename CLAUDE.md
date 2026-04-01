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
| `BtmOverlayDstX` | 190 | X position on top screen |
| `BtmOverlayDstY` | 0 | Y position on top screen |
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
- `CachedHudConfig`
- `BattleMatchState`

### Runtime HUD behavior details
Useful implementation notes for future edits:
- Crosshair X coordinate is adjusted by `topStretchX` so widescreen rendering still lines up with game aim position.
- HP and ammo gauges support both anchored positioning relative to text and fully independent positioning.
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
- `updateHpAmmoPreview()` and `updateMatchStatusPreview()` intentionally mirror runtime layout rules such as text alignment and gauge anchor calculations
- `updateRadarPreview()` is only an approximate visualization of destination placement/size/opacity; it does not sample the real bottom-screen texture

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
    float topStretchX = 1.0f
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
- `Metroid.Visual.Crosshair*`
- `Metroid.Visual.HudHp*`
- `Metroid.Visual.HudWeapon*`
- `Metroid.Visual.HudAmmo*`
- `Metroid.Visual.HudMatchStatus*`
- `Metroid.Visual.HudRank*`
- `Metroid.Visual.HudTimeLeft*`
- `Metroid.Visual.HudTimeLimit*`
- `Metroid.Visual.HudBombLeft*`
- `Metroid.Visual.BtmOverlay*`
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
