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
Zoom state is read from MPH runtime state through `MelonPrime::ZoomStatus`:
- local player pointer from `RomAddresses::hookLocalPlayerPtrGlobal`
- scope flag at `player + 0x850`, bit `0`

Aim scaling is cached as Q14 fixed-point:
- base aim scale: `m_aimFixedScaleX/Y`
- effective aim scale: `m_aimEffectiveFixedScaleX/Y`
- configured zoom scale: `m_zoomAimScaleQ14`
- active runtime scale: `m_activeZoomAimScaleQ14`

`ProcessAimInputMouse()` and the native aim delta hook read the effective scale
directly. They only recalculate the effective scale when the scoped state changes,
so the hot path does not apply an extra zoom multiplier every mouse input.

## Verification
- `.claude/skills/audit-config-defaults.ps1`
- `.claude/skills/build-mingw.bat --tail 120`

## Caveats
Do not add per-frame floating-point sensitivity math to the aim hot path. Keep new
scale factors precomputed in config/reload code or in zoom-state edge updates.
