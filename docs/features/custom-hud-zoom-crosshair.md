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
- `Metroid.Visual.CrosshairZoomTransitionStyle`, default `0` (Fade), range `0..12`
  - `0` Fade, `1` Staged, `2` Glitch, `3` Glitch2, `4` Snap, `5` Digital,
    `6` Pulse Wave, `7` Magic Circle, `8` SF Movie, `9` Tactical Lock,
    `10` Sniper Optics, `11` Drone LIDAR, `12` Beam Charge
- `Metroid.Visual.CrosshairZoomTransitionPulseStrength`, default `38`, range `0..100`
- `Metroid.Visual.CrosshairZoomScopeRadius`, default `128`, range `4..1024`
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
See also [Zoom status performance](zoom-status-performance.md) for the shared
hot-path rules behind this design.

Zoom state is updated once per emulated game frame, then
`ReadCrosshairZoomAmount()` returns the cached amount during rendering:
- scoped bit from `player + 0x850`
- current weapon pointer from `player + 0x858`
- cached CanZoom flag from `weapon + 0x08`, bit `0x800`

Visibility is gated by `scoped && cachedCanZoom`, with a 2-game-frame debounce.
When scoped is false, weapon data is not read. When scoped is true, CanZoom is
re-read only if `player + 0x858` points to a different weapon than the cached
one. The renderer no longer reads zoom FOV or HUD animation frame state; host
smoothing in `DrawCrosshair()` handles visual zoom-in/zoom-out from the cached
0/1 amount. This keeps charge-shot/shoot animation noise from flashing the zoom
reticle without doing per-draw memory polling.

Crosshair zoom geometry that does not change per frame is cached in
`CrosshairHudConfig`, including the scope reticle bbox.
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
   `CrosshairZoomTransitionStyle` (Fade, Staged, Glitch, Glitch2, Snap,
   Digital, Pulse Wave, Magic Circle, SF Movie, Tactical Lock,
   Sniper Optics, Drone LIDAR, Beam Charge).
   Fade uses opacity-only blending (no darkening or flash overlays).
   Glitch uses AniGlitchArtFX-inspired RGB break, horizontal slices, scan bars,
   scanlines, block collapse, noise, and pixel grid (cyan #42e8ff / magenta #ff4fd8).
   Magic Circle draws rotating arc segments and rune ticks.
   SF Movie adds lock-on convergence rings, orbit ring, radar sweep,
   rangefinder ticks, chromatic glow, and corner brackets (overlay only —
   scope reticle size is never modulated by FX pulse).
   Styles 9–12 are SF catalog presets: Tactical Lock, Sniper Optics, Drone LIDAR,
   Beam Charge.
   See also `docs/features/sf-reticle-effects-reference.md` for the full catalog.

Toggle `CrosshairZoomTransitionEnable` to disable all transition animation and
revert to instant crosshair↔scope switching (geometry/opacity still follow zoom
stage values, but without smoothing or staged curves).

After the crosshair stops drawing (death, HUD hide, third-person, transform), the
display zoom snaps to the current target on the next draw so respawn does not
replay a zoom-out animation.

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
extend outside the ring). Arms reach from the configured gap to the circle edge.
The former `CrosshairZoomScopeLineLength` setting was removed (it was dead: the
arm end was always the scope radius, never this value).

The same zoom amount must be passed to `DrawCrosshair()` from both:
- normal Custom HUD render path
- on-screen HUD edit preview path

## Verification
- `tools/ci/audits/audit-hud-key-parity.ps1`
- `tools/build/windows/build-mingw.bat --tail 120`

## Caveats
Existing user config may already have `CrosshairZoomStageEnable=false`. In that
case, new defaults will not override the saved value; the user must enable the
Zoom Stage toggle for the crosshair to react to zoom.
Existing user config may also have `CrosshairZoomOpacity=100`; set it to `0` to
hide the base crosshair while fully zoomed.
