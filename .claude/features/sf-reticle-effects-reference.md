# SF Reticle Effects Reference

Reference catalog for SF movie / tactical HUD / scope UI effects. Use when
extending zoom crosshair transition styles or future lock-on HUD work.

**Runtime rule:** overlay FX must not multiply `scopeT` (scope geometry). Pulse,
glow, and breathing belong in `DrawZoomTransitionFx()` only. Scope reticle
radius follows zoom progress alone.

## Zoom transition style map (0–12)

| ID | Style | Catalog preset / effects used |
|---|---|---|
| 0 | Fade | Opacity crossfade only |
| 1 | Staged | Original staged blend + optional pulse ring |
| 2 | Glitch | RGB break, slices, scan bars, blocks, noise, pixel grid |
| 3 | Glitch2 | Scan-heavy glitch overload + scan disturb + corner brackets |
| 4 | Snap | Instant scope pop |
| 5 | Digital | Pixel crush + glitch noise/grid |
| 6 | Pulse Wave | Pulse ring (optional) |
| 7 | Magic Circle | Rotating arc segments |
| 8 | SF Movie | Lock rings, orbit, radar sweep, rangefinder, brackets |
| 9 | Tactical Lock | Target box + lock rings + brackets |
| 10 | Sniper Optics | Iris aperture + focus brackets + rangefinder |
| 11 | Drone LIDAR | LIDAR point cloud + wireframe + material grid |
| 12 | Beam Charge | Charge ring + railgun lines + energy surge |

Helpers live in `MelonPrimeHudRenderCrosshairFx.inc`.

## Design constraints

- Keep center readable; avoid constant heavy glitch on the reticle itself.
- Limit HUD density: center, range, lock state, threat type are enough inline.
- Hit FX on target/frame, not large reticle displacement.
