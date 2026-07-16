# Platform Safety

## General

- Keep platform dispatch behind `MelonPrimePlatformInput.h` and platform-owned translation units. New Windows/macOS/Linux branching in common input/runtime code consumes the platform-scatter ratchet and needs explicit review.
- GUI and cursor APIs belong on the GUI thread. Platform callbacks may accumulate deltas/state but must not mutate Qt widgets directly.
- Mouse buttons and keyboard hotkeys remain on the Qt/SDL path; native raw-motion backends must not create duplicate press edges.

## Windows

- RawInput is the primary relative-aim path. Preserve cursor clip/unclip and focus-loss cleanup.
- Windows-only sources and APIs remain guarded and excluded from non-Windows builds.

## macOS

- Input backend order and exclusivity are GCMouse, then IOHID when needed, then QCursor fallback. Never double-count backend deltas.
- Use the MelonPrime macOS warp/capture helpers, not `QCursor::setPos`, for the in-game capture path. External GCMouse capture and built-in trackpad behavior differ; preserve release-event recovery.
- AppKit, GameController, IOHID, Metal, and window mutations must respect their documented thread and lifetime rules.
- The compute renderer restriction/fallback must remain visible and truthful in settings; never silently claim native Metal support when the active path is compatibility/fallback.

## Linux

- X11 relative aim uses XInput2 raw motion. Absolute tablet valuators are differenced per device; axes above X/Y are not aim input.
- XWayland/Wayland may not deliver raw motion. Preserve the Qt previous-position-differencing fallback.
- Use `LinuxWarpCursorGlobal` on X11 and reseed delta baselines after every warp. Do not reintroduce center-delta plus warp-per-event drift.
- Focus loss and Escape must always release confinement and restore the cursor.

Details: [`docs/architecture/input/aim-input.md`](../../docs/architecture/input/aim-input.md), [`docs/features/rendering/macos-compute-renderer-restriction.md`](../../docs/features/rendering/macos-compute-renderer-restriction.md).
