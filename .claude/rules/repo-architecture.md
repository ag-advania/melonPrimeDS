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

### OPT-DR1 — Dirty-Rect Overlay Optimization

**Goal**: avoid per-frame full-window memset and full GL texture upload for the HUD overlay.

**`CustomHud_Render` return value**: returns `QRect` (dirty pixel rect in overlay space, in `Overlay[0]` coordinates). Returns empty `QRect` when nothing was drawn. `void` at all call sites was replaced with capturing this return value.

**Key statics in `MelonPrimeHudRender.cpp`**:
- `s_chPrevPxCx / s_chPrevPxCy` — previous frame crosshair pixel center (`INT_MIN` = unset)
- `s_chDirtyThisFrame` — union of current + previous crosshair bbox, computed in `DrawCrosshair`
- `s_staticHudDirtyPx` — conservative pixel-space bbox covering all non-crosshair HUD elements; cached across frames
- `s_staticDirtyStale` — set `true` when config or transform changes; triggers `RefreshStaticHudDirty()` at next frame start

**`CrosshairBboxPx(cx, cy, ch)`**: computes exact pixel bbox from stamp (`s_chOutlineStamp`, `s_chStampOrigin`), pre-cached arm rects, and dot rect. Used to union prev + current crosshair positions.

**`RefreshStaticHudDirty()`**: iterates over all enabled fixed-position HUD elements (HP, weapon, ammo, radar, match status, rank, time, bomb), converts their DS-space positions to pixel space, adds per-element padding, unions into `s_staticHudDirtyPx`. Called once per config/transform change (not every frame).

**Stale flag triggers** (`s_staticDirtyStale = true`):
- `RecomputeAnchorPositions` (positions changed)
- `CustomHud_ResetPatchState` (emu stop/reset)
- `CustomHud_InvalidateConfigCache` (settings saved)

**Screen.cpp software path** (`ScreenPanelNative::paintEvent`):
```cpp
static QRect s_hudPrevDirtyS;
// On resize: full fill + reset s_hudPrevDirtyS
// On same size: clear only prev dirty via CompositionMode_Source
QRect curDirty = MelonPrime::CustomHud_Render(...);
const QRect compositeRect = s_hudPrevDirtyS.united(curDirty);
if (!compositeRect.isEmpty())
    painter.drawImage(QPoint(compositeRect.x(), compositeRect.y()), Overlay[0], compositeRect);
s_hudPrevDirtyS = curDirty;
```

**Screen.cpp GL path** (`ScreenPanelGL::drawScreen`):
```cpp
static QRect s_hudPrevDirtyGL;
// partial clear of prev dirty via CompositionMode_Source
QRect curDirty = MelonPrime::CustomHud_Render(...);
const QRect uploadRect = s_hudPrevDirtyGL.united(curDirty);
if (!uploadRect.isEmpty()) {
    glPixelStorei(GL_UNPACK_ROW_LENGTH,  topOutW);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, uploadRect.x());
    glPixelStorei(GL_UNPACK_SKIP_ROWS,   uploadRect.y());
    glTexSubImage2D(GL_TEXTURE_2D, 0,
        uploadRect.x(), uploadRect.y(), uploadRect.width(), uploadRect.height(),
        GL_BGRA, GL_UNSIGNED_BYTE, Overlay[0].constBits());
    // reset GL_UNPACK_* to 0
}
s_hudPrevDirtyGL = curDirty;
```

**Removed dead code**: `GetOutlineBuffer()`, `s_outlineBuf`, `s_prevOutlineDirty` — were declared but never called; deleted.

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

### GetXxx Default Coverage Audit (Metroid keys)
Use this quick audit whenever you add/rename settings or large default updates:
- Enumerate call sites by accessor type: `GetInt("Metroid....")`, `GetDouble("Metroid....")`, `GetBool("Metroid....")`.
- Ensure every runtime/UI-read key exists in the matching default list (`DefaultInts` / `DefaultDoubles` / `DefaultBools`).
- If a key type changes (for example int->double), move it to the correct list and remove the old entry from the old list.

Rules of thumb from recent fixes:
- Outline keys split by type: `*OutlineColor[R/G/B]` and `*OutlineThickness` are int; `*OutlineOpacity` is double.
- Non-Visual `Metroid.*` keys used by InputConfig/MelonPrimeCore also need defaults (not just `Metroid.Visual.*`).
- `Metroid.Visual.HudFontSize` is a legacy migration-only key read behind `HasKey()`; it does not need a new default unless migration logic changes.

#### Audit Command (PowerShell, Metroid.*)
Run from repo root. This lists missing defaults by accessor type and catches cross-list type mistakes:

```powershell
$cfgPath = "src/frontend/qt_sdl/Config.cpp"
$cfgLines = Get-Content $cfgPath
$ints    = [System.Collections.Generic.HashSet[string]]::new()
$doubles = [System.Collections.Generic.HashSet[string]]::new()
$bools   = [System.Collections.Generic.HashSet[string]]::new()
$state = ""
foreach ($line in $cfgLines) {
  if ($line -match "^\s*DefaultList<int>\s+DefaultInts") { $state = "int"; continue }
  if ($line -match "^\s*DefaultList<double>\s+DefaultDoubles") { $state = "double"; continue }
  if ($line -match "^\s*DefaultList<bool>\s+DefaultBools") { $state = "bool"; continue }
  if ($state -ne "" -and $line -match "^\s*};\s*$") { $state = ""; continue }
  if ($state -eq "") { continue }
  if ($line -match "\{\"Instance\*\.Metroid\.([^\"]+)\"\s*,") {
    $k = "Metroid." + $matches[1]
    if ($state -eq "int")    { [void]$ints.Add($k) }
    if ($state -eq "double") { [void]$doubles.Add($k) }
    if ($state -eq "bool")   { [void]$bools.Add($k) }
  }
}

$usageInt = rg -o "GetInt\(\"Metroid\.[^\"]+\"\)" src/frontend/qt_sdl -g"*.cpp" -g"*.h"  | % { if($_ -match "GetInt\(\"([^\"]+)\"\)"){ $matches[1] } } | sort -Unique
$usageDbl = rg -o "GetDouble\(\"Metroid\.[^\"]+\"\)" src/frontend/qt_sdl -g"*.cpp" -g"*.h" | % { if($_ -match "GetDouble\(\"([^\"]+)\"\)"){ $matches[1] } } | sort -Unique
$usageBol = rg -o "GetBool\(\"Metroid\.[^\"]+\"\)" src/frontend/qt_sdl -g"*.cpp" -g"*.h"   | % { if($_ -match "GetBool\(\"([^\"]+)\"\)"){ $matches[1] } } | sort -Unique

"GetInt missing:";    $usageInt | ? { -not $ints.Contains($_) }
"GetDouble missing:"; $usageDbl | ? { -not $doubles.Contains($_) }
"GetBool missing:";   $usageBol | ? { -not $bools.Contains($_) }

"GetInt in DefaultDoubles:";  $usageInt | ? { $doubles.Contains($_) }
"GetInt in DefaultBools:";    $usageInt | ? { $bools.Contains($_) }
"GetDouble in DefaultInts:";  $usageDbl | ? { $ints.Contains($_) }
"GetBool in DefaultInts:";    $usageBol | ? { $ints.Contains($_) }
"GetBool in DefaultDoubles:"; $usageBol | ? { $doubles.Contains($_) }
```

## Active Branch: `highres_fonts_v3`
Current work is on the `highres_fonts_v3` branch. Main changes relative to `master`:
- Full 9-point anchor system for all HUD element positions
- All `*X`/`*Y` HUD config values are offsets from anchor, not absolute DS-space coordinates
- `ApplyAnchor()` helper in `MelonPrimeHudRender.cpp` is called once per element in `Load*Config()`, transparent to draw functions
- In-game HUD edit mode with drag-and-drop editor, properties panels, crosshair panel with side panels, and live previews
- Edit mode code rooted at `MelonPrimeHudConfigOnScreen.cpp` (unity-build included by `MelonPrimeHudRender.cpp`) and split into `.inc` fragments:
  - `MelonPrimeHudConfigOnScreenDefs.inc` - definition tables
  - `MelonPrimeHudConfigOnScreenSnapshot.inc` - snapshot/restore/reset
  - `MelonPrimeHudConfigOnScreenDraw.inc` - bounds and overlay drawing
  - `MelonPrimeHudConfigOnScreenInput.inc` - public edit API and input handling
- Classic settings dialog restored with 5 hierarchical main sections and live preview widgets on the right (except HUD Scale)
- Programmatic widget architecture via `HudMainSec` / `HudSubSec` / `HudWidgetProp`, enabling data-driven save/restore/TOML-export
- Snapshot/restore covers all HUD widgets plus 3 global fields
- HUD auto-scale system with per-category caps (text, icons, gauges, crosshair); radar excluded from auto-scale
- Property labels use full unabbreviated names throughout the edit mode UI
- Element boxes show live previews (gauge bars, cached icons, sample text) instead of static text labels
- Element box font scales with `HudTextScale`
- OPT-DR1 dirty-rect overlay optimization: `CustomHud_Render` returns `QRect`; only dirty regions cleared/composited/uploaded per frame (see OPT-DR1 section above)
