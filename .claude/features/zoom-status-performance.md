# Zoom Status Performance

## Purpose
Shared runtime notes for zoom-state consumers:
- Custom HUD zoom crosshair
- zoom aim sensitivity

This path is hot because it can run during input and HUD rendering. Keep it
small, predictable, and biased toward the common unscoped case.

## Performance Rules

Use the cheapest reliable gate first:
- Read local player pointer.
- Read `player + 0x850` scope bit.
- If scoped is false, stop. Do not read weapon data.

Only scoped frames may inspect the current weapon:
- Read `player + 0x858` weapon pointer.
- If it matches the cached weapon pointer, reuse cached CanZoom.
- If it changed, read `weapon + 0x08` once and cache bit `0x800`.

Visibility uses:
```text
rawVisible = scoped && cachedCanZoom
```

Do not put these reads back on the hot path:
- `weapon + 0x54` zoom FOV
- `crosshairControl + 0x04` HUD animation active
- `crosshairControl + 0x06` HUD current frame

Those were safer but heavier. The current design relies on the native scope bit,
cached CanZoom, and a 2-emulated-game-frame debounce to reject charge-shot /
shoot / weapon-switch animation noise.

## Update Cadence

Update zoom state once per emulated game frame, not once per draw:
- `UpdateCrosshairZoomAmountForGameFrame()` is keyed by `NDS::NumFrames`.
- 120/240Hz presentation should reuse the cached zoom amount for repeated draws
  of the same emulated frame.
- Debounce timers are game-frame timers. Do not advance them per render frame.
- Custom HUD display smoothing (`s_chDisplayZoom`) is also keyed by
  `NDS::NumFrames`. Repeated draws of the same emulated frame reuse the same
  visual zoom amount, so a 2-frame transition remains 2 game frames instead of
  shrinking at high presentation rates.

Rendering should consume cached state:
- `ReadCrosshairZoomAmount()` returns the cached amount only.
- Drawing code owns visual smoothing and staged curves.
- ZoomStatus owns memory reads and boolean visibility.

## Cache Invalidation

`ZoomCapabilityCache` is keyed by both player and weapon:
- player change clears the weapon/CanZoom cache
- weapon pointer change refreshes CanZoom
- invalid local player clears the caller's zoom state

When the HUD stops drawing because of death, pause, third-person, or transform,
reset the reticle display state so respawn/re-entry does not replay stale zoom.

## Design Bias

Prefer these patterns:
- branch early for the common false case
- cache stable RAM-derived facts
- convert percentages/scales to fixed-point outside aim math
- keep render code as a cached-state consumer
- use game-frame counters for gameplay debounce

Avoid these patterns:
- per-draw RAM polling
- using HUD animation frames as visibility truth
- adding floating-point sensitivity math inside `ProcessAimInputMouse()`
- re-reading weapon flags when the weapon pointer is unchanged
