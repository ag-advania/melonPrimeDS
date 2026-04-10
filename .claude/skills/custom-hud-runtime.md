# Custom HUD Runtime

## MelonPrimeHudRender Details

### Source files and responsibilities

| File | Responsibility |
|------|----------------|
| `src/frontend/qt_sdl/MelonPrimeHudRender.h` | public API surface |
| `src/frontend/qt_sdl/MelonPrimeHudRender.cpp` | runtime HUD rendering, cache management, no-HUD patching, match-state caching |
| `src/frontend/qt_sdl/MelonPrimeHudConfigOnScreen.cpp` | in-game HUD layout editor (unity-build included by `MelonPrimeHudRender.cpp`, not in `CMakeLists.txt`) |
| `src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.cpp` | in-game HUD element properties side panel - `populate*()` functions define per-element settings (unity-build included by `MelonPrimeHudConfigOnScreen.cpp`) |
| `src/frontend/qt_sdl/MelonPrimeHudConfigOnScreenEdit.h` | side panel class declaration |
| `src/frontend/qt_sdl/MelonPrimeConstants.h` | hunter-specific radar source Y positions and related constants |
| `src/frontend/qt_sdl/Screen.cpp` | calls `CustomHud_Render()` and OpenGL radar overlay path |

### Runtime entry points
`CustomHud_Render()` is the main per-frame entry point.

Current high-level flow inside `CustomHud_Render()`:
1. If edit mode, draw element overlay and return immediately.
2. Return immediately if not in-game.
3. If custom HUD is disabled, restore the native HUD patch state and exit.
4. Apply the no-HUD patch.
5. Refresh cached HUD config when invalidated, or recompute anchors if `topStretchX` changed.
6. Read current gameplay values from RAM.
7. Hide HUD entirely for certain gameplay states.
8. Set up painter (scale + translate + font); P-9 caches `QFontMetrics` on first call.
9. Draw HP, bomb-left, match-status, and rank/time.
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
- P-9: `s_frameFm` / `s_frameFpx` - frame-level `QFontMetrics` cache constructed once on first call and shared by all draw sub-functions via statics
- P-11: `CrosshairHudConfig::chInnerColor/chOuterColor/chDotColor` - pre-computed arm/dot colors with alpha set at config load time
- Radar frame SVG (`s_radarFrame` / `s_radarFrameTinted` / `s_radarFrameOutline`) - loaded once via `loadSvgToHeight()`, tinted and outline-colored images cached separately; re-tinted on color change

### `CachedHudConfig` struct notes
Each sub-struct stores both raw anchor + offset values and computed final coordinates. The raw values are loaded once per config change in `Load*Config()`. Final positions are recomputed by `RecomputeAnchorPositions(topStretchX)` whenever config changes or `topStretchX` changes (window resize). This ensures anchored elements track the actual visible screen edges in widescreen/narrow views.

Key struct fields:
- `HpHudConfig`: `hpAnchor`, `hpOfsX/Y` (raw), `hpX/Y` (final); `hpGaugePosAnchor`, `hpGaugePosOfsX/Y`, `hpGaugePosX/Y`
- `WeaponHudConfig`: `wpnAnchor`, `wpnOfsX/Y`, `wpnX/Y`; `iconPosAnchor`, `iconPosOfsX/Y`, `iconPosX/Y`; `ammoGaugePosAnchor`, `ammoGaugePosOfsX/Y`, `ammoGaugePosX/Y`
- `MatchStatusHudConfig`: `matchStatusAnchor`, `matchStatusOfsX/Y`, `matchStatusX/Y`
- `BombLeftHudConfig`: `bombLeftAnchor`, `bombLeftOfsX/Y`, `bombLeftX/Y`; `bombIconPosAnchor`, `bombIconPosOfsX/Y`, `bombIconPosX/Y`
- `RankTimeHudConfig`: `rankAnchor/OfsX/Y/X/Y`, `timeLeftAnchor/OfsX/Y/X/Y`, `timeLimitAnchor/OfsX/Y/X/Y`
- `RadarOverlayConfig`: `radarAnchor`, `radarOfsX/Y`, `radarDstX/Y`
- `CachedHudConfig`: `lastStretchX` tracks the `topStretchX` used for the last position computation; `lastHudScale` tracks `hudScale` for outline thickness conversion; `scaleText/scaleIcons/scaleGauges/scaleCrosshair` store per-category auto-scale factors
- `CrosshairHudConfig`: additionally stores `chInnerColor`, `chOuterColor`, `chDotColor`

### High-resolution HUD rendering
The HUD overlay is rendered into a hi-res buffer matching the actual screen output size, not DS-native `256x192`.

Key design points:
- `Overlay[0]` is sized to the full output window. No intermediate scaling; the buffer matches display 1:1.
- `CustomHud_Render()` receives `hudScale` (`scaleY = output height / 192`) and `topStretchX` (`scaleX / scaleY`: `>1` widescreen, `<1` narrow window, `=1` exact `4:3`).
- The painter uses `scale(hudScale, hudScale)` then `translate((topStretchX-1)*128, 0)`.
- DS Y is always scaled by `scaleY`; DS X effectively scales by `scaleX` via the translate.
- All HUD element positions are specified in DS-space (`0-255` / `0-191`) and scale correctly with the window without manual correction.
- Icons are drawn with `drawImage(QRectF(x, y, icon.width(), icon.height()), icon)` so they scale with the painter transform.
- The font is always rendered at `kCustomHudFontSize = 6` px (optimal glyph quality for `mph.ttf`). Visual text size is controlled separately by `Metroid.Visual.HudTextScale` (percentage, default `60`).
- Text bitmaps are drawn scaled via `DrawCachedText(..., tds)` where `tds = textDrawScale` (pre-computed from auto-scale + user TextScale).
- The crosshair reads `cx/cy` from RAM in DS-space (`0-255`) and requires no manual `topStretchX` correction.

### 9-point anchor system
All HUD text/icon positions use a 9-point anchor + offset model.

```text
0=Top Left    1=Top Center    2=Top Right
3=Middle Left 4=Middle Center 5=Middle Right
6=Bottom Left 7=Bottom Center 8=Bottom Right
```

- Anchor base coordinates (DS canvas `256x192`):
  - X: col 0 -> 0, col 1 -> 128, col 2 -> 256
  - Y: row 0 -> 0, row 1 -> 96, row 2 -> 192
- The stored `*X`/`*Y` config values are offsets from the anchor base, not absolute positions.
- `ApplyAnchor(anchor, ofsX, ofsY, outX, outY)` in `MelonPrimeHudRender.cpp` computes final DS-space coordinates at config-load time inside `Load*Config()`.
- Every HUD element has a `*Anchor` config key. Default anchors:
  - HP: 6 (BL), Weapon/Ammo: 8 (BR), WeaponIconPos: 8 (BR)
  - HpGaugePos: 6 (BL), AmmoGaugePos: 8 (BR)
  - MatchStatus: 0 (TL), Rank: 0 (TL), TimeLeft: 0 (TL), TimeLimit: 0 (TL)
  - BombLeft: 8 (BR), BombLeftIconPos: 8 (BR)
  - Radar: 2 (TR)
- Preview functions (`updateHpAmmoPreview`, `updateMatchStatusPreview`, `updateRadarPreview`) each contain a local anchor helper that mirrors the same logic.

### Runtime HUD behavior details
Useful implementation notes for future edits:
- HP and ammo gauges support both anchored positioning relative to text (via `HudHpGaugeAnchor`) and fully independent absolute positioning (mode 1, via `HudHpGaugePosAnchor` + offset).
- Match-status colors use an "invalid `QColor` means inherit overall color" convention in cached config.
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

| ID | Description | Impact | When |
|---|---|---|---|
| P-9 | Frame-level `QFontMetrics` cache (`s_frameFm`/`s_frameFpx`) - constructed once, shared by all draw sub-functions via statics | Eliminates 5+ `p->fontMetrics()` / `p->font().pixelSize()` copies per frame | Per frame |
| P-10 | `HpGaugeColor()` returns `const QColor&` with `static const` threshold colors | Eliminates per-call `QColor(255,0,0)` / `QColor(255,165,0)` construction | Per frame (HP <= 50) |
| P-11 | Pre-computed crosshair arm/dot colors with alpha in `CrosshairHudConfig` | Eliminates 3 `QColor` copies + `setAlphaF()` per frame | Per frame |
| P-12 | Separable max-filter dilation (`DilateSeparableTinted`) - two-pass horizontal+vertical max replaces `O(R^2)` naive kernel with `O(R)` per pixel | About `1.5x` faster for `R=1`, about `3x` for `R=3` | Config changes / editor |

### HUD Auto-Scale System
Automatic integer-based scaling that makes HUD elements readable at high resolutions without manual adjustment.

Core formula:
- `autoScaleInt = max(1, floor(hudScale))` - integer DS resolution multiplier
- `autoScalePct = autoScaleInt * 100`
- Per-category effective scale = `min(autoScalePct, globalCap, categoryCap) / 100.0`

Per-category scale factors in `CachedHudConfig`:

| Field | Config cap key | Applied to |
|-------|---------------|------------|
| `scaleText` | `HudAutoScaleCapText` | Text: additive with `HudTextScale` - `effectiveTextPct = scaleText*100 + (textScalePct - 100)` |
| `scaleIcons` | `HudAutoScaleCapIcons` | SVG icons loaded at `iconHeight * scaleIcons` (rasterized at full resolution, no draw-time stretch) |
| `scaleGauges` | `HudAutoScaleCapGauges` | Gauge bar `len * scaleGauges` and `wid * scaleGauges` at both `CalcGaugePos` and `DrawGauge` |
| `scaleCrosshair` | `HudAutoScaleCapCrosshair` | Crosshair arm rects recomputed with `cs = chScale * scaleCrosshair` after auto-scale is determined |

Exclusions:
- Radar overlay is not auto-scaled.

Enable/disable:
- Gated by `HudAutoScaleEnable` (default true). When disabled, all scale factors are `1.0`.

Computation order in `RefreshCachedConfig()`:
1. `LoadCrosshairConfig()` computes arm rects with `chScale` only.
2. Auto-scale factors are computed from `floor(hudScale)` and caps.
3. If `scaleCrosshair != 1.0`, arm rects are recomputed with `chScale * scaleCrosshair`.

UI locations:
- Settings dialog: `HUD SCALE` section with enable checkbox + 5 cap spinboxes (global, text, icons, gauges, crosshair)
- Edit mode: `Auto` slider (global cap, `100-800`, step `25`)
- Edit mode snapshot/restore/reset covers all auto-scale keys
- guarded by `s_hudPatchApplied`
- restored automatically when custom HUD is no longer active

### Radar overlay note
The software radar overlay path takes `hunterID` and uses `kBtmOverlaySrcCenterY[hunterID]` to pick the crop center. That means the source crop is intentionally not identical for all hunters.

The radar frame SVG (`res/assets/radar/Rader.svg`, Qt resource `:/mph-icon-radar-frame`) is drawn as a background behind the crop circle. The SVG's `76x76` viewBox matches the actual bottom screen radar art size in DS pixels, not the crop circle diameter. Sizing uses proportional mapping: `kRadarArtSize * dstSize / (srcRadius * 2)` DS pixels, so the frame scales with the overlay but does not match the crop circle boundary. The frame is always shown when the radar is visible. The SVG frame outline can be independently toggled via `BtmOverlayFrameOutlineEnable` (default `true`).

Radar color is independently configurable via `Metroid.Visual.BtmOverlayRadarColor[R/G/B]` (default `#B90005`, the SVG's original stroke color). The HUD Outline settings (`HudOutlineColor*`, `HudOutlineThickness`, `HudOutlineOpacity`) are applied to the frame: a cached outline-colored copy (`s_radarFrameOutline`) is drawn expanded behind the tinted frame (`s_radarFrameTinted`). All three cached images (`s_radarFrame`, `s_radarFrameTinted`, `s_radarFrameOutline`) are invalidated on size/color change.

## MelonPrimeHudRender.h - Public API
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
- `CustomHud_Render()` now covers match status, rank/time, bomb-left HUD, and radar overlay in addition to crosshair/HP/ammo.
