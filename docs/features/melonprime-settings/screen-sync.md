# Screen sync mode

## Purpose

Screen Sync Mode adds an optional host-side synchronization call after the
renderer work for a frame. It is intended to trade latency against visible
tearing/choppiness. It does not patch the ROM, change guest timing, or
guarantee a monitor refresh rate.

| Control | Key | Default | Values |
| --- | --- | --- | --- |
| Screen Sync Mode | Metroid.Screen.SyncMode | 0 | 0 Off, 1 glFinish, 2 DwmFlush |

## Modes

| Value | Name | Behavior |
| ---: | --- | --- |
| 0 | Off | No extra sync call |
| 1 | glFinish | Wait for OpenGL commands to complete |
| 2 | DwmFlush | Wait for the Windows DWM compositor |

DwmFlush is Windows-only. On non-Windows platforms the UI removes the item
and normalizes a stored value of 2 to Off. Documentation or test scripts must
not report DwmFlush as active on Linux or macOS merely because an old config
file contains the integer 2.

## Fast-forward and slow-motion rule

The sync call is automatically disabled while FastForward or SlowMo is active.
The configured mode remains stored, so returning to normal speed can re-enable
the selected sync behavior without changing the setting.

This creates two distinct test conditions:

- configured mode: the value in Metroid.Screen.SyncMode; and
- effective mode: configured mode after platform and speed-state gating.

Only the effective mode describes the call made for a particular frame.

## Cost and interpretation

Off generally gives the lowest synchronization overhead but can look uneven
depending on the renderer and display path. glFinish can reduce visible
rendering overlap at the cost of waiting for the GPU. DwmFlush waits on the
Windows compositor and may have different behavior from a GPU completion wait.

The setting is not a replacement for VSync, the FPS limiter, renderer
selection, or a present/pacing policy. Keep those values constant in an A/B
test. A lower measured FPS after enabling sync is an expected cost signal, not
by itself a correctness failure.

## Lifecycle

The setting is read by the host emulation/rendering thread. Configuration
reload updates the runtime snapshot; no guest match join or patch registry
operation is required. There is no ARM9 address table for this feature.

## Verification checklist

- Test Off, glFinish, and DwmFlush on supported platforms.
- Confirm DwmFlush is absent/normalized on non-Windows.
- Test normal speed and FastForward/SlowMo separately.
- Record VSync, FPS limit, renderer, display refresh, and active platform.
- Confirm no guest RAM or executable instruction changes.
- Measure latency and frame pacing with enough frames to distinguish startup
  behavior from steady state.

## Evidence and related material

Current source:

- MelonPrime.h
- MelonPrime.cpp
- EmuThread.cpp and its renderer update fragments
- InputConfig/MelonPrimeInputConfig.cpp

Related host presentation documentation:

- docs/architecture/srp-performance-contract.md
- docs/features/melonprime-settings/video-presets.md

There is no guest reverse-engineering report to copy for this host-only
setting. mphCodex address research is therefore not a source for Screen Sync
Mode.
