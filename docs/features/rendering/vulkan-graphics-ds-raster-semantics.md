# Vulkan Graphics vs DS raster semantics

Static audit of the Vulkan **graphics** rasteriser (`GPU3D_Vulkan_GraphicsRasterShader.vert/.frag`)
against the two references that implement DS raster semantics explicitly:

- `src/GPU3D_Soft.cpp` (`SoftRenderer3D::RenderPolygonScanline`, `TextureLookup`)
- `src/GPU3D_Compute_shaders.h` (`InterpSpans`, `Rasterise`)

Scope: pixel coverage and S/T integerisation only. No sampler, filtering or diagnostic
changes. Everything below is derived from the sources listed; nothing here has been
verified on a GPU.

## 1. Reference span model (software and compute agree exactly)

| | software | compute |
| --- | --- | --- |
| scanline range | `y >= YTop && y < YBottom` (`GPU3D_Soft.cpp:1404`) | `position.y >= polygon.YTop && position.y < polygon.YBot` |
| span domain | `Interpolator<0> interpX(xstart, xend+1, ...)` (`GPU3D_Soft.cpp:1097`) | `xspan.X1 = xr + 1` (`GPU3D_Compute_shaders.h:719`) |
| pixel loop | `for (; x < xlimit; x++)` with `xlimit <= xend+1` (`GPU3D_Soft.cpp:1107`) | `position.x >= xspan.X0 && position.x < xspan.X1` |
| attribute sample point | `interpX.SetX(x)` — the **integer** pixel coordinate | `i = position.x - xspan.X0` — the **integer** pixel coordinate |
| S/T to texel | `s16 s; s >>= 4;` (`GPU3D_Soft.cpp:151`) | `uvf = vec2(ivec2(u,v)) * (1.0/16.0) * InvTextureSize` then NEAREST fetch |
| wrap | explicit `& (size-1)` / mirror / clamp (`GPU3D_Soft.cpp:158-188`) | sampler wrap mode |

Two facts follow, and they are the whole basis of this audit:

1. **The DS span is one pixel wider than its vertex hull.** Pixels `[XL, XR]` are filled
   inclusively, i.e. the continuous footprint is `[XL, XR+1)`. Vertically the rule is the
   ordinary half-open `[YTop, YBottom)`.
2. **The DS evaluates attributes at the pixel's integer coordinate**, not at its centre.
   The interpolation denominator is `XR+1-XL`, so pixel `XL` gets exactly the left vertex
   attribute and pixel `XR` gets `(XR-XL)/(XR+1-XL)` of the way to the right one.

## 2. What the Vulkan graphics path did

Vertex stage: `gl_Position` from `HiresPosition * scale / 16` (subpixel-exact, unrounded —
`ResolveAcceleratedVertexFixedX`, `useHiresCoordinates = true`). Fragment stage: hardware
rasteriser, attributes interpolated at the fragment centre `p + 0.5`.

## 3. Findings against the ranked hypothesis list

### Rank 1 — "Vulkan generates fragments outside DS coverage": **not supported**

For an axis-aligned edge pair `[xl, xr]` in render-pixel space, hardware covers
`p + 0.5 ∈ [xl, xr]`, i.e. `p ∈ [ceil(xl-0.5), floor(xr-0.5)]`. The DS covers
`d ∈ [floor(xl), floor(xr)]`, because `FinalPosition` is the truncation of
`HiresPosition/16` (`GPU3D.cpp:1123-1134`). For every `xl`, `ceil(xl-0.5) >= floor(xl)`;
for every `xr`, `floor(xr-0.5) <= floor(xr)`.

**Vulkan graphics coverage is a subset of DS coverage — it can under-cover but never
over-cover.** The extra-edge-fragment mechanism cannot produce the reported artefact, and
the same inequality is why removing the 0.2 px passive expansion opened cracks rather than
closing them. A fragment-shader coverage re-test (only able to *remove* fragments) would
not help either.

### Rank 2 — "S/T integerisation rule differs": **rule identical, timing was not**

`floor(s/16)`, `s >> 4` and `floor(uvf * texSize)` are the same function. The divergence
was **where** the value is produced, not how it is reduced:

- The shader carried S/T as a float already divided by 16, then took `floor()` of it.
- Software and compute reduce an **integer** in 1/16-texel units.

A float that should land exactly on a texel boundary can floor to the texel below. With
`repeat`/`mirror` wrapping, texel `-1` reads the opposite edge of the texture — the exact
"ghost pixel from the far side" signature.

### Rank 3 — "DS subpixel → Vulkan coordinate conversion": **real, and it is the
half-pixel the corrections were chasing**

Fragment centre sampling is half a render pixel past the DS sample point, for every
fragment, not just at edges.

### The actual defect: the corrections themselves

The fragment shader carried three ad-hoc UV displacements. The one on the
`TRI_FLAG_LINEAR && (repeat|mirror)` path was:

```glsl
texcoord += dFdx(fTexcoord) * -subpixelOffset.x + dFdy(fTexcoord) * -subpixelOffset.y;
texcoord -= vec2(1.0 / 8.0);   // LINEAR_TEXEL_COORD_BIAS
```

With one texel per DS pixel at render scale `S`, `dFdx(fTexcoord) = 1/S` texels per render
pixel and the fragment texcoord is `s0 + (sub + 0.5)/S`. The correction evaluates to

```
s0 + (sub + 0.5)/S - sub/S - 1/8
```

which is `s0` exactly when `S == 4` — `1/8` is the half-pixel term hardcoded for 4x. So the
corrected value lands **exactly on the texel boundary**, `floor()` turns float noise into
`s0 - 1`, and repeat wrapping reads the opposite texture edge. At other scales the constant
over- or under-shoots instead. `usesHighresOpaqueRepeatedModelTexture` and
`usesHighresLinearTextBand` (texture-page magic numbers `0x79df2000`, `0x71df2800`, …) were
exclusion lists carved out of this same correction.

## 4. Change applied

`GPU3D_Vulkan_GraphicsRasterShader.vert`
- `fTexcoord` is passed in the DS 1/16-texel fixed-point domain (the `* (1/16)` is gone),
  so the fragment stage can integerise with software's bit semantics.

`GPU3D_Vulkan_GraphicsRasterShader.frag`
- Removed `LINEAR_TEXEL_COORD_BIAS`, `dsPixelCenterDelta()`,
  `usesCompactOpaqueDepthWritePaletteUi()`, `usesHighresOpaqueRepeatedModelTexture()` and
  `usesHighresLinearTextBand()` — every per-texture-page and per-resolution constant on the
  UV path.
- One derived correction replaces them, applied to linear spans only:

  ```glsl
  vec2 dsSampleGridDelta()   // = -(subpixelOffset + 0.5)
  ```

  `-0.5` is the fragment-centre-to-integer-grid-point step; `-subpixelOffset` collapses the
  render-scale block onto the DS grid point software sampled. Both terms come from
  `pc.width / 256` and `pc.height / 192`.
- Integerisation now mirrors `TextureLookup()`:

  ```glsl
  int sampleS = int(floor(texcoord.x + 0.5)) >> 4;
  ```

  Snapping back onto the 1/16 grid before the arithmetic shift makes the boundary case
  deterministic, which is what removes the wrap.

`usesDsPixelCenteredTranslucentPaletteUi` was renamed `usesTranslucentPaletteUi`: it no
longer drives UV, only the palette-UI alpha hole fill and blend-alpha encode.

## 5. Gating choice

The DS-grid collapse is applied under `TRI_FLAG_LINEAR`, which the CPU sets when all three
vertices share a W and `(W & 0x7F) == 0` (`GPU3D_Vulkan.cpp:11952`). That is the same
predicate `SoftRenderer3D`'s `Interpolator` uses to take its linear path
(`GPU3D_Soft.h:98-104`), so it is a DS concept, not a heuristic. Perspective spans keep
hardware centre sampling: collapsing them onto the DS grid would throw away the hi-res
texture detail that upscaling exists to provide.

## 6. CPU-side geometry: exact DS footprint for linear polygons

Sections 1-5 left one divergence open: Vulkan coverage was a strict subset of the DS's,
because the footprint `[XL, XR+1)` needs a trailing-edge-only extension. A uniform outward
expansion cannot express it — at render scale `S` the trailing edge needs `+S`, and `+S` on
a leading edge adds a pixel there — which is why every "expand by 0.2 px" attempt either
left seams or over-covered.

**The `[XL, XR+1)` footprint does not apply to vertical right edges.** `GPU3D_Soft.cpp:988`:

```cpp
// right vertical edges are pushed 1px to the left as long as either:
// the left edge slope is not 0, or the span is not 0 pixels wide, and it is not at the
// leftmost pixel of the screen
if (rp->SlopeR.Increment==0 && (rp->SlopeL.Increment!=0 || xstart != xend) && (xend != 0))
    xend--;
```

So an axis-aligned span is `[XL, XR)`, which snapped vertices already reproduce exactly
under centre sampling — extending it draws a spurious column, which is visible immediately
on bitmap text. The extension applies only where the right chain has a *sloped* edge.
Zero-height edges are skipped, because `SetupPolygonRightEdge()` advances past them
immediately and they never become the active `SlopeR`. The compute rasteriser does **not**
implement this rule (`xspan.X1 = xr + 1` unconditionally, `GPU3D_Compute_shaders.h:719`);
software is the reference.

Where the extension does apply, the exact construction is the **Minkowski sum of the
polygon with a horizontal segment of one DS pixel**, after snapping the vertices to the DS
pixel grid. `BuildAcceleratedScene()` walks the ring from `VTop` towards `VBottom` along the
right chain — the direction `SoftRenderer3D::SetupPolygon()` advances `SlopeR`, selected by
`FacingView` — emitting:

```
VTop(x)  VTop(x+S)  [right chain](x+S)  VBottom(x+S)  VBottom(x)  [left chain](x)
```

Duplicating the two shared apexes is what keeps the left chain at `XL` while every
scanline's right edge becomes `XR+1`. Both copies of an apex carry the source vertex
attributes, so the interpolation denominator becomes `XR+1-XL` — the software denominator —
and the fragment stage's DS grid sample point then lands on the exact software value.

Consequences:

- `AcceleratedCoverageFixConfig` is bypassed for these polygons (`CoverageFixState{}`): the
  footprint is exact, so heuristic expansion must not stack on top of it.
- `BetterPolygons` is skipped for them. It exists to hide perspective seams across a quad;
  a DS-linear polygon has no perspective, and its interpolated centre vertex would not lie
  on the reconstructed footprint.
- The construction needs `VTop != VBottom`; single-scanline polygons fall back to the
  unchanged path.

`tools/testing/ds-linear-ring-coverage-model.py` re-derives the result without a GPU. It
models the DS span rule — including the x-major run fill and the vertical-right-edge
pushback — and compares centre-sampled hardware coverage before and after, in wrong-pixel
counts. It derives `VTop`/`VBottom` and the chain direction rather than taking them as
input, because hand-written indices are what made an earlier revision bless the wrong rule.

| case | scale | new (miss/extra) | old (miss/extra) |
| --- | --- | --- | --- |
| axis-aligned rect | 1 / 2 / 4 | 0/0 | 0/0 |
| rect, reverse wind | 1 / 2 / 4 | 0/0 | 0/0 |
| 1px tall rect | 1 / 2 / 4 | 0/0 | 0/0 |
| vert right, slant left | 4 | 86/0 | 86/0 |
| pointed-apex tri | 4 | 110/14 | 208/0 |
| slanted quad | 4 | 87/23 | 176/0 |

Axis-aligned polygons — the DS 2D UI — were already exact and stay exact: the extension is
suppressed for them, and the DS-grid snap is a no-op whenever the ortho transform already
lands on integers. The gain is on sloped right edges, where the error roughly halves. Those
cases now over-cover slightly where they previously only under-covered; that cannot
reintroduce the wrap artefact, because the extension moves the vertex attributes with the
geometry, so an over-covered fragment still interpolates strictly inside `[A_L, A_R]`.

`vert right, slant left` stays inexact for a reason this change does not address: the DS
fills the whole horizontal run of an x-major *left* edge across a scanline, which centre
sampling cannot reproduce.

## 7. Known remaining divergences

- **x-major edge runs.** As above: non-axis-aligned DS-linear polygons, and every
  perspective polygon, still differ from the DS span rule at their slanted edges.
- **Perspective polygons keep hardware centre sampling**, both for coverage and for S/T.
- **`s16` texcoord wraparound.** Software truncates S/T to `s16` before `>>= 4`; neither the
  compute renderer nor this path does. Only reachable past ±2048 texels of tiling.
- **`usesTranslucentPaletteUi` still matches on texture page `0xA3A0`.** That is an alpha
  path, outside this audit's scope.

## 8. Validation performed

- All eight fragment variants and the vertex shader compile (`glslc` 2024.2 / glslang
  1.3.290.0).
- `tools/vulkan_spirv.py check` passes; the nine regenerated headers now match
  byte-for-byte.
- `GPU3D_AcceleratedFrontend.cpp` passes `g++ -fsyntax-only -std=c++20`.
- `tools/testing/ds-linear-ring-coverage-model.py` passes.
- `audit-vulkan-compositor-spirv`, `audit-vulkan-capture-export-timing`,
  `audit-vulkan-compositor-colorimage-sync`, `audit-vulkan-frame-resource-ownership`,
  `audit-vulkan-no-post-translucent-opaque-replay`,
  `audit-structured-composition-contract` pass.
- **Not performed:** any build, and any runtime or visual comparison. The behavioural
  claims in sections 3 and 4 are derivations from source, not measurements.
