# Vulkan/DX12 native GPU2D follow-up audit — 2026-08-19

This audit records the implementation committed for
.codex/MelonPrimeDS_Vulkan_DX12_GPU2D_実装指示書_develop_hud.md. The instruction
file remains an untracked user-provided file and is intentionally not included
in the implementation commit.

The record separates source/model/build evidence from physical renderer
evidence. A successful shader compile, C++ build, or static audit is not a
hardware exactness result.

## Commit and scope

| Item | Value |
| --- | --- |
| Repository | ag-advania/melonPrimeDS |
| Branch | develop_hud |
| Instruction baseline | b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4 |
| Implementation commit | 33c639835 gpu2d: add exact validation and stateful capture |
| Target backends | Vulkan and DirectX 12 |
| Software oracle | SoftRenderer canonical 6-bit logical Top/Bottom frames |
| Custom HUD | Not part of the logical comparison |

## Evidence summary

| Area | Status | Evidence |
| --- | --- | --- |
| Backend-neutral frame ABI | PASS by model/build | Fixed-width line state, memory mirrors, routing, generation fields, exact comparator, and upload-plan contract vectors |
| Native BG/OBJ/window/effect path | IMPLEMENTED / RUNTIME OPEN | Native Vulkan GLSL and DX12 HLSL evaluate register/VRAM/palette/OAM inputs; physical pixel parity has not yet been established |
| Exact logical comparator | PASS by model/build | 98,304 logical pixels are compared with zero tolerance; top/bottom/line/sample accounting is present |
| Dirty-range device upload | PASS by model/build | Retained CPU mirrors and 512-byte dirty ranges drive partial device copies after first-slot initialization |
| GPU LCDC capture mirror | IMPLEMENTED / RUNTIME OPEN | Four-bank device-resident mirror, changed LCDC range initialization, ordered capture dispatches and barriers |
| Normal-frame mandatory readback | PASS by source inspection | Native output remains GPU-resident; readback is only recorded when exact validation is explicitly enabled |
| Shader generation and validation | PASS | Vulkan 114 variants / 608 scale-specialized modules; DX12 generated source synchronization |
| C++ build | PASS | melonDS and melonprime_gpu2d_native_contract_vectors, rebuild-mingw-x86_64, one job |
| Isolated exact differential | OPEN / NOT RUN | No GPU harness was executed in this environment |
| Real-renderer Gate B | OPEN / NOT RUN | No compatible test ROM was present in the workspace |

## Implementation delivered

The follow-up adds canonical Software final-screen oracle storage, an explicit
MELONPRIME_GPU2D_EXACT_VALIDATE=1 developer gate, per-slot native output
readback for that gate, mismatch diagnostics, retained frame memory mirrors,
512-byte upload-range planning, and persistent GPU LCDC capture state. Native
Vulkan/DX12 composition consumes packed registers and memory rather than a
software-rendered pixel plane.

Capture state is kept in the unused tail of the existing blend-continuation
buffer. Normal capture-disabled frames do not issue the extra line capture
dispatches. The native output readback path waits only for the exact developer
validation submission and is not part of ordinary presentation.

## Validation commands

The following checks passed for implementation commit 33c639835:

- py tools/ci/audits/check-vulkan-shaders.py
- py tools/dx12/compile-shaders.py --check-source-sync
- py tools/ci/audits/audit-raster-software-parity.py
- py tools/ci/audits/audit-structured-composition-contract.py
- py tools/ci/audits/audit-renderer-perf-zero-overhead.py
- py tools/ci/audits/audit-renderer-physical-ab-contract.py
- git diff --check
- build/rebuild-mingw-x86_64 melonDS target
- build/rebuild-mingw-x86_64 melonprime_gpu2d_native_contract_vectors
- melonprime_gpu2d_native_contract_vectors: PASS

## Runtime boundary and remaining acceptance gates

The following are deliberately OPEN / NOT RUN:

- Vulkan and DX12 physical Gate A exact differential vectors for all required
  BG, OBJ, window, mosaic, blend, brightness, 3D insertion, routing, and
  capture cases.
- Same-ROM Vulkan/DX12 Gate B comparisons against Software with 49,152 Top
  pixels and 49,152 Bottom pixels per frame.
- Exact frame count, mismatch count, fallback-line count, upload bytes, GPU2D
  dispatch timing, and CPU 2D before/after timing from a physical run.
- Capture same-frame line timing, savestate/reset invalidation, renderer
  switching, device-loss, validation-layer, and AMD/Intel/Linux/macOS/BSD
  runtime coverage.

Known implementation constraints at this commit are that VulkanRenderer and
DX12Renderer still share the SoftRenderer host and therefore still generate
the CPU oracle path for validation/fallback, and startup/no-composed-output
fallback remains explicitly diagnosed. The remaining Phase I/IV acceptance
work must remove normal-frame CPU 2D pixel generation only after native exact
parity and the capture/savestate lifecycle have been demonstrated on real
renderers. This audit does not claim final instruction-sheet completion.
