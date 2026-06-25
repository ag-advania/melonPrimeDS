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

## Input / Aim Latency Path

The input→`RunFrame` path is latency-bound, not throughput-bound. Keep it short, and
keep work that only the *next* frame can observe off it:
- do not issue a syscall on the pre-frame path when a later post-present site can do the
  same work. Stuck-key recovery (`clearStuck*`) and message draining run after `RunFrame` +
  `drawScreen` (`DeferredDrain` / `clearStuckPostFrame`), because their effect is only ever
  read by the next snapshot.
- gate optional per-frame syscalls on a cheap flag instead of running them unconditionally.
  LateLatch re-drains the raw-input buffer only when a `FrameAdvance` opened a wide window
  (`m_didFrameAdvanceSinceSnapshot`); normal frames skip the `GetRawInputBuffer` call.
- early-out the aim path on zero work: a zero mouse delta with zero residual returns before
  any IMUL / clamp / output.

Respect the Single-Writer atomic discipline (one writer thread per Raw Input mode):
- prefer relaxed load + release store over locked RMW (`fetch_or` / `exchange` / CAS).
- load a press/edge slot relaxed first and pay the lock-prefixed op only when it is nonzero;
  consumer-thread-only slots can use plain load + `store(0)`.

See [melonprime-aim-input.md](melonprime-aim-input.md) and
[melonprime-click-handling.md](melonprime-click-handling.md) for the full pipeline and the
P-44 / P-47 / P-48 rationale.

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
- `ProcessAimInputMouse()` skips IMUL / clamp / output on zero-delta frames and returns immediately when delta and residuals are both zero (P-44).
- LateLatch's `GetRawInputBuffer` syscall is gated on `m_didFrameAdvanceSinceSnapshot`, so normal in-game frames skip it (P-47).
- `clearStuck*` and message draining run post-present in `DeferredDrain` / `clearStuckPostFrame`, off the input→`RunFrame` latency path (P-48b).
