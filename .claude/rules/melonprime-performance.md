# MelonPrime Performance Rules

Use this as the default performance model for MelonPrime gameplay and HUD work.

## Update Cadence

Separate game-frame state updates from presentation-frame drawing:
- gameplay-derived state should update once per emulated game frame
- repeated 120/240Hz draws of the same `NDS::NumFrames` should consume cached state
- debounce timers, short transitions, random-effect phases, and aim/HUD smoothing should not advance per presentation frame unless the feature is explicitly visual-only

Prefer this ownership split:
- `Update...ForGameFrame(..., nds->NumFrames)` reads RAM and advances gameplay timers
- `Draw...()` consumes cached values and performs only drawing work
- caches are keyed by `NDS*`, `MainRAM`, `NDS::NumFrames`, player offset, and ROM group when those can affect the read result

## Hot-Path Shape

Keep common cases cheap:
- branch early for disabled features and common false states
- test the cheapest reliable byte/flag before reading wider or dependent structures
- avoid reading weapon/player substructures until an outer state proves they are needed
- cache stable facts by pointer or frame number instead of re-reading them per draw

Keep math out of inner loops:
- precompute percentages, fixed-point scales, colors with alpha, bboxes, and stamps when config changes
- move expensive painter setup, image generation, dilation, text rasterization, and lookup-table construction out of per-frame paths
- use dirty rects and actual drawn bboxes so unchanged pixels are not cleared, composited, or uploaded

## Invalidations

Every cache needs a clear invalidation owner:
- config change invalidates config-derived caches
- ROM/player/weapon pointer change invalidates RAM-derived capability caches
- death, pause, third-person, transform, emulator stop, and HUD disabled paths must reset visual state that would otherwise replay later
- frame caches naturally expire when `NDS::NumFrames` changes

## Current Examples

- Custom HUD zoom amount, crosshair DS aim position, and `s_chDisplayZoom` are game-frame keyed.
- Custom HUD runtime state caches base/adventure/visible reads within a game
  frame, and lazily caches optional element values such as ammo, owned weapons,
  bombs, match status, rank, and time only when those elements are drawn.
- Zoom status uses scope bit + cached CanZoom and intentionally avoids zoom FOV / HUD animation reads on the hot path.
- Aim sensitivity stores an effective scale and keeps floating-point percentage math out of `ProcessAimInputMouse()`.
