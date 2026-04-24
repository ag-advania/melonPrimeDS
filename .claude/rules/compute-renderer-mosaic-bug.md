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

## Things to investigate next

1. **Confirm or refute the overflow hypothesis empirically.**
   Add a debug counter: in the binning shader, atomicMax a separate SSBO field with `workOffset + workCount`, then read it back per frame on the CPU side and log the peak value vs `MaxWorkTiles`. If peak << MaxWorkTiles during the buggy frame, overflow is *not* the cause.

2. **Look at other buffers that could overflow:**
   - `BinningMaskAndOffset` per-tile per-group entries — bounded by `BinStride = 64`, hardware-limited by 2048 polygons. Probably safe.
   - Per-variant counters (`VariantWorkCount[variantIdx].z`) — could a single variant exceed reasonable counts?
   - `YSpanIndices` — `maxYSpanIndices = 64 * 2048 * ScaleFactor` ([GPU3D_Compute.cpp:438](../../src/GPU3D_Compute.cpp#L438)). Likely safe but verify.
   - `WorkDescMemory` size = `MaxWorkTiles*2*4*2` ([GPU3D_Compute.cpp:426](../../src/GPU3D_Compute.cpp#L426)).

3. **Suspect transparent-polygon path specifically.**
   Magmaul blast and death effects are alpha-blended. The compute renderer's transparent rasterise path differs from opaque. Look at `Rasterise` shader (around [GPU3D_Compute_shaders.h:1056](../../src/GPU3D_Compute_shaders.h#L1056)) and how it composites transparent layers.

4. **Suspect shadow-volume / shadow-mask polygons.**
   The rasterizer has special handling for `isShadowMask` ([GPU3D_Compute_shaders.h:1343](../../src/GPU3D_Compute_shaders.h#L1343)). Death effects in MPH may use shadow-volume tricks for the dissolve.

5. **Check upstream melonDS issues / discussions** for compute renderer artifacts on alpha-heavy effects — this might be a known unfixed issue with a discussed root cause we can leverage.

6. **Test reproduction in vanilla melonDS (compute renderer).**
   If vanilla melonDS reproduces the same bug, this is purely an upstream issue and fixes should be coordinated upstream. If only MelonPrimeDS reproduces it, something in our local edits to compute renderer / shaders / config introduced or exposed it.

7. **Driver / GPU dependency.**
   Note GPU vendor and driver version. Some out-of-bounds SSBO behavior is implementation-defined and may differ between NVIDIA / AMD / Intel.

## Files touched and reverted in this investigation

- [src/GPU3D_Compute.cpp](../../src/GPU3D_Compute.cpp) — Attempt A (reverted)
- [src/GPU3D_Compute_shaders.h](../../src/GPU3D_Compute_shaders.h) — Attempt B (reverted)
