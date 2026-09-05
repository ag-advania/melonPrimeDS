# High-resolution vertex coordinates

## Setting contract

| Item | Value |
| --- | --- |
| Key | `3D.GL.HiresCoordinates` |
| Default | `true` |
| UI | Use high resolution coordinates |
| Scope | global 3D renderer setting |

The setting chooses which already-calculated DS vertex position a supporting
renderer scales into its rasterizer. At an internal render scale above 1x,
enabled uses `HiresPosition`; disabled uses the ordinary final integer-screen
position. It does not increase the selected render scale by itself and is not a
texture filter, anti-aliasing mode, or Custom HUD coordinate option.

## Current backend policy

| Renderer | UI state | Effective policy |
| --- | --- | --- |
| OpenGL Compute | checked and disabled | forced on |
| Metal raster | checked and disabled | forced on |
| Metal compute | checked and disabled | forced on |
| Vulkan | checked and disabled | forced on |
| DirectX 12 | checked and disabled | forced on |
| classic OpenGL | control disabled | this compute/Metal coordinate path is not exposed |
| software | control disabled | not consumed by the software renderer |

The forced policy is intentional for every native high-resolution backend used
by MelonPrimeDS. `VideoSettingsDialog::RendererForcesHiresCoordinates()` returns
true for OpenGL Compute, Metal raster, Metal Compute, Vulkan, and DX12. Renderer
selection then checks the box, disables it, and writes `true` to config. The
rendering paths also receive the effective value independently; UI disablement
is not the only enforcement layer.

## Rasterizer semantics

OpenGL Compute, Metal raster, Metal Compute, Vulkan, and DX12 use the same
branch while building polygon edge positions:

```cpp
if (HiresCoordinates && ScaleFactor > 1) {
    scaledX = (vertex.HiresPosition[0] * ScaleFactor) >> 4;
    scaledY = (vertex.HiresPosition[1] * ScaleFactor) >> 4;
} else {
    scaledX = vertex.FinalPosition[0] * ScaleFactor;
    scaledY = vertex.FinalPosition[1] * ScaleFactor;
}
```

`HiresPosition` retains four fractional bits, hence the `>> 4` after scaling.
At 1x the condition is false even when the config is true. That preserves the
native-resolution integer coordinate path and means “forced on” has no visual
effect until internal scale exceeds 1.

The option is distinct from Improved polygon splitting. Compute, Vulkan, and
DX12 rasterize original DS polygon spans and do not need the classic
OpenGL/Metal center-fan workaround. High-resolution coordinates change vertex
positions; polygon splitting changes how an N-sided polygon becomes GPU
triangles. One setting is not an alias for the other.

## Live update and resource ownership

The render-setting handoff is through `EmuThread`'s `GPU::RenderSettings`.
Each supporting backend stores the incoming flag. OpenGL Compute, Vulkan, and
DX12 explicitly update it even when the scale is unchanged, then return without
rebuilding resolution-sized resources. This permits a live policy/value update
without treating it as a renderer restart or scale change.

The Metal implementations likewise retain their own per-renderer flag, but the
Metal backend wrapper always supplies the effective value `true`. Visual
validation must therefore identify both backend and scale; reading the config
alone does not prove which coordinate branch drew a frame.

## Dialog normalisation and cancel behaviour

When Video Settings opens, it snapshots the current renderer and coordinate
value. If the renderer is one of the forced backends, it first normalises both
the snapshot and live config to `true`. As a result:

- an old config containing `false` becomes `true` merely by opening the dialog
  on a forced backend;
- the box cannot be unchecked while that backend remains selected;
- the state-change slot writes `forced || checked` as an additional guard; and
- Cancel restores the normalised `true`, not the stale pre-contract `false`.

Switching to Metal immediately checks and locks the box. Switching back to
OpenGL Compute, Vulkan, or DX12 keeps the same forced value.

MelonPrime's renderer preset handlers for the forced backends also set
`3D.GL.HiresCoordinates=true`, so choosing a preset and using the general Video
Settings dialog converge on the same config state.

## Source ownership

| Source | Responsibility |
| --- | --- |
| `src/frontend/qt_sdl/Config.cpp` | default and legacy config migration |
| `src/frontend/qt_sdl/VideoSettingsDialog.cpp` | renderer policy, UI enablement, normalisation, apply/cancel |
| `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp` | MelonPrime renderer preset normalisation |
| `src/frontend/qt_sdl/EmuThread.cpp` | config-to-`GPU::RenderSettings` handoff |
| `src/GPU_OpenGL.cpp` | force `true` when forwarding to OpenGL Compute |
| `src/GPU_Vulkan.cpp` | force `true` when forwarding to Vulkan 3D |
| `src/GPU_DX12.cpp` | force `true` when forwarding to DX12 3D |
| `src/GPU_Metal.mm` | force `true` when forwarding to Metal 3D |
| `src/GPU3D_Compute.cpp` | OpenGL Compute storage and edge-position branch |
| `src/GPU3D_Vulkan.cpp` | Vulkan storage and edge-position branch |
| `src/GPU3D_DX12.cpp` | DX12 storage and edge-position branch |
| `src/GPU3D_Metal.mm` | Metal raster storage and edge-position branch |
| `src/GPU3D_MetalCompute.mm` | Metal compute storage and edge-position branch |

This is host renderer behaviour and has no ROM patch address table. mphCodex is
therefore not the authority for this setting.

## Verification matrix

- Open Video Settings on OpenGL Compute, Vulkan, and DX12: the box is checked,
  disabled, and config is true.
- Seed config false, open each forced backend, cancel, and confirm the value
  remains normalised true.
- Open Video Settings on Metal raster and Metal Compute: the box is checked and
  disabled, and config is true.
- Compare Metal output at 1x and above: 1x must take the ordinary coordinate
  branch while higher scales use the forced high-resolution branch.
- Compare at 2x and a high scale using geometry that exposes subpixel vertex
  movement; record backend, scale, and frame evidence.
- Confirm the disabled control cannot change the forced value or trigger a
  scale-resource rebuild.
- Confirm classic OpenGL/software do not imply support merely because the
  global config key exists.
- Keep source audit, successful backend build, backend initialization, and
  visual parity as separate evidence claims.
