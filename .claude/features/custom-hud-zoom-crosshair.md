# Custom HUD Zoom Crosshair

## Purpose
Lets the Custom HUD crosshair change while zooming. The default behavior enables
the zoom stage and draws a sniper-style scope reticle when fully scoped.

## HUD Schema
Boolean keys:
- `Metroid.Visual.CrosshairZoomStageEnable`, default `true`
- `Metroid.Visual.CrosshairZoomScopeEnable`, default `true`
- `Metroid.Visual.CrosshairZoomTransitionEnable`, default `true`
- `Metroid.Visual.CrosshairZoomTransitionPulseEnable`, default `false`

Numeric keys:
- `Metroid.Visual.CrosshairZoomScale`
- `Metroid.Visual.CrosshairZoomOpacity`, default `0` (base crosshair hidden while fully zoomed)
- `Metroid.Visual.CrosshairZoomTransitionSpeed`, default `100`, range `25..400`
- `Metroid.Visual.CrosshairZoomTransitionStyle`, default `0` (Staged), range `0..18`
  - `0` Staged, `1` Fade, `2` Glitch, `3` Snap, `4` Expand, `5` Contract,
    `6` Scanline, `7` Digital, `8` Pulse Wave, `9` Crossfade, `10` Magic Circle,
    `11` SF Movie, `12` Tactical Lock, `13` Sniper Optics, `14` Drone LIDAR,
    `15` Cyber Jam, `16` Beam Charge, `17` Wireframe, `18` Data Link
- `Metroid.Visual.CrosshairZoomTransitionPulseStrength`, default `38`, range `0..100`
- `Metroid.Visual.CrosshairZoomScopeRadius`, default `128`, range `4..1024`
- `Metroid.Visual.CrosshairZoomScopeLineLength`
- `Metroid.Visual.CrosshairZoomScopeGap`, default `0`
- `Metroid.Visual.CrosshairZoomScopeThickness`
- `Metroid.Visual.CrosshairZoomScopeCenterDot`, default `true`
- `Metroid.Visual.CrosshairZoomScopeDotSize`, default `2`
- `Metroid.Visual.CrosshairZoomScopeDotOpacity`, default `100`
- `Metroid.Visual.CrosshairZoomScopeDotCustomColor`, default `false`
- `Metroid.Visual.CrosshairZoomScopeDotColorR/G/B`, default `255/255/255`
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

### Zoom transition (staged blend)
When zoom stage is enabled, the renderer no longer switches base crosshair and
scope reticle in one step. Instead:

1. **Host smoothing** — `AdvanceDisplayZoom()` lerps a display value toward the
   RAM target. Speed is controlled by `CrosshairZoomTransitionSpeed` (25–400%,
   default 100 = ~8 overlay frames for a full sweep).
2. **Staged curves** — `ComputeCrosshairZoomBlend()` applies smoothstep easing:
   - base crosshair scale fades on an early curve
   - base opacity drops in the second half of the transition
   - scope reticle radius/line length expand on a delayed curve (starts ~28% in)
3. **Transition FX** — style-specific overlays during mid-transition. The pulse
   ring (styles that use it) is gated by `CrosshairZoomTransitionPulseEnable` and
   scaled by `CrosshairZoomTransitionPulseStrength`. Pick the overall feel with
   `CrosshairZoomTransitionStyle` (Staged, Fade, Glitch, Snap, Expand, Contract,
   Scanline, Digital, Pulse Wave, Crossfade, Magic Circle, SF Movie, Tactical Lock,
   Sniper Optics, Drone LIDAR, Cyber Jam, Beam Charge, Wireframe, Data Link).
   Fade and Crossfade use opacity-only blending (no darkening or flash overlays).
   Glitch uses AniGlitchArtFX-inspired RGB break, horizontal slices, scan bars,
   scanlines, block collapse, noise, and pixel grid (cyan #42e8ff / magenta #ff4fd8).
   Magic Circle draws rotating arc segments and rune ticks.
   SF Movie adds lock-on convergence rings, orbit ring, radar sweep,
   rangefinder ticks, chromatic glow, and corner brackets (overlay only —
   scope reticle size is never modulated by FX pulse).
   Styles 12–18 are SF catalog presets: Tactical Lock (target box + lock rings),
   Sniper Optics (iris aperture + focus brackets + rangefinder), Drone LIDAR
   (point cloud + material grid), Cyber Jam (scan disturb + glitch jitter),
   Beam Charge (charge ring + railgun lines + energy surge),
   Wireframe (wireframe box + material grid),
   Data Link (data HUD strip + hit probability arc + vertical scan).
   See also `.claude/features/sf-reticle-effects-reference.md` for the full catalog.

Toggle `CrosshairZoomTransitionEnable` to disable all transition animation and
revert to instant crosshair↔scope switching (geometry/opacity still follow zoom
stage values, but without smoothing or staged curves).

After the crosshair stops drawing (death, HUD hide, third-person, transform), the
display zoom snaps to the current target on the next draw so respawn does not
replay a zoom-out animation.

Game-side frame mapping in `ComputeReticleAmount()` uses smoothstep over frames
0–4 (zoom in) and 0x10–0x14 (zoom out) when the HUD animation flag is active.

Settings UI keys (Custom HUD tab + in-game edit mode):
- Normal crosshair section: color, scale, outline, center dot, T-style
- Zoom crosshair section: zoom stage/scale/opacity, scope reticle, scope dot color,
  transition enable/style/speed, pulse ring/strength
- Inner / outer line sections (unchanged)
- `Metroid.Visual.CrosshairZoomTransitionEnable`
- `Metroid.Visual.CrosshairZoomTransitionStyle`
- `Metroid.Visual.CrosshairZoomTransitionSpeed`
- `Metroid.Visual.CrosshairZoomTransitionPulseEnable`
- `Metroid.Visual.CrosshairZoomTransitionPulseStrength`
- `Metroid.Visual.CrosshairZoomScopeDotCustomColor`
- `Metroid.Visual.CrosshairZoomScopeDotColorR/G/B`

Scope reticle cross lines are clipped to the scope radius circle (they do not
extend outside the ring). Arms reach from the configured gap to the circle edge;
`CrosshairZoomScopeLineLength` is kept for legacy configs but no longer extends
lines outside the ring.

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
