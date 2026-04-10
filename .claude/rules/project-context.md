# Project Context

## Project Overview
MelonPrimeDS is a fork of melonDS tailored for Metroid Prime Hunters DS with mouse/keyboard-focused FPS controls and game-specific emulator/UI patches.

Primary target: Windows. The repo has also been used with MinGW cross-compilation from Ubuntu WSL.

## Key Custom Features Implemented

### 1. Bottom Screen Radar Overlay (`MELONPRIME_CUSTOM_HUD`)
Displays a circular crop of the DS bottom screen on top of the rendered top screen.

Two render paths:
- OpenGL path (`Screen.cpp` + `main_shaders.h`): samples the bottom screen from the texture array and clips it to a circular overlay.
- Software path (`MelonPrimeHudRender.cpp`): `DrawBottomScreenOverlay()` draws a cropped `QImage` region with `QPainter` and a circular clip path. A radar frame SVG (`res/assets/radar/Rader.svg`) is drawn behind the crop with an independently configurable color (`BtmOverlayRadarColor*`), and the HUD outline is applied to it.

Current source region behavior:
- X center is fixed at `128`
- Y center is hunter-specific via `kBtmOverlaySrcCenterY[]` in `MelonPrimeConstants.h`
- Radius is configurable via `Metroid.Visual.BtmOverlaySrcRadius` (default `46`)

Config keys (`Instance*.Metroid.Visual.*`):

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
- Crosshair with inner/outer arms, outline, center dot, T-style, opacity, thickness, and XY-length controls
- HP HUD with text, optional auto-color, and optional gauge
- Weapon / Ammo HUD with text, optional weapon icon, optional icon tint overlay, and optional ammo gauge
- Match Status HUD with per-mode labels and separate overall/label/value/separator/goal colors
- Rank / Time HUD for multiplayer ranking and timer display
- Bomb Left HUD for alt-form bomb count, with optional text and bomb icon
- Radar overlay described above

Implementation details worth knowing:
- `MelonPrimeHudRender.cpp` caches HUD config and several rendered assets to avoid repeated per-frame work.
- Weapon icons and bomb icons are cached and optionally tint-overlaid.
- The custom HUD also applies a No HUD patch to suppress parts of the game's native HUD while custom elements are active.

### 3. In-game Aspect Ratio Patch
`MelonPrimePatch.cpp` patches the game's projection/scaling setup so gameplay FOV better matches the emulator aspect ratio.

Config keys:
- `Metroid.Visual.InGameAspectRatio`
- `Metroid.Visual.InGameAspectRatioMode`

Modes:
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
- `kBtmOverlaySrcCenterY[]` - per-hunter radar source Y positions
- `kHunterFrameColor[]` - per-hunter radar frame colors (RGB packed as `0xRRGGBB`)

### 5. In-game Logic Structure
`MelonPrimeInGame.cpp` contains the current hot/cold-split gameplay update path:
- `MelonPrimeCore::HandleInGameLogic()` is the hot per-frame entry point
- Rare actions are outlined into cold helpers such as morph, weapon-check, and adventure UI handlers
- Movement/buttons, morph-ball boost, and mouse/stylus aim are handled separately for better hot-path locality
