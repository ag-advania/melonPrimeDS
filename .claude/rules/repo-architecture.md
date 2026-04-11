# Repo Architecture

## Shader Files
`src/frontend/qt_sdl/main_shaders.h`:
- `kScreenVS` / `kScreenFS` - standard screen renderer
- `kBtmOverlayVS` / `kBtmOverlayFS` - radar overlay shader path used when `MELONPRIME_CUSTOM_HUD` is enabled

`src/frontend/qt_sdl/OSD_shaders.h`:
- `kScreenVS_OSD` / `kScreenFS_OSD` - OSD (on-screen display) + HUD overlay shader. Has `#ifdef MELONPRIME_DS` branches (VS: `uTexScale` is `vec2` vs `float`; FS: texture swizzle removed by OPT-TX1)

### `main_shaders.h` <-> `Screen.cpp` coupling
Shaders and C++ are tightly coupled. Always check both sides when modifying either.

| Shader (`main_shaders.h`) | C++ (`Screen.cpp`) | Coupling |
|---|---|---|
| `kBtmOverlayFS` `uniform vec3 uPalette[15]` | `initOpenGL()` OPT-SH1 block | Radar palette (15 colors). Shader `PALETTE_SIZE` must match C++ array size and `glUniform3fv` count |
| `kBtmOverlayFS` `uniform float uOpacity` etc. | `drawScreen()` radar draw block | `btmOverlayOpacityULoc` etc. - uniform locations correspond to shader uniform names |
| `kBtmOverlayVS` `uniform vec2 uScreenSize` etc. | `initOpenGL()` `glGetUniformLocation` calls | Uniform locations fetched at init and stored as member variables |

### `OSD_shaders.h` <-> `Screen.cpp` coupling

| Shader (`OSD_shaders.h`) | C++ (`Screen.cpp`) | Coupling |
|---|---|---|
| `kScreenFS_OSD` texture read | `glTexImage2D` / `glTexSubImage2D` format arg | MelonPrime: `GL_BGRA` upload + no swizzle (OPT-TX1). Non-MelonPrime: `GL_RGBA` upload + `.bgra` swizzle |

`Screen.h` / `Screen.cpp` also hold the OpenGL-side overlay shader program, uniforms, and draw setup.

## Config System
Default values live in `src/frontend/qt_sdl/Config.cpp`.

MelonPrime-specific settings are primarily stored under:
- `Instance*.Metroid.*` for per-instance values
- `Metroid.UI.Section*` for persisted UI section state

Important current groups include:
- `Metroid.Visual.CustomHUD`
- `Metroid.Visual.HudTextScale` - text visual scale in percent (default 60, font always 6px)
- `Metroid.Visual.HudAutoScaleEnable` - enable automatic HUD scaling (default true)
- `Metroid.Visual.HudAutoScaleCap` - global auto-scale cap percent (default 800)
- `Metroid.Visual.HudAutoScaleCapText` - text category cap (default 800)
- `Metroid.Visual.HudAutoScaleCapIcons` - icons category cap (default 800)
- `Metroid.Visual.HudAutoScaleCapGauges` - gauges category cap (default 800)
- `Metroid.Visual.HudAutoScaleCapCrosshair` - crosshair category cap (default 800)
- `Metroid.Visual.Crosshair*` - crosshair settings (CrosshairScale max 800)
- `Metroid.Visual.HudHp*` - includes `HudHpAnchor` (default 6=BL), `HudHpX/Y` (offsets), `HudHpGaugePosAnchor` (default 6=BL)
- `Metroid.Visual.HudWeapon*` - includes `HudWeaponAnchor` (default 8=BR), `HudWeaponIconPosAnchor` (default 8=BR)
- `Metroid.Visual.HudAmmo*` - includes `HudAmmoGaugePosAnchor` (default 8=BR)
- `Metroid.Visual.HudMatchStatus*` - includes `HudMatchStatusAnchor` (default 0=TL)
- `Metroid.Visual.HudRank*` - includes `HudRankAnchor` (default 0=TL)
- `Metroid.Visual.HudTimeLeft*` - includes `HudTimeLeftAnchor` (default 0=TL)
- `Metroid.Visual.HudTimeLimit*` - includes `HudTimeLimitAnchor` (default 0=TL)
- `Metroid.Visual.HudBombLeft*` - includes `HudBombLeftAnchor` (default 8=BR), `HudBombLeftIconPosAnchor` (default 8=BR)
- `Metroid.Visual.BtmOverlay*` - includes `BtmOverlayAnchor` (default 2=TR)
- `Metroid.Visual.InGameAspectRatio*`

### Default value type classification — CRITICAL

`Config.cpp` has **three separate typed default lists**. Each `GetXxx()` method looks only in its own list; cross-list entries are silently ignored and the hard-coded fallback is used instead (e.g. `false` for bool, `0.0` for double).

| List | Type | `GetXxx` that reads it | Use for |
|---|---|---|---|
| `DefaultInts` | `int` | `GetInt()` | integers, anchors, color R/G/B channels, enum indices |
| `DefaultBools` | `bool` | `GetBool()` | booleans (Show, Enable, …) |
| `DefaultDoubles` | `double` | `GetDouble()` | opacities, scale factors, floating-point values |

**Rule**: always register a new key in the list that matches its C++ accessor:
- `cfg.GetBool(...)` → entry must be in `DefaultBools`
- `cfg.GetDouble(...)` → entry must be in `DefaultDoubles`
- `cfg.GetInt(...)` → entry must be in `DefaultInts`

In particular, `true`/`false` literals placed in `DefaultInts` are **not** seen by `GetBool()`, and `1.0`/`0.8` etc. placed in `DefaultInts` are **not** seen by `GetDouble()`. Both bugs silently produce 0/false at runtime for any key not yet present in the user's config file.

When touching UI or runtime HUD behavior, always check both:
- `Config.cpp` for defaults (and correct list placement per the table above)
- `MelonPrimeInputConfigConfig.cpp` for save/reset coverage

## Build Notes
- Main feature flags in `src/frontend/qt_sdl/CMakeLists.txt`:
  - `MELONPRIME_DS`
  - `MELONPRIME_CUSTOM_HUD`
- `MelonPrimeHudRender.cpp` and `InputConfig/MelonPrimeInputConfig.cpp` are explicitly built as part of the frontend
- `MelonPrimeHudConfigOnScreen.cpp` is a unity-build include (pulled in by `MelonPrimeHudRender.cpp`); do not add it to `CMakeLists.txt`
- The project has been built on Windows and via MinGW cross-compilation from WSL
- `vcpkg/` is used for dependencies in this repo setup

## Active Branch: `highres_fonts_v2`
Current work is on the `highres_fonts_v2` branch. Main changes relative to `master`:
- Full 9-point anchor system for all HUD element positions
- All `*X`/`*Y` HUD config values are offsets from anchor, not absolute DS-space coordinates
- `ApplyAnchor()` helper in `MelonPrimeHudRender.cpp` is called once per element in `Load*Config()`, transparent to draw functions
- In-game HUD edit mode with drag-and-drop editor, properties panels, crosshair panel with side panels, and live previews
- Edit mode code split into `MelonPrimeHudConfigOnScreen.cpp` (unity-build included by `MelonPrimeHudRender.cpp`)
- Classic settings dialog restored with 5 hierarchical main sections and live preview widgets on the right (except HUD Scale)
- Programmatic widget architecture via `HudMainSec` / `HudSubSec` / `HudWidgetProp`, enabling data-driven save/restore/TOML-export
- Snapshot/restore covers all HUD widgets plus 3 global fields
- HUD auto-scale system with per-category caps (text, icons, gauges, crosshair); radar excluded from auto-scale
- Property labels use full unabbreviated names throughout the edit mode UI
- Element boxes show live previews (gauge bars, cached icons, sample text) instead of static text labels
- Element box font scales with `HudTextScale`
