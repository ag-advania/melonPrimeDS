# Phase 0 verification record

Date: 2026-07-12 (Asia/Tokyo)

## Status

The ROM-free baseline infrastructure is implemented. The ROM/runtime evidence
gate remains open because no `.nds` image is present in the scoped workspace.
No renderer or runtime source was changed in this phase.

## Commit

`docs(vulkan): capture Vulkan backend baseline and acceptance matrix`

## Build verified

- Windows configure: passed with the checked-in MinGW batch workflow.
- Windows compile: passed (`release-mingw-x86_64`, parallel 1).
- Windows link: passed; `build/release-mingw-x86_64/melonPrimeDS.exe` produced.
- Incremental confirmation: passed (`ninja: no work to do`).
- JSON manifest parse: passed.
- Python syntax and comparator self-test: passed.
- `git diff --check`: passed.
- Required PowerShell config/schema/ownership/platform/thread audits: passed.
- HUD schema regeneration produced no tracked diff.

## Runtime verified

- Developer HUD golden harness launched and completed twice.
- Both new outputs were byte-identical:
  `8CC5500A2D7871565D3512129ECA1B640AD00CE0D1398A41FC35D8DC1004B490`.
- The current output does not match
  `src/frontend/qt_sdl/tests/melonprime-hud-golden.txt`. This phase intentionally
  does not update those hashes because it contains no intentional visual change.

## Hardware

- Microsoft Windows 11 Home 10.0.22621, x86_64.
- NVIDIA GeForce RTX 5070 Ti (`VEN_10DE`, `DEV_2C05`).

## Validation

Vulkan validation is not applicable before Phase 1/3. Existing repository
audits passed. No OpenGL ROM frame was captured in this workspace.

## Unverified

- Metroid Prime Hunters OpenGL Classic captures at 1x/2x/4x/8x.
- Metroid Prime Hunters OpenGL Compute captures at 1x/2x/4x/8x.
- Color/alpha/depth/attribute pixel parity evidence.
- GPU timings, draw/dispatch counts, uploads, readbacks, and VRAM dirty bytes.
- AMD, Intel, Linux X11/Wayland, and macOS hardware.

## Known limitations

- ROM-derived images must remain local and are excluded by `.gitignore`.
- Existing HUD golden drift must be investigated in a dedicated visual change;
  it is not evidence of a Phase 0 regression.
- The current OpenGL code does not expose every requested baseline measurement;
  missing values must be recorded as `unavailable`, never as passing.

## Rollback

Revert the Phase 0 commit. No config migration or runtime state rollback is
required.
