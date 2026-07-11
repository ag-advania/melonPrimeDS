# Vulkan renderer reference captures

This directory defines the reproducible, ROM-free acceptance baseline for the
MelonPrime Vulkan renderer work. Copyrighted game frames are local artifacts and
must not be committed.

## Capture contract

1. Start from a clean build of the commit recorded in `manifest.json`.
2. Disable OSD, Custom HUD, filtering, and window scaling for the core GPU
   captures. Capture HUD/OSD separately using the frontend scenarios.
3. Run both `OpenGL` and `OpenGL Compute Shader` at 1x, 2x, 4x, and 8x.
4. Reproduce every scene listed in `manifest.json`; save lossless PNG or raw
   BGRA/RGBA data under `captures/<renderer>/<scale>/<scene>/`.
5. Record renderer selection, pass diagnostics, driver, OS, GPU, settings, and
   capture hashes in `manifest.local.json`.
6. Compare like-for-like outputs with:

   ```powershell
   python tools/vulkan/compare_renderer_frames.py reference.png candidate.png
   ```

Raw images require explicit dimensions and format:

```powershell
python tools/vulkan/compare_renderer_frames.py reference.bgra candidate.bgra `
  --width 256 --height 192 --format bgra8 --report reports/scene.json
```

The comparison tool exits nonzero when dimensions differ, changed pixels exceed
the configured limit, or a channel exceeds the configured tolerance. PNG pixel
comparison requires Pillow; SHA-256 comparison remains available without it.

## Required evidence

Each local run records these independently:

- build/configure/link result;
- application launch and requested/normalized/actual renderer log;
- ROM and scene reproduction method (never the ROM itself);
- color, alpha, depth, and attribute comparisons where available;
- CPU/GPU timing, draw/dispatch counts, upload/readback bytes;
- validation status and hardware/driver identity;
- known limitations and unverified matrix entries.

`manifest.json` is the acceptance contract. `manifest.local.json`, `captures/`,
and `reports/` are intentionally ignored because they contain machine-specific
or ROM-derived data.
