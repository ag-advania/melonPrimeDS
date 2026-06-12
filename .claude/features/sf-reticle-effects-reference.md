# SF Reticle Effects Reference

Reference catalog for SF movie / tactical HUD / scope UI effects. Use when
extending zoom crosshair transition styles or future lock-on HUD work.

**Runtime rule:** overlay FX must not multiply `scopeT` (scope geometry). Pulse,
glow, and breathing belong in `DrawZoomTransitionFx()` only. Scope reticle
radius follows zoom progress alone.

## Zoom transition style map (0–18)

| ID | Style | Catalog preset / effects used |
|---|---|---|
| 0 | Staged | Original staged blend + optional pulse ring |
| 1 | Fade | Opacity crossfade only |
| 2 | Glitch | Psycho-Pass-like slices + chromatic rings |
| 3 | Snap | Instant scope pop |
| 4 | Expand | Radial expand |
| 5 | Contract | Radial contract |
| 6 | Scanline | Horizontal + vertical scan |
| 7 | Digital | Pixel / digital reveal |
| 8 | Pulse Wave | Pulse ring (optional) |
| 9 | Crossfade | Opacity blend, no flash |
| 10 | Magic Circle | Rotating arc segments |
| 11 | SF Movie | Lock rings, orbit, radar sweep, rangefinder, brackets, chromatic glow |
| 12 | Tactical Lock | Target Box Acquisition + Lock-on Ring Convergence + corner brackets + hit probability |
| 13 | Sniper Optics | Optical Zoom Aperture + Focus Bracket Breathing + rangefinder |
| 14 | Drone LIDAR | LIDAR Point Cloud + Material Analysis Grid + wireframe box |
| 15 | Glitch2 | Scan-heavy glitch overload + scan disturb + corner brackets |
| 16 | Beam Charge | Charge Ring Fill + Railgun Alignment Lines + Energy Surge Pulse |
| 17 | Wireframe | Wireframe box + material grid |
| 18 | Data Link | Data HUD strip + Hit Probability arc + soft lock halo + converging rings |

Helpers live in `MelonPrimeHudRenderCrosshairFx.inc`.

## Still-unimplemented high-priority effects

Good next additions if more styles are needed:

1. Lead Indicator / Predictive Ghost Reticle
2. Hit Confirm Pulse (on fire, not zoom)
3. Jammed Reticle Tear
4. Sensor Reboot Sweep
5. Hard Lock Seal / Typewriter LOCKED text
6. IFF Identification / Threat Classification Label
7. Multi Target Tagging / Lock Chain Cascade
8. Missile Lock Cascade
9. EMP Whiteout / HUD Desync Shift
10. Digital Magnification Step (numeric zoom steps)

## Preset bundles (original catalog)

### Cinematic sniper HUD → style 13
Optical Zoom Aperture → Focus Bracket Breathing → Rangefinder ticks →
Lead Indicator → Hit Probability → Recoil Stabilizer

### AI tactical lock-on → style 12
Target Box → Ring Convergence → corner brackets → hit probability arc

### Drone / robot POV → style 14
LIDAR Point Cloud → Wireframe → Material grid

### Glitch2 (scan overload) → style 15
Scan Disturb → Glitch jitter → Signal Dropout

### Energy weapon → style 16
Charge Ring Fill → Energy Surge Pulse → Railgun Alignment Lines

## Design constraints

- Keep center readable; avoid constant heavy glitch on the reticle itself.
- Limit HUD density: center, range, lock state, threat type are enough inline.
- Avoid magic-circle ornament on SF styles; prefer ticks, segments, numerics.
- Hit FX on target/frame, not large reticle displacement.
