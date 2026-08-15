# Vulkan present pacing — Validation Layer session, 2026-08-13

Raw evidence for the first Validation Layer run of the vendor-neutral present
pacing path. Kept because "nothing appeared on screen" is not a validation
result: these are the logs the PASS was read from, so the claim can be
re-checked later without re-running the session.

Tested commit: `945823c7a23c8e3b7af8767f6a02137f8860cb1a`
(the VUID fix these logs verify landed in `639a6e8b6`).

Environment: RTX 5070 Ti, driver 610.74.0.0, Vulkan loader 1.4.357, device API
1.4.341, `VK_LAYER_KHRONOS_validation` from SDK 1.4.357.0, Windows 11.
Debug build via `tools/build/windows/build-mingw-validation.bat`.

| File | Configuration |
|---|---|
| `core-p0.log` | `TelemetryOnly`, Reflex off, VSync on |
| `core-p1.log` | `PresentWait`, Reflex off, VSync on |
| `core-p2.log` | `JustInTime`, Reflex off, VSync on |
| `core-p3.log` | `JustInTimeFifoLatestReady`, Reflex off, VSync on |
| `core-reflex-on.log` | `JustInTime`, Reflex on, VSync on |
| `core-vsync-off.log` | `JustInTime`, Reflex off, **VSync off** |
| `sync-enabled-banner.log` | proof that synchronization validation was actually enabled |

Every run loaded a ROM and presented for 40-50 seconds. Grep for
`Validation Error`, `Validation Warning`, `VUID-` or `SYNC-` — all seven files
contain none.

`sync-enabled-banner.log` exists because an empty validation log proves nothing
on its own: it looks identical whether the layer found no problems or the
settings file was never read. That run added `info` to `report_flags` so the
layer printed which checks it had enabled, and **Synchronization** is in the
list. The synchronization pass itself (policies 0/2/3 and Reflex On+Boost)
produced zero output, which is why there is no log for it here.

What these logs do **not** cover: the fullscreen/resize/DPI/minimize event
matrix, F2 and renderer switching, speed modes, and all latency measurement.
Those need a person driving the emulator. See
[`../../../../development/testing/vulkan-present-pacing-runbook.md`](../../../../development/testing/vulkan-present-pacing-runbook.md).
