# Video quality presets

## Scope

The Video section contains preset buttons rather than a single
Metroid-prefixed setting key. Each button writes a coordinated group of global
melonDS video configuration values immediately. The preset is therefore a
convenience transaction over renderer, sync, scale, and quality flags; it is
not a benchmark result.

The buttons are created by the settings UI and are also affected by build and
platform gates. The current global values after pressing a button are the
source of truth.

## Preset matrix

| Button | Screen.UseGL | Renderer | Threaded | Scale | Better polygons | Hires coordinates |
| --- | --- | --- | --- | ---: | --- | --- |
| Video quality: Low | true | Software | true | 4 | true | unchanged |
| Video quality: High | true | OpenGL | true | 4 | true | unchanged |
| Video quality: High2 | true | OpenGL Compute | true | 4 | true | unchanged |
| Video quality: High (Mac Metal) | false | Metal | true | 4 | true | true |
| Video quality: High (Mac Metal Compute Shader) | false | Metal Compute | true | 4 | true | true |

Every preset also writes Screen.VSync=false and Screen.VSyncInterval=1. The
preset does not itself establish an FPS limit, a physical display refresh
rate, or a renderer acceptance result.

## Button behavior

The preset slots update global configuration directly. They do not wait for
the settings dialog's ordinary instance-key save table and do not write guest
RAM. A renderer may be constructed later, or may fall back if the requested
backend is unavailable. Therefore:

1. record the values written by the button;
2. restart/recreate the renderer if the surrounding application requires it;
3. inspect the active renderer, not only the configured renderer; and
4. report fallback separately from the requested preset.

Changing a preset can also change whether an OpenGL context is required. The
renderer selection must be validated together with Screen.UseGL; reading only
3D.Renderer is insufficient.

## Platform gates

High2 / OpenGL Compute is disabled on macOS because the required OpenGL
compute path is unavailable. The click handler has a defense-in-depth return
even if the button is invoked programmatically.

Metal buttons exist only in builds with MELONPRIME_ENABLE_METAL and on macOS.
They are enabled only when MelonPrime::Metal::SupportsRequiredBaseline()
returns true. When the baseline is unavailable, the UI reports the cached
feature reason and does not write a Metal renderer selection.

Metal raster and Metal Compute force HiresCoordinates=true in the renderer and
lock the setting in Video Settings. At 1x the renderer retains the ordinary
coordinate branch; the forced mode applies only when the internal scale is
above 1x. Metal raster also forces Improved polygon splitting on and locks that
control; Metal Compute's normal span path does not consume polygon splitting.

## What the names mean

- Low selects the software 3D renderer while retaining threaded operation and
  the MelonPrime 4x software/GL scale defaults.
- High selects the ordinary OpenGL renderer.
- High2 selects the OpenGL compute renderer where supported.
- Mac Metal selects the native Metal raster path.
- Mac Metal Compute Shader selects the native Metal compute path.

The names describe the requested configuration. They do not claim that High
has better image quality on every GPU, that Compute is faster on every scene,
or that Metal is available on every macOS machine.

## Verification checklist

- Press each enabled button and inspect all values in the preset matrix.
- Confirm VSync and interval are written as specified.
- Verify the configured and active renderer separately.
- Confirm High2 cannot be selected on macOS.
- Confirm Metal buttons are gated by the required baseline.
- Test renderer creation, fallback, first frame, and shutdown.
- Record OS, build flags, GPU, active renderer, and frame pacing when
  comparing performance.

## Evidence and related material

Current source:

- InputConfig/MelonPrimeInputConfig.cpp
- InputConfig/MelonPrimeInputConfig.ui
- MelonPrimeVideoBackend.cpp
- EmuInstance.cpp
- Config.cpp

Platform background:

- docs/architecture/melonprime-metal-backend-plan.md
- docs/architecture/melonprime_macos_compute_renderer_restriction.md

The renderer implementation and platform capability checks are maintained in
melonPrimeDS. mphCodex research is not copied here because it does not own the
host renderer configuration.
