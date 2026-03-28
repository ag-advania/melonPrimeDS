# MelonPrimeDS — Context for Claude Code Sessions

## Project Overview
MelonPrimeDS is a fork of melonDS emulator specifically tailored for **Metroid Prime Hunters DS** (FPS mouse+keyboard controls). The game renders on the **top screen only** in mouse mode; the bottom screen shows the radar/map.

Build target: **Windows (MinGW cross-compilation from Ubuntu WSL)**.

---

## Key Custom Features Implemented

### 1. Bottom Screen Radar Overlay (`MELONPRIME_CUSTOM_HUD`)
Displays a circular region of the DS bottom screen (radar) as an overlay on the top screen.

**Two render paths:**
- **OpenGL path** (`Screen.cpp`): Custom GLSL shader (`kBtmOverlayVS` / `kBtmOverlayFS` in `main_shaders.h`). Samples layer 1 of the `GL_TEXTURE_2D_ARRAY` (bottom screen). Uses `uSrcCenter` + `uSrcRadius` uniforms to extract a circular region. Exact color filtering (10 radar palette colors). `GL_BLEND` with antialiasing via `smoothstep`.
- **Software path** (`MelonPrimeCustomHud.cpp`): `DrawBottomScreenOverlay()` uses `QPainter` + `QPainterPath` circle clip on a 256×192 `QImage` bottom screen buffer.

**Source region** (hardcoded, radar center on DS bottom screen):
- Center: `(128, 112)` in DS pixels
- Radius: `49.5f / 256.0f` (normalized) ≈ 49.5 DS pixels diameter ~99px

**Config keys** (`Instance*.Metroid.Visual.*`):
| Key | Default | Description |
|-----|---------|-------------|
| `BtmOverlayEnable` | false | Enable overlay |
| `BtmOverlayDstX` | 190 | X position on top screen |
| `BtmOverlayDstY` | 0 | Y position on top screen |
| `BtmOverlayDstSize` | 64 | Size (width=height) of the circular overlay |
| `BtmOverlayOpacity` | 0.85 | Opacity (0.0–1.0) |

**GL shader color filter** (exact match, `round(pixel.rgb * 255.0)`):
```
E01018, 68E028, D0F0A0, D09838, F87038, F8F858, C0F868, F8A8A8, E03030, 5098D0
```

### 2. Custom HUD System
Drawn via `QPainter` over the top screen buffer. Includes:
- **Crosshair** (configurable shape, color, size)
- **HP & Ammo** display (position, gauge, color, weapon icon)
- **Match Status HUD** (battle score, lives, octoliths — configurable labels/colors)
- **HUD Radar** overlay (the circular bottom screen feature above)

---

## Settings UI Architecture (`MelonPrimeInputConfig`)

### Files
| File | Purpose |
|------|---------|
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui` | Qt Designer UI XML |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | Load/save/preview logic |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.h` | Class declaration |

### Tab Structure (4 tabs after restructuring)
| Tab name | Object name | Content |
|----------|-------------|---------|
| Controls | `tabAddonsMetroid` | Keybinds page 1 (dynamically populated via `populatePage()`) |
| Controls 2 | `tabAddonsMetroid2` | Keybinds page 2 (dynamically populated) |
| Settings | `tabMetroid` | Sensitivity, Gameplay, Video, Volume, License, Input Settings, Screen Sync, Aspect Ratio — all collapsible sections in QScrollArea |
| Custom HUD | `tabCrosshair` | Crosshair + HP/Ammo + Match Status + HUD Radar — all collapsible sections in QScrollArea |

### Collapsible Section Pattern
Toggle buttons (`btnToggle*`) + section widgets (`section*`) managed by `setupToggle` lambda in constructor. Toggle state persisted in config under `Metroid.UI.Section*` bool keys.

Example:
```cpp
setupToggle(ui->btnToggleSensitivity, ui->sectionSensitivity, "SENSITIVITY", "Metroid.UI.SectionSensitivity");
```

### Color Picker Buttons
All color pickers use `QPushButton` with `QColorDialog`. Set up via:
```cpp
setupColorButton(ui->btnMetroidCrosshairColor,
    "Metroid.Visual.CrosshairColorR", "Metroid.Visual.CrosshairColorG", "Metroid.Visual.CrosshairColorB");
```
The button background stylesheet reflects the current color. On click, opens `QColorDialog`, updates config + stylesheet.

### Preview Widgets
Each Custom HUD section has a live preview widget:
- `widgetCrosshairPreview` (200×200) — drawn by `updateCrosshairPreview()`
- `widgetHpAmmoPreview` (300×150) — drawn by `updateHpAmmoPreview()`
- `widgetMatchStatusPreview` (300×80) — drawn by `updateMatchStatusPreview()`
- `widgetRadarPreview` (200×150) — drawn by `updateRadarPreview()`

Previews are initialized at constructor end and updated live via `applyVisualPreview()`.

---

## Shader Files
`src/frontend/qt_sdl/main_shaders.h`:
- `kScreenVS` / `kScreenFS` — standard screen renderer
- `kBtmOverlayVS` / `kBtmOverlayFS` — radar overlay (inside `#ifdef MELONPRIME_CUSTOM_HUD`)

`Screen.h` (inside `#ifdef MELONPRIME_CUSTOM_HUD`):
```cpp
GLuint btmOverlayShader;
GLint btmOverlayScreenSizeULoc, btmOverlayOpacityULoc, btmOverlaySrcCenterULoc, btmOverlaySrcRadiusULoc;
GLuint btmOverlayVertexArray, btmOverlayVertexBuffer;
```

---

## Config System
Config defaults in `src/frontend/qt_sdl/Config.cpp` under `/* MelonPrimeDS Custom HUD */` block.

All instance-local settings use prefix `Instance*.Metroid.*`.

---

## Build Notes
- Cross-compile from Ubuntu WSL to Windows using MinGW toolchain
- Feature flag: `MELONPRIME_CUSTOM_HUD` (CMake define)
- vcpkg used for dependencies

---

## Hotkeys
Defined in `EmuInstance.h` as `HK_Metroid*` enum values.
Two lists in `MelonPrimeInputConfig.h`:
- `hk_tabAddonsMetroid` + `hk_tabAddonsMetroid_labels` (20 entries)
- `hk_tabAddonsMetroid2` + `hk_tabAddonsMetroid2_labels` (11 entries)
