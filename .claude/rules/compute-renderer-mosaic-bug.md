# Compute Renderer Mosaic Bug — Investigation Log

**Status:** Open. No working fix yet.
**First investigated:** 2026-04-24
**Branch:** `highres_fonts_v3`

## Symptoms

- **Renderer:** Compute shader renderer only (OpenGL and Software renderers are unaffected).
- **Triggers seen so far in MPH:**
  - Magmaul blast (large explosion effect)
  - Enemy death effects
- **Distance dependency:** Worse / triggers when approaching the effect.
- **Visual:** Mosaic-shaped (tile-shaped) corruption.
- **Spread:** Affects the in-game (NDS-side) OSD too — not just the offending effect.

## Code under suspicion

- [src/GPU3D_Compute.cpp](../../src/GPU3D_Compute.cpp)
- [src/GPU3D_Compute_shaders.h](../../src/GPU3D_Compute_shaders.h)
- [src/GPU3D_Compute.h](../../src/GPU3D_Compute.h)

Key constants:

- `MaxWorkTiles = TilesPerLine * TileLines * 16` ([GPU3D_Compute.cpp:404](../../src/GPU3D_Compute.cpp#L404))
- `BinStride = 2048/32 = 64` ([GPU3D_Compute.h:163](../../src/GPU3D_Compute.h#L163))
- `CoarseBinStride = BinStride/32 = 2` ([GPU3D_Compute.h:164](../../src/GPU3D_Compute.h#L164))

## Hypothesis (current best guess, not confirmed)

Per-frame work-tile bin overflow. The binning shader does:

```glsl
// GPU3D_Compute_shaders.h: line ~988
uint workOffset = atomicAdd(VariantWorkCount[0].w, uint(bitCount(binnedMask)));
BinningMaskAndOffset[BinningWorkOffsetsStart + linearTile * BinStride + groupIdx] = workOffset;
...
WorkDescs[WorkDescsUnsortedStart + workOffset + idx] = uvec2(...);
```

There is **no upper bound check** on `VariantWorkCount[0].w`. The downstream sort/rasterise stages iterate `gl_GlobalInvocationID.x < VariantWorkCount[0].w`, so an over-committed counter would cause out-of-bounds reads of `WorkDescs`, producing garbage `tilePositionCombined` and `polygonIdx` — which would then write garbage pixels to random tile positions, matching the "mosaic that bleeds into OSD" symptom.

`MaxWorkTiles = TilesPerLine * TileLines * 16` is a heuristic average of 16 polygon-groups per tile (out of 64 possible per `BinStride`). Heavy alpha effects covering many tiles can plausibly exceed this.

## Things tried (all reverted)

### Attempt A: Bump `MaxWorkTiles` multiplier from x16 to x32

**Change:** `MaxWorkTiles = (TilesPerLine * TileLines) << 4;` → `<< 5` in [GPU3D_Compute.cpp:404](../../src/GPU3D_Compute.cpp#L404).

**Idea:** If the bug is bin overflow at the heuristic limit, doubling the budget should at least reduce frequency.

**Result:** **No improvement.** Mosaic corruption still reproduces with the same triggers.

**What this tells us:**

- Either the overflow is much larger than 2x the original budget (possible but unlikely for 60fps content), or
- The overflow hypothesis is wrong, or
- The overflow happens in a *different* buffer (not `WorkDescs` / `TileMemory`).

### Attempt B: Bounds-checked allocation via CAS loop in binning shader

**Change:** In [src/GPU3D_Compute_shaders.h](../../src/GPU3D_Compute_shaders.h) binning shader (around line 980), replaced the unconditional `atomicAdd` with an `atomicCompSwap` loop that aborts on overflow. On overflow, the group's `binnedMask` is zeroed before any downstream writes (fine mask, coarse mask, work offset, WorkDescs all skip), so the buffers stay consistent without gaps containing stale entries.

**Idea:** Even if A didn't help, this is a defensive correctness fix that prevents OOB writes/reads from corrupting other buffers. If overflow IS the cause, this should make corruption "graceful" (missing polygons instead of trashed screen).

**Result:** **Made things worse / new bug.** Reverted.

**Note:** The exact failure mode wasn't captured. Possibilities to keep in mind for any future attempt:

- The CAS loop relies on a relaxed read of `VariantWorkCount[0].w` to seed `expected`. SSBO load semantics may have ordering issues across workgroups in some drivers.
- Dropping a group's polygons may break invariants the rasterizer relies on (e.g. shadow-mask polygons that *must* be processed in order with their casters).
- The `atomicCompSwap` retry budget (32) may interact badly with workgroup divergence.
- Possibly the binnedMask=0 path still leaves `inVariantOffset` consistency issues, since other groups for the same variant continue counting.

### Attempt C: CPU-side bounds guard on `YSpanIndices` writes

**Hypothesis tested:** [src/GPU3D_Compute.cpp:438](../../src/GPU3D_Compute.cpp#L438) sizes `YSpanIndices` and the GL `XSpanSetupMemory` buffer with the heuristic `64*2048*ScaleFactor`. The 64 represents an assumed "average polygon height in DS coords"; large alpha effects could push the actual per-frame `numSetupIndices` over this budget. The two write sites at [line 936-940](../../src/GPU3D_Compute.cpp#L936) and [line 1001-1005](../../src/GPU3D_Compute.cpp#L1001) had **no bounds check** — `std::vector::operator[]` with an out-of-range index is undefined behavior, and the subsequent `glBufferSubData(..., numSetupIndices*4*2, ...)` upload at [line 1027](../../src/GPU3D_Compute.cpp#L1027) would silently fail with `GL_INVALID_VALUE` if the size exceeded the GL buffer's capacity, leaving the shader to read stale span data.

This hypothesis matched all the symptoms (Magmaul/death effects produce many tall polygons; close range increases polygon screen height; mosaic-shaped corruption matches stale `XSpanSetups` causing `BinPolygon` to bin polygons into wrong tiles; OSD bleed-through matches a frame-wide shared buffer being corrupted; Compute-only because Software/OpenGL don't use a span buffer; and most importantly **explains why both A and B did nothing** — neither touched the relevant buffer).

**Change:**

- Added `int MaxYSpanIndicesAllocated` member to [src/GPU3D_Compute.h](../../src/GPU3D_Compute.h) and cached the budget there at `SetRenderSettings` time (replaced the local `int maxYSpanIndices` so `RenderFrame()` could see the value).
- Wrapped the dummy-path write ([line 936-940](../../src/GPU3D_Compute.cpp#L936)) with `if (numSetupIndices < MaxYSpanIndicesAllocated)`. On overflow, collapsed the polygon to zero height (`RenderPolygons[i].YBot = RenderPolygons[i].YTop`) so `BinPolygon`'s early-out would skip it.
- In the multi-row loop ([line 951-1006](../../src/GPU3D_Compute.cpp#L951)), added `if (numSetupIndices >= MaxYSpanIndicesAllocated) { RenderPolygons[i].YBot = (s32)y; break; }` to truncate the polygon to whatever rows had been written.

**Result:** **Did not fix the bug.** Reverted.

**What this tells us:**

- The `YSpanIndices` overflow hypothesis is **wrong** (or at least not the dominant cause). Either no overflow is happening in practice, or overflow happens and the dropped polygons are somewhere other than the corrupting effect.
- All three single-buffer overflow hypotheses (`MaxWorkTiles` global pool, binning sort buffer, `YSpanIndices`) have been eliminated by direct testing. The bug is almost certainly **not a buffer overflow**.

## Things to investigate next

**Strong negative result from C:** with three buffer-overflow hypotheses all empirically refuted, redirect investigation away from buffer sizing and toward shader-side numerical / pipeline issues.

1. **Test reproduction in vanilla melonDS (compute renderer).**
   This is the cheapest first step now. If vanilla melonDS reproduces the same bug, it's an upstream Compute renderer issue we should fix at the source rather than patching downstream. If only MelonPrimeDS reproduces, narrow to our diff.

2. **Diff our compute renderer vs vanilla melonDS develop.**
   This fork has many "improved", "optimized" commits ([git log](#) for `GPU3D_Compute*`) that touched the C++ side. Most look like comment / TileSize-formula micro-optimizations, but a regression smuggled in there is plausible. Bisect the compute renderer commits if vanilla doesn't repro.

3. **Suspect transparent-polygon path / shadow-mask polygons specifically.**
   Magmaul blast and death effects are alpha-blended; death effects may also use shadow-volume tricks for the dissolve. Look at `Rasterise` shader's transparent path (around [GPU3D_Compute_shaders.h:1056](../../src/GPU3D_Compute_shaders.h#L1056)) and `isShadowMask` handling (around [GPU3D_Compute_shaders.h:1343](../../src/GPU3D_Compute_shaders.h#L1343)). The mosaic shape might come from a per-tile composition bug, not a binning bug.

4. **Numerical edge cases when polygons are very close to the camera.**
   "Worse when approaching" suggests W getting very small. Look at `XRecip` / `YRecip` calculation and Q-format precision. A divide-by-near-zero or saturation could produce extreme `XSpanSetups` values that bin polygons into wildly wrong tiles. Check `InterpSpans` shader where these are computed.

5. **Add per-frame debug instrumentation.**
   Now that overflow is ruled out, the cheapest next step on our side is to dump runtime numbers: peak `numSetupIndices` per frame, peak `VariantWorkCount[0].w`, polygon counts, and the bounding boxes of polygons during a buggy frame vs a clean frame. If we see anomalous values (e.g. polygon with `XMin=-2147483648` or `YBot=2000` at native res), we have the smoking gun.

6. **Driver / GPU dependency.**
   Note GPU vendor and driver version on the reproducing machine. Some shader behavior (especially around SSBO synchronization, denormalized floats, and integer overflow in shaders) is implementation-defined.

7. **Bisect within MPH itself.**
   Confirm whether the bug repros in single-player only, multiplayer, both. Confirm it repros in Adventure mode and Battle mode. The differences in poly counts and effects between modes can hint at which polygon kind triggers it.

## Files touched and reverted in this investigation

- [src/GPU3D_Compute.cpp](../../src/GPU3D_Compute.cpp) — Attempts A and C (both reverted)
- [src/GPU3D_Compute_shaders.h](../../src/GPU3D_Compute_shaders.h) — Attempt B (reverted)
- [src/GPU3D_Compute.h](../../src/GPU3D_Compute.h) — Attempt C (reverted; added `MaxYSpanIndicesAllocated` member)
