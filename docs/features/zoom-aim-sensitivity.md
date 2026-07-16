# Zoom Aim Sensitivity

## Purpose
Adds an optional aim sensitivity multiplier while the player is zoomed/scoped.
The default multiplier is 75%, and the feature is off until enabled by the user.

## Settings
- `Metroid.Aim.ZoomScale.Enable`: bool, default `false`
- `Metroid.Aim.ZoomScale.Percent`: int, default `75`, range `10..300`

The settings UI lives in `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp`.
The reset button should restore the percent to `75`.

## Runtime
See also [Zoom status performance](zoom-status-performance.md) for the shared
hot-path rules behind this design.

Zoom state is read from MPH runtime state through `MelonPrime::ZoomStatus`:
- local player pointer from `RomAddresses::hookLocalPlayerPtrGlobal`
- scope flag at `player + 0x850`, bit `0`
- current weapon pointer at `player + 0x858`
- cached CanZoom flag at `weapon + 0x08`, bit `0x800`

The runtime multiplier uses `scope && cachedCanZoom`, not the scope bit alone,
so stale scope state on non-zoom weapons does not keep aim scaled. When scoped is
false, weapon data is not read. While scoped, CanZoom is re-read only when the
current weapon pointer changes.

Aim scaling is cached as Q14 fixed-point:
- base aim scale: `m_aimFixedScaleX/Y`
- effective aim scale: `m_aimEffectiveFixedScaleX/Y`
- configured zoom scale: `m_zoomAimScaleQ14`
- active runtime scale: `m_activeZoomAimScaleQ14`

`ProcessAimInputMouse()` and the native aim delta hook read the effective scale
directly. They only recalculate the effective scale when the scoped state changes,
so the hot path does not apply an extra zoom multiplier every mouse input.

If the configured percent resolves to exactly 100%, runtime treats the feature as
disabled even if the checkbox is on. This avoids reading zoom state for a no-op
scale.

## Verification
- `tools/ci/audits/audit-config-defaults.ps1`
- `tools/build/windows/build-mingw.bat --tail 120`

## Caveats
Do not add per-frame floating-point sensitivity math to the aim hot path. Keep new
scale factors precomputed in config/reload code or in zoom-state edge updates.
