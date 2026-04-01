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

## Settings UI Architecture (`MelonPrimeInputConfig`)

### Files
| File | Purpose |
|------|---------|
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui` | Qt Designer UI XML |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h` | class declaration and hotkey tables |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | constructor/setup/save logic |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigPreview.cpp` | live preview rendering |

### Tab Structure
| Tab name | Object name | Content |
|----------|-------------|---------|
| Controls | `tabAddonsMetroid` | Hotkey mappings page 1 |
| Controls 2 | `tabAddonsMetroid2` | Hotkey mappings page 2 |
| Settings | `tabMetroid` | Sensitivity, toggles, hunter license, volume, video, screen sync, in-game aspect ratio, and related settings |
| Custom HUD | `tabCrosshair` | Crosshair, HP/ammo, match status, rank/time, bomb-left HUD, and radar settings |

### Collapsible Sections
The settings/custom HUD UI uses toggle buttons and section widgets wired up in `setupCollapsibleSections()`. Toggle state is persisted under `Metroid.UI.Section*` config keys.

### Color Picker Pattern
Color pickers use `QPushButton` plus `QColorDialog`, coordinated with preset combos, RGB spin boxes, and hex editors through `setupColorButton()` and helper sync functions.

### Preview Widgets
Live preview rendering is split by feature area:
- `widgetCrosshairPreview`
- `widgetHpAmmoPreview`
- `widgetMatchStatusPreview`
- `widgetRadarPreview`

Constructor flow currently initializes previews with:
- `updateRadarPreview()`
- `updateCrosshairPreview()`
- `updateHpAmmoPreview()`
- `updateMatchStatusPreview()`

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
- `DrawBottomScreenOverlay()` now takes `hunterID` so the source crop can use hunter-specific radar centers.
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

---

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- `MelonPrimeCustomHud.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

---

## Hotkeys
Defined in `EmuInstance.h` and grouped in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h`:
- `kMetroidHotkeys` — first controls page (`20` entries)
- `kMetroidHotkeys2` — second controls page (`11` entries)

These replaced the older `hk_tabAddonsMetroid*` naming.
