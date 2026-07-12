# Phase 1 verification record

Date: 2026-07-12 (Asia/Tokyo)

## Status

Phase 1 is implemented. The Vulkan build remains default OFF and exposes no
runtime renderer, presenter, UI, config, or environment-variable path.

## Commit

`build(vulkan): add complete Vulkan build gate and shader toolchain`

## Build verified

- Default OFF: Windows MinGW configure, compile, and link passed.
- Vulkan ON: Windows MinGW configure, SPIR-V generation, compile, and link
  passed with `MELONPRIME_ENABLE_VULKAN=ON`.
- Force-disabled: Windows MinGW configure, compile, and link passed with
  `MELONPRIME_ENABLE_VULKAN=ON` and `MELONPRIME_FORCE_DISABLE_VULKAN=ON`.
- The working build tree was restored to default OFF after the matrix.
- vcpkg dependency compilation is constrained to one job by the supported
  batch workflow; this avoids the observed `cc1plus.exe` OOM at `-j29`.

## Shader toolchain verified

- Compiler: glslang 16.3.0 from the opt-in vcpkg manifest feature.
- Target environment: Vulkan 1.1.
- Generated probe SPIR-V: 280 bytes.
- Source, manifest, compiler, SPIR-V, and generated-header hashes are recorded
  in `src/Vulkan_shaders/generated/manifest.lock.json`.
- Full compiler regeneration check: passed.
- Compiler-free CI metadata/payload integrity check: passed.
- Windows and Ubuntu CI audit jobs run the compiler-free drift check.

## Runtime verified

Phase 1 intentionally has no Vulkan runtime path. The default-OFF developer HUD
golden harness completed with the same Phase 0 output SHA-256:
`8CC5500A2D7871565D3512129ECA1B640AD00CE0D1398A41FC35D8DC1004B490`.

## Hardware

- Microsoft Windows 11 Home 10.0.22621, x86_64.
- NVIDIA GeForce RTX 5070 Ti (`VEN_10DE`, `DEV_2C05`).

## Validation

- Required config/schema/ownership/platform/thread/SRP audits passed.
- `git diff --check` passed.
- Force-disabled build graph contains no Vulkan source or compile definition.
- Force-disabled binary contains none of the Phase 1 Vulkan markers, shader
  metadata, `vkCreateInstance`, or `volkInitialize`.
- PE import inspection shows no Vulkan loader DLL dependency.

## Dependency policy

The vcpkg Vulkan feature provides Vulkan headers, volk, VMA, and the host
glslang tool only while the derived gate is active. The application does not
bundle `vulkan-1.dll`; later runtime phases use volk to load the system/driver
Vulkan loader. The OFF and force-disabled manifest resolutions remove the
opt-in packages from the build tree.

## Unverified

- Linux X11 and Wayland Vulkan ON builds.
- macOS default Metal regression and MoltenVK opt-in build.
- Release CI execution of the newly added workflow steps.
- Runtime Vulkan loader/device behavior (owned by Phase 3).

## Known limitations

- The sole shader is a build-only compute probe and is never dispatched.
- Existing HUD golden drift remains unchanged from Phase 0.
- Vulkan UI and strings are intentionally absent until Phase 12.

## Rollback

Revert the Phase 1 commit. Default-OFF builds require no config or runtime state
migration.
