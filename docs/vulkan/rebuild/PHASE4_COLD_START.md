# Phase 4 — ROM cold-start (rebuild branch)

Run: `python tools/test_sapphire_vulkan_cold_start_regression_s79.py`  
Binary: `build/release-mingw-x86_64/melonPrimeDS.exe` with `MELONPRIME_SAPPHIRE_REBUILD=ON`

## Latest result (2026-07-16)

| Field | Value |
|---|---|
| Commit | `c640a33c1` |
| Exit code | `3221225477` (`0xC0000005`) |
| Last trace | `[FirstGpuLine] FinishFrame done lines=263` |
| Crash artifacts | `build/cold-start-artifacts/20260716T074303Z/` |
| Rebuild delta vs baseline | Same post-FinishFrame crash family as S80 baseline |

## Interpretation

Pure Sapphire FrameQueue/FrameLatch path and Desktop heuristic removal did not resolve the
first-frame crash. Next investigation remains in CPU/ARM execution after `FinishFrame`
(S80 RVA family ~`0xF941`), not in presenter composition.

## CI

- `python tools/generate_sapphire_vulkan_sources.py --verify` added to `sapphire-vendor-parity.yml`
- Cold-start gate remains **expected red** until post-FinishFrame crash is fixed
