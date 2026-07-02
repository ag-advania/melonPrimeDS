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

Custom HUD integration inside `Screen.cpp` is split into unity include fragments named `MelonPrimeHudScreenCpp*.inc`. These are grouped by call site: shared helpers, panel setup/layout/input, `OverlayOfSoftware`, `OverlayOfGl`, and GL init/deinit. They are not standalone translation units and should not be added to CMake.

### OPT-DR1 — Dirty-Rect Overlay Optimization

**Goal**: avoid per-frame full-window memset and full GL texture upload for the HUD overlay.

**`CustomHud_Render` return value**: returns `QRect` (dirty pixel rect in overlay space, in `Overlay[0]` coordinates). Returns empty `QRect` when nothing was drawn. `void` at all call sites was replaced with capturing this return value.

**Key statics in `MelonPrimeHudRender.cpp`**:
- `s_chPrevPxCx / s_chPrevPxCy` — previous frame crosshair pixel center (`INT_MIN` = unset)
- `s_chDirtyThisFrame` — union of current + previous crosshair bbox, computed in `DrawCrosshair`
- `s_drawnDirtyPx` (**OPT-DR2**) — accumulated device-pixel bbox of every non-crosshair primitive actually drawn this frame; reset at the top of `CustomHud_Render`

**`CrosshairBboxPx(cx, cy, ch)`**: computes exact pixel bbox from stamp (`s_chOutlineStamp`, `s_chStampOrigin`), pre-cached arm rects, and dot rect. Used to union prev + current crosshair positions.

**OPT-DR2 — actual-drawn-rect accumulation** (supersedes the old conservative `RefreshStaticHudDirty()` / `s_staticHudDirtyPx` / `s_staticDirtyStale` machinery, now removed): `AccumDirtyDs(p, dsRect)` maps a DS-space rect through the painter transform and `AccumDirtyDevPx(devRect)` unions an already-device-space rect (both into `s_drawnDirtyPx`, +1px AA pad). They are called from the low-level draw funnels — `DrawCachedText`, the hi-res branch of `DrawCachedTextOutlined`, `DrawImageOutlined`, `DrawGauge` — plus the direct icon / inventory-highlight / radar-frame/outline/crop draw sites. Every visible overlay pixel passes through one funnel, so `s_drawnDirtyPx` is a superset of what changed but uses the *actual* glyph/icon footprint instead of worst-case padding. `CurrentHudDirtyRect(xf)` returns `(s_drawnDirtyPx | s_chDirtyThisFrame) & overlayRect`. No stale-flag / config-change recompute is needed (it is per-frame by construction).

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

### OPT-DR3 — Skip the GL upload when the dirty region is unchanged

`glTexSubImage2D` of the `uploadRect` runs every frame even when the rendered HUD pixels are
identical to the previous frame. The expensive case is holding a **zoom scope** still: the scope
reticle bbox expands `curDirty` to a large region ([MelonPrimeHudRenderConfig.inc](../../src/frontend/qt_sdl/MelonPrimeHudRenderConfig.inc) `CrosshairScopeBboxPx`), so a
multi-MB region is re-uploaded over PCIe every frame for static content.

OPT-DR3 (GL path only, [MelonPrimeHudScreenCppOverlayOfGl.inc](../../src/frontend/qt_sdl/MelonPrimeHudScreenCppOverlayOfGl.inc)) gates the partial
`glTexSubImage2D` on a content hash:

- Statics `s_hudUploadedRectGL` / `s_hudUploadedHashGL` / `s_hudUploadedValidGL` remember the last
  uploaded region and its FNV-1a hash (`MelonPrimeHud_HashImageRegion` in
  [MelonPrimeHudScreenCppHelpers.inc](../../src/frontend/qt_sdl/MelonPrimeHudScreenCppHelpers.inc)).
- Only when `uploadRect` area `>= 256*256` is the hash computed; if the rect and hash both match
  the last upload, the `glTexSubImage2D` is skipped. Small regions upload unconditionally (not
  worth hashing) and reset tracking.
- The composite (`glDrawArrays`) still runs every frame, sampling the unchanged GL texture, so the
  HUD stays drawn. The CPU clear + overlay re-render are unchanged.

Pixel-exact, so it can never leave the HUD stale (no input-signature enumeration). Texture
re-allocation on resize sets `s_hudUploadedValidGL = false`. The software path is **not** covered:
its composite reads the overlay image directly every frame, so there is no separable upload to skip.

Additional Screen-fragment caches:
- `m_hudEnabled` is refreshed by `m_hudCfgEpoch` instead of reading `Metroid.Visual.CustomHUD` every frame.
- `m_radarCfgEpoch` owns GL radar config refresh separately from the top HUD enable cache.
- `m_hudTopMatrix` / `m_hudTopMatrixValid` are updated during layout, so the GL radar path does not scan `screenKind` each frame.
- `m_radarAnchorDsX/Y` are computed when radar config refreshes, not per frame.
- The GL overlay path skips texture upload/composite setup when both previous and current dirty rects are empty, and restores screen GL state only if HUD/radar drawing changed it.

**Removed dead code**: `GetOutlineBuffer()`, `s_outlineBuf`, `s_prevOutlineDirty` — were declared but never called; deleted.

## Config System
Default values live in `src/frontend/qt_sdl/Config.cpp`.

MelonPrime-specific settings are primarily stored under:
- `Instance*.Metroid.*` for per-instance values
- `Metroid.UI.Section*` for persisted UI section state

Important current groups include:
- `Metroid.Visual.CustomHUD`
- `Metroid.Visual.HudFontMode` - HUD font source (int, default 0): 0=bundled MPH, 1=system font, 2=font file
- `Metroid.Visual.HudFontFamily` - system font family name (string, default ""), used when HudFontMode=1
- `Metroid.Visual.HudFontFile` - path to a `.ttf`/`.otf` font (string, default ""), used when HudFontMode=2
- `Metroid.Visual.HudFontSize` - base render px for system/file fonts (int, default 12, clamp 4-64); ignored for MPH (HudFontMode=0, fixed 6px)
- `Metroid.Visual.HudFontWeight` - weight index for system/file fonts (int, default 3=Normal; 0..8 -> Thin..Black, mapped to QFont::Weight)
- `Metroid.Visual.HudFontItalic` / `HudFontUnderline` / `HudFontStrikeOut` - style/effects for system/file fonts (bool, default false)
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

### HUD Property Schema Ownership

HUD visual settings are owned by `src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc`.
It is the single source for `Metroid.Visual.*` HUD keys, default type views, key macros, and
surface coverage metadata. Generated or schema-driven consumers should include this file rather
than spelling HUD config strings by hand.

Ownership flow:

```text
MelonPrimeHudPropSchema.inc
  -> Config.cpp typed defaults via MP_HUD_PROP_SCHEMA_INT/BOOL/STRING/DOUBLE
  -> InputConfig/MelonPrimeInputConfigHudDialogProps.inc dialog rows
  -> MelonPrimeHudConfigOnScreenEditProps.inc edit descriptors
  -> MelonPrimeHudConfigOnScreenSnapshot.inc snapshot/reset fixed keys
  -> MelonPrimeHudRenderConfig.inc runtime load key references
```

HUD geometry helpers live in `src/frontend/qt_sdl/MelonPrimeHudGeometry.h`. The
header is Qt-free and state-free; it owns shared calculations such as 9-point
anchor resolution, aligned text X positions, and gauge relative positioning.
Consumers:

```text
MelonPrimeHudGeometry.h
  -> MelonPrimeHudRenderConfig.inc / MelonPrimeHudRenderDraw.inc runtime helpers
  -> InputConfig/MelonPrimeInputConfigHudPreviews.inc dialog preview helpers
  -> MelonPrimeHudConfigOnScreen*.inc edit/runtime preview code as needed
```

When adding a new HUD element or preview:

1. Add the key/default/surface metadata to `MelonPrimeHudPropSchema.inc` through
   the generator path.
2. Put shared coordinate or alignment math in `MelonPrimeHudGeometry.h` when the
   runtime and preview need the same interpretation.
3. Keep runtime drawing caches and dirty-rect ownership in the runtime render
   fragments; previews may call the same geometry helpers without adopting the
   runtime cache model.
4. Run the CI audits listed in [build.md](build.md), then compare the settings
   dialog preview, in-game edit preview, and runtime HUD visually when the
   element has user-facing rendering.

OSD color rows are a small sub-domain under the HUD schema. `MelonPrimeOsdColorSchema.inc` owns
the message/slot row list and expands into both the settings dialog OSD sections and
`MelonPrimePatchOsdColor.cpp`; it still uses `MP_HUD_PROP_KEY_*` for the actual keys.

To add a new HUD property:

1. Add or regenerate the property in `MelonPrimeHudPropSchema.inc`, preserving the correct
   accessor type (`Int`, `Bool`, `String`, or `Double`) and surface metadata.
2. Wire the intended consumer through an existing schema view or descriptor table, rather than
   adding a new `"Metroid.Visual.*"` literal at the use site.
3. Run `.claude/skills/audit-hud-key-parity.ps1` and the default coverage audit before testing the
   relevant UI/runtime path.

#### Audit Command (PowerShell, Metroid.*)
Checked in as [.claude/skills/audit-config-defaults.ps1](../skills/audit-config-defaults.ps1).
Run from anywhere (it resolves the repo root from its own path):

```powershell
.\.claude\skills\audit-config-defaults.ps1
```

It lists missing defaults by accessor type and catches cross-list type mistakes (the same five
sections as before: `GetXxx missing` for each accessor, plus the four cross-list mismatch
sections). It has no external tool dependency (uses `Get-ChildItem` / `[regex]` instead of
`rg`/`Select-String` patterns that need ripgrep). Exit code is `0` when every section is empty
(fully covered, no cross-list mismatches), `1` otherwise.

## MelonPrime Structural Refactor 2026-06

The Phase 0-8 structural refactor finished on 2026-06-11. Current ownership points:

- Static write-patch lifecycle is centralized in `MelonPrimePatchRegistry.h/.cpp`. New persistent
  write-patches should add a module plus one registry row; `MelonPrime.cpp` should not regain
  per-module apply/restore/reset lists.
- Shared all-or-nothing word patches should use `MelonPrimePatchCommon.h` / `StaticWordPatch`.
  Patch-specific state machines such as mask-selected or canary-based patches may stay custom.
- ROM address definitions live in `MelonPrimeGameRomAddrTable.h` as a single X-macro source of
  truth. The `LIST_*` arrays, `RomAddresses` fields, and `CreateRomAddress()` are generated from
  the same rows.
- Weapon IDs and masks come from `MelonPrimeDef.h`; `MelonPrimeGameWeapon.cpp` should not define a
  second weapon enum.
- Non-HUD settings use `MelonPrimeInputConfig::buildSettingBindings()` for mirrored load/save.
  Manual save/load code is reserved for migrations, dynamic combo data, invalidation-coupled keys,
  and developer-only guarded settings.
- Visible localization tables live in `MelonPrimeLocalization.cpp`; `MelonPrimeLocalization.h`
  exposes the translation API only.
- Historical implementation notes live under `.claude/rules/notes/`, not in `src/frontend/qt_sdl/`.

## Active Branch: `highres_fonts_v3`
Current work is on the `highres_fonts_v3` branch. Main changes relative to `master`:
- Full 9-point anchor system for all HUD element positions
- All `*X`/`*Y` HUD config values are offsets from anchor, not absolute DS-space coordinates
- `ApplyAnchor()` helper in `MelonPrimeHudRender.cpp` is called once per element in `Load*Config()`, transparent to draw functions
- In-game HUD edit mode with drag-and-drop editor, properties panels, crosshair panel with side panels, and live previews
- Edit mode code rooted at `MelonPrimeHudConfigOnScreenUnity.inc` (unity-build included by `MelonPrimeHudRender.cpp`) and split into `.inc` fragments:
  - `MelonPrimeHudConfigOnScreenDefs.inc` - definition tables
  - `MelonPrimeHudConfigOnScreenSnapshot.inc` - snapshot/restore/reset
  - `MelonPrimeHudConfigOnScreenDraw.inc` - bounds and overlay drawing
  - `MelonPrimeHudConfigOnScreenInput.inc` - public edit API and input handling
- Classic settings dialog restored with 5 hierarchical main sections and live preview widgets on the right (except HUD Scale)
- MelonPrime settings/edit-mode labels are localized through `MelonPrimeLocalization.h/.cpp` for English/Japanese based on OS locale
- Programmatic widget architecture via `HudMainSec` / `HudSubSec` / `HudWidgetProp`, enabling data-driven save/restore/TOML-export
- Snapshot/restore covers all HUD widgets plus 3 global fields
- HUD auto-scale system with per-category caps (text, icons, gauges, crosshair); radar excluded from auto-scale
- Property labels use full unabbreviated names throughout the edit mode UI
- Element boxes show live previews (gauge bars, cached icons, sample text) instead of static text labels
- Element box font scales with `HudTextScale`
- OPT-DR1 dirty-rect overlay optimization: `CustomHud_Render` returns `QRect`; only dirty regions cleared/composited/uploaded per frame (see OPT-DR1 section above)
- Runtime HUD code rooted at `MelonPrimeHudRender.cpp` and split into `.inc` fragments:
  - `MelonPrimeHudRenderAssets.inc` - assets, icon/radar/text/outline caches
  - `MelonPrimeHudRenderConfig.inc` - cached config structs/loaders and anchor recomputation
  - `MelonPrimeHudRenderRuntime.inc` - battle state, frame helpers, hide rules, NoHUD patch/cache lifecycle
  - `MelonPrimeHudRenderDraw.inc` - HUD element drawing
  - `MelonPrimeHudRenderMain.inc` - `CustomHud_Render`, radar overlay, edit-mode forward state
- Screen integration code rooted at `Screen.cpp` and split into `MelonPrimeHudScreenCpp*.inc` fragments:
  - `Helpers` fragment for common edit-panel placement, epoch refresh, top overlay clear/render, and patch restore helpers
  - setup/layout/input fragments for edit-mode forwarding and floating panel placement
  - software overlay fragment for `ScreenPanelNative::paintEvent`
  - GL init/deinit/overlay fragments for texture/shader resources, HUD upload/composite, and native radar overlay
- EmuThread integration has small self-contained MelonPrime fragments in `MelonPrimeEmuThread*.inc` for includes, constructor setup, run setup, message queue atomics, and renderer VSync preservation. The frame limiter and frame pacing body remain inline in `EmuThread.cpp`.
- Unity include ownership is checked by `.claude/skills/check-inc-ownership.ps1`; it verifies one parent per unity `.inc`, verifies the fixed parent set for the macro-section `MelonPrimeArm9InstructionHook.inc`, rejects `#include "*.cpp"`, and rejects `.inc` entries in `CMakeLists.txt`
