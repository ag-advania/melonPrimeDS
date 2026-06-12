# Custom HUD Zoom Crosshair

## Purpose
Lets the Custom HUD crosshair change while zooming. The default behavior enables
the zoom stage and draws a sniper-style scope reticle when fully scoped.

## HUD Schema
Boolean keys:
- `Metroid.Visual.CrosshairZoomStageEnable`, default `true`
- `Metroid.Visual.CrosshairZoomScopeEnable`, default `true`

Numeric keys:
- `Metroid.Visual.CrosshairZoomScale`
- `Metroid.Visual.CrosshairZoomOpacity`, default `0` (base crosshair hidden while fully zoomed)
- `Metroid.Visual.CrosshairZoomScopeRadius`, default `128`, range `4..1024`
- `Metroid.Visual.CrosshairZoomScopeLineLength`
- `Metroid.Visual.CrosshairZoomScopeGap`, default `0`
- `Metroid.Visual.CrosshairZoomScopeThickness`
- `Metroid.Visual.CrosshairZoomScopeCenterDot`, default `true`
- `Metroid.Visual.CrosshairZoomScopeDotSize`, default `2`
- `Metroid.Visual.CrosshairZoomScopeDotOpacity`, default `100`
- `Metroid.Visual.CrosshairZoomScopeOpacity`

All Custom HUD visual keys should be added through
`src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc` and exposed consistently to
defaults, dialog properties, edit mode, side panel, and runtime load.

## Runtime
Zoom amount is read in `ReadCrosshairZoomAmount()`:
- scoped bit from `player + 0x850`
- exact HUD animation state from `crosshairControl + 0x04`
- exact HUD current frame from `crosshairControl + 0x06`

If the HUD animation flag is inactive, the renderer falls back to the scoped bit.
This avoids treating a stable `currentFrame == 0` as "not zoomed" after zoom has
already completed.

For non-animated frames, the renderer returns from scoped state directly and skips
the current-frame read. Crosshair zoom geometry that does not change per frame is
cached in `CrosshairHudConfig`, including the scope reticle bbox.
The scope reticle is clipped to the top-screen content rectangle so it does not
draw into letterbox/pillarbox areas. Scope reticle radius, line length, gap,
thickness, and center-dot size are part of the crosshair auto-scale category and
use `HudAutoScaleCapCrosshair`.

The same zoom amount must be passed to `DrawCrosshair()` from both:
- normal Custom HUD render path
- on-screen HUD edit preview path

## Verification
- `.claude/skills/audit-hud-key-parity.ps1`
- `.claude/skills/build-mingw.bat --tail 120`

## Caveats
Existing user config may already have `CrosshairZoomStageEnable=false`. In that
case, new defaults will not override the saved value; the user must enable the
Zoom Stage toggle for the crosshair to react to zoom.
Existing user config may also have `CrosshairZoomOpacity=100`; set it to `0` to
hide the base crosshair while fully zoomed.
