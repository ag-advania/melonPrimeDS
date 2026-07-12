# Phase 2 verification record

Date: 2026-07-12 (Asia/Tokyo)

## Status

Phase 2 is implemented. Persisted renderer IDs are stable, backend selection is
centralized, and renderer construction moved from the upstream-facing
`EmuThread.cpp` switch into a fork-owned factory. No Vulkan UI is exposed.

## Commit

`refactor(video): add stable renderer IDs and Vulkan backend policy`

## Build verified

- Default OFF: Windows MinGW configure, compile, and link passed.
- Vulkan ON: Windows MinGW configure, compile, and link passed.
- Force-disabled: Windows MinGW configure, compile, and link passed.
- The working build tree was restored to default OFF.
- Renderer IDs are compile-time asserted as Software=0, OpenGL=1,
  OpenGLCompute=2, Metal=3, MetalCompute=4, Vulkan=5,
  VulkanCompute=6, Max=7 when the corresponding names are compiled.

## Runtime verified

- Default-OFF build with temporary `3D.Renderer=5` and `Screen.UseGL=false`
  survived a four-second no-ROM smoke test; the test config was restored with
  an identical SHA-256 hash.
- Vulkan-ON raster env bootstrap survived a three-second no-ROM smoke test.
- Vulkan-ON compute + presenter env bootstrap survived a three-second no-ROM
  smoke test.
- Default-OFF HUD harness output remained identical to Phase 0/1:
  `8CC5500A2D7871565D3512129ECA1B640AD00CE0D1398A41FC35D8DC1004B490`.

## Policy behavior

- Unsupported, unknown, and compile-gated renderer IDs normalize to Software.
- macOS OpenGL Compute normalization remains OpenGL Classic.
- Vulkan IDs never request an OpenGL context.
- Until Phase 4, requested Vulkan presentation resolves to actual NativeQt so
  the existing repaint scheduler remains correct.
- Until Phase 6, requested Vulkan/Vulkan Compute renderer construction returns
  actual Software with an explicit factory stage and fallback reason.
- Environment overrides are developer bootstrap only and never alter config.
- Renderer creation logs requested, normalized, actual, presenter, failure
  stage, fallback reason, and `config_changed=no`.

## Hardware

- Microsoft Windows 11 Home 10.0.22621, x86_64.
- NVIDIA GeForce RTX 5070 Ti (`VEN_10DE`, `DEV_2C05`).

## Validation

- Required config/schema/ownership/platform/thread/SRP audits passed.
- Full compiler and compiler-free SPIR-V checks passed.
- `git diff --check` passed.
- Force-disabled build graph contains no Vulkan sources/definitions.
- Force-disabled binary contains no Vulkan env names, renderer names, or
  factory fallback text.

## Unverified

- Requested/normalized/actual log output during ROM renderer creation (no ROM
  is present in the scoped workspace).
- Live renderer switching from a running ROM.
- Linux, macOS, and MoltenVK builds.
- CI execution.

## Known limitations

- Vulkan presentation is a policy enum only until Phase 4.
- Vulkan renderer IDs intentionally fall back to Software until Phase 6.
- No capability probe exists until Phase 3.
- Existing HUD golden drift remains unchanged from Phase 0.

## Rollback

Revert the Phase 2 commit. IDs 0..4 retain their previous persisted meaning, so
no config migration is required.
