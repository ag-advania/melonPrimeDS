# Vulkan 3D draw pass order

`VulkanRenderer3D::dispatchGraphicsRasterAndReadback()` records the color draws
that produce `ColorImage`, which the structured compositor then combines with the
2D planes. This document fixes the order those draws must happen in, and why.

## The contract

1. **opaque polygons** — exactly one color draw per polygon
2. **shadow mask / shadow** — DS stencil semantics
3. **translucent polygons** — including `NeedOpaquePass` for opaque texels inside
   an alpha texture
4. **edge marking / fog / final** — full-screen passes

No color polygon drawn in step 1 may be drawn again after step 3.

Polygon classification comes from `BuildAcceleratedPolygonMeta()`, which reads
`melonDS::Polygon` fields only: `Translucent`, `IsShadowMask`, `IsShadow`,
`WBuffer`, `FacingView`, and `Attr`. Those are the same DS-computed fields
`ComputeRenderer3D` reads. The renderer must not infer a polygon's role from its
appearance.

Forbidden inputs to draw order:

- texture addresses or texture pages
- screen coordinates or bounding boxes
- W values
- specific polygon IDs treated as "UI"
- any draw issued from a modified copy of `polyAttr`

## What went wrong

A fifth pass, `PaletteUiOpaqueReplay`, used to run after step 3. It walked
`GraphicsOpaqueDrawIndices` a second time, selected draws with a predicate that
guessed which polygons were menu UI, and redrew them with `polyAttr` bit 14
forced on — which also changed the depth compare of the pipeline they were drawn
with, since `depthCompareMode` is derived from that bit.

The predicate mixed DS state with appearance guesses:

```
palette texture format, color-0 transparent, clamped wrapping
a flat plane at W == 25600
texture pages 0x05C0 / 0x85C0, texParam 0x6DC00200, 0x68C01B10, 0x6A5016D0
screen-coordinate boxes (x 38..46 DS px, y 6..16 DS px)
X/Y bounds overlap against the frame's translucent overlays
```

Large flat background polygons satisfy those conditions too. On frames where the
predicate matched, a background polygon that belongs *behind* the menu was
redrawn *over* it. Because the predicate reads the frame's own translucent
overlay list — which changes with menu animation, blinking and overlay presence
— it matched intermittently, so correct and incorrect frames alternated.

OpenGL Compute never showed this: `ComputeRenderer3D` submits every polygon once
and resolves ordering in `DepthBlend`/`FinalPass` from DS polygon metadata. There
is no post-translucent opaque re-submission anywhere in it.

## Why the earlier synchronization fixes did not help

The per-`FrameResource` packed planes and the `ColorImage` compute-read barrier
are both real and are kept — see
[vulkan-compositor-frame-ownership.md](vulkan-compositor-frame-ownership.md).
But this defect is upstream of all of them: the 3D renderer deliberately recorded
an extra draw, so `ColorImage` was already wrong when the compositor received it,
and the compositor then displayed that wrong image correctly. No amount of buffer,
barrier or queue correctness changes a faithfully composited bad input.

## Enforcement

`tools/ci/audits/audit-vulkan-no-post-translucent-opaque-replay.py` checks that:

- the retired symbols and artwork constants are absent
- `dispatchGraphicsRasterAndReadback()` walks `GraphicsOpaqueDrawIndices` exactly
  once, and not after a translucent draw
- nothing in that function mutates `polyAttr`
- `NeedOpaquePass`, shadow, edge and fog survive (matched on word boundaries, so
  renaming a pass away is caught)
- the contract comment stays next to the code it governs
