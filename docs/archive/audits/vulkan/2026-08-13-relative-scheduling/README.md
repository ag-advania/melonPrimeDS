# Vulkan relative-time scheduling — runtime evidence, 2026-08-13

Logs from the session that unblocked Phase 3 A/B. Before this change, the
`JustInTime` policies could not schedule at all on this driver: the surface
reports `presentAtAbsoluteTimeSupported = false`, so A2 and A3 behaved exactly
like A1 and an A/B would have measured nothing.

Environment: RTX 5070 Ti, driver 610.74.0.0, Vulkan loader 1.4.357,
`VK_LAYER_KHRONOS_validation` enabled, Debug build via
`tools/build/windows/build-mingw-validation.bat`. Every run loaded a ROM and
presented; all four contain zero `Validation Error`, `Validation Warning`,
`VUID-` and `SYNC-` lines.

| File | What it shows |
|---|---|
| `relative-p2-steady.log` | `JustInTime` reaching `targetMode=relative`, `state=TargetSchedulingActive`, and the steady-state cadence |
| `relative-p3-fifo-latest-ready.log` | `JustInTimeFifoLatestReady` selecting `FIFO_LATEST_READY` with relative scheduling — the A3 unblock condition |
| `relative-vsync-off-control.log` | VSync off → `IMMEDIATE`, `targetMode=none`, `fallback=present mode is not FIFO` |
| `sync-reflex-authority.log` | Reflex on → `authority=NvidiaReflex`, `targetMode=none`: the vendor rule still suppresses relative |

## The cadence, from `relative-p2-steady.log`

```text
jit=active  lastTarget=14885600  frameIntervalNs=16666667
refreshIntervalNs=1859000  dynamics=FRR  queueFull=0  fallback=none
```

`14,885,600 = 8 x 1,860,700` — a whole number of refresh intervals, which is what
the quantizer must produce on a fixed-refresh display.

The emulator's interval divided by that refresh is **8.9572**, so the base is 8
quanta and the fractional accumulator carries 1,781,067 ns of remainder per
frame. That remainder is 0.9572 of a refresh, so the accumulator crosses the
threshold on **about 95.7% of frames**: the steady cadence is 9 quanta most of
the time with an 8-quanta frame roughly every 23rd frame. The sample above is
one of those 8-quanta frames.

Both fixed alternatives are worse, in opposite directions:

| Choice | Duration | Error vs 16,666,667 ns |
|---|---:|---:|
| fixed 9 (`round(8.9572)`) | 16,746,300 | +0.478% (slow) |
| fixed 8 (truncate) | 14,885,600 | -10.686% (fast) |

The accumulator gives neither: it spends 9 quanta 95.7% of the time and 8 the
rest, so the long-run average is the emulator's own interval and the accumulated
error never exceeds one refresh. That invariant — not any particular pattern —
is what `TestRelativeCadence60On144` asserts.

(The refresh interval drifts slightly between samples — 1,859,000 vs 1,860,700 —
because the driver bumps `timingPropertiesCounter` and the pacer re-reads it.
The logged interval is the current one; the logged target came from the present
that last changed the scheduling state, so a summary line alone cannot prove the
target is a multiple of the interval printed beside it. The A/B capture records
the refresh interval each target was generated against, together with the quanta
and the accumulator either side of it, precisely so that
`target_value_ns == relative_quanta x target_generation_refresh_interval_ns`
can be checked per present after the fact.)

## Synchronization validation

A separate pass with `khronos_validation.validate_sync = true` covered policies
2 and 3 in relative mode plus Reflex on: zero hazards. The proof that the
setting was actually applied is in the previous session's evidence folder
(`../2026-08-13-validation/sync-enabled-banner.log`) — an empty log is only
meaningful once the layer has been seen to report `Synchronization` among its
enabled checks.

## Not covered

The fullscreen/resize/DPI/minimize event matrix, F2 and renderer switching,
speed modes, and all latency measurement. Relative scheduling is **implemented
and active**, which is not the same as **beneficial**: that needs the A/B in
[`../../../../development/testing/vulkan-present-pacing-runbook.md`](../../../../development/testing/vulkan-present-pacing-runbook.md).
