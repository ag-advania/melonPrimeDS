# Vulkan/DX12 native GPU2D follow-up audit — 2026-08-19

This audit records the implementation committed for
`.codex/MelonPrimeDS_Vulkan_DX12_GPU2D_実装指示書_develop_hud.md`. The
instruction file remains an untracked user-provided file and is intentionally
not included in the implementation or audit commits.

The record separates source/model/build evidence from physical renderer
evidence. A shader compile, C++ build, or static audit is not by itself a
hardware exactness result.

## Commit and scope

| Item | Value |
| --- | --- |
| Repository | `ag-advania/melonPrimeDS` |
| Branch | `develop_hud` |
| Instruction baseline | `b4aecb3869d0f983a073cfa8b1ee28567b5ab8d4` |
| Final implementation commit | `c0805005a gpu2d: close native parity and savestate validation` |
| Audit documentation commit | `0d4d21910 docs: record USA Rev1 GPU2D parity matrix` |
| Target backends | Vulkan and DirectX 12 |
| Software oracle | SoftRenderer canonical 6-bit logical Top/Bottom frames |
| Exact comparison | 98,304 logical pixels per frame, zero tolerance |
| Custom HUD | Not part of the logical comparison |

## Evidence summary

| Area | Status | Evidence |
| --- | --- | --- |
| Backend-neutral frame ABI | PASS by model/build | Fixed-width line state, memory mirrors, routing, generation fields, exact comparator, and upload-plan contract vectors |
| Native BG/OBJ/window/effect path | PASS by physical Gate B | USA Rev 1 F1/F2/F3/F4/F5/F8 state-load matrix passed on both native backends at scale 1 |
| Exact logical comparator | PASS | All 12 physical runs reported `native_gpu2d_mismatches=0` and no exact-failure marker |
| Dirty-range device upload | PASS by model/build | Retained CPU mirrors and 512-byte dirty ranges drive partial device copies after first-slot initialization |
| GPU LCDC capture mirror | PASS by model/build | Four-bank device-resident mirror, changed LCDC range initialization, ordered capture dispatches and barriers |
| Normal-frame mandatory readback | PASS by source inspection | Native output remains GPU-resident; readback is only enabled by the developer exact-validation gate |
| Shader generation and validation | PASS | Vulkan 114 variants / 608 scale-specialized modules; DX12 117 variants across 3 scales; DX12 native path uses generated DXIL |
| C++ build | PASS | Developer build `[124/124]`; shipping build `[128/128]`; relevant tests passed in both configurations |
| Isolated exact differential | PASS | `melonprime_gpu2d_native_contract_vectors`: `PASS` |
| Real-renderer Gate B | PASS | 12/12 USA Rev 1 savestate-load runs passed with strict provenance and zero mismatch/fallback counters |

## Final USA Rev 1 savestate-load matrix

Inputs used for every row:

- ROM: `C:\DSMPH\melonPrimeDS\all roms\allRoms\0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`
- States: the matching `.ml1`, `.ml2`, `.ml3`, `.ml4`, `.ml5`, and `.ml8` files in the same directory
- Action: `savestate-load`, with the matching production shortcut `F1`, `F2`, `F3`, `F4`, `F5`, or `F8`
- Scale: `1`, VSync off, implementation/binary source SHA `c0805005ab6ed33c659b6c0fdbfd369a331a6fd8`
- Harness: `tools/testing/renderer-physical-ab.ps1` procedure `physical-ab-2026-08-19-v3`

`capture rows` is the captured latency-artifact row count; DX12 does not
produce Vulkan latency rows and therefore reports zero. It is not used as the
parity result. `config/state/provenance` means configuration restore,
savestate action marker, and source provenance respectively.

| Backend | State | Capture rows | Frame rows | Process | Config/state/provenance | Final renderer | Mismatch / native fallback / fallback lines |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- |
| DX12 | F1 | 0 | 336 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| DX12 | F2 | 0 | 341 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| DX12 | F3 | 0 | 341 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| DX12 | F4 | 0 | 340 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| DX12 | F5 | 0 | 339 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| DX12 | F8 | 0 | 340 | 0 | PASS / 1 / PASS | `DX12/DX12`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F1 | 352 | 353 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F2 | 384 | 385 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F3 | 352 | 353 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F4 | 352 | 353 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F5 | 350 | 351 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |
| Vulkan | F8 | 352 | 353 | 0 | PASS / 1 / PASS | `Vulkan/Vulkan`, fallback 0 | 0 / 0 / 0 |

The F3 run is specifically the savestate lifecycle regression case. It passed
after accelerated `PostSavestate()` was wired to reset and rebuild the
one-line-ahead Software OBJ sprite cache before exact comparison. This closes
the state-load mismatch that was observed in the earlier diagnostic run.

## Implementation delivered

The final implementation includes:

- exact validation against the real Software 3D line oracle, avoiding a
  transparent structured placeholder under the exact gate;
- Software OBJ sprite-cache reconstruction after savestate restore, wired from
  the actual Vulkan and DX12 accelerated `PostSavestate()` paths;
- correct native backdrop logical flags in Vulkan and DX12 shaders;
- a separate DX12 GPU2D capture shader pipeline generated as Shader Model 6.0
  DXIL, while retaining the existing DXBC route for the other shader paths;
- retained content/VRAM/capture generations and 512-byte dirty-range upload
  planning for native slots;
- strict physical-harness checks for exact mismatch, native fallback, and
  fallback-line counters; and
- ROM/state staging plus the actual requested `F$slot` shortcut in the
  physical harness, so the recorded state is the state under test.

The exact gate is developer-only and its readback is not part of ordinary
native presentation. The physical matrix above intentionally used the
developer build so the Software comparison oracle was active; the separate
shipping build also compiled and passed its test suite with developer
features and renderer telemetry disabled.

## Historical diagnostic closure

Earlier physical diagnostics were run against different, unverified binaries
and exposed two real issues: a native-vs-oracle mismatch in the logical
backdrop value, and an F3 post-savestate OBJ-cache mismatch. The backdrop flag,
real-oracle selection, and accelerated savestate cache rebuild fixes are in the
final implementation commit. The old failure is not used as current evidence;
the strict, provenance-verified 12-run matrix above is the acceptance result.

## Validation commands

The following checks passed for the final implementation source:

- `py tools/ci/audits/check-vulkan-shaders.py` — 114 variants, 608
  scale-specialized modules
- `py tools/dx12/compile-shaders.py --check-source-sync`
- `py tools/ci/audits/check-dx12-shaders.py --fxc "C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe"` — 117 variants across 3 scales
- `py tools/ci/audits/audit-raster-software-parity.py`
- `py tools/ci/audits/audit-structured-composition-contract.py`
- `py tools/ci/audits/audit-renderer-physical-ab-contract.py`
- `py tools/ci/audits/audit-renderer-perf-zero-overhead.py`
- `py -m py_compile tools/dx12/compile-shaders.py tools/ci/audits/check-dx12-shaders.py`
- `build/rebuild-mingw-x86_64/melonprime_gpu2d_native_contract_vectors.exe` — `PASS`
- developer C++ build — `[124/124]` and all registered tests `PASS`
- shipping C++ build — `[128/128]` and all registered tests `PASS`
- `git diff --check` — `PASS`

## Runtime boundary and remaining coverage

The physical result is Windows-only on an NVIDIA GeForce RTX 5070 Ti, at
scale 1, with VSync off and the savestate-load action. AMD, Intel, Linux,
macOS, BSD, other scales, and broader all-scene/performance matrices remain
`NOT RUN` here. The parity acceptance does not claim a performance gain: the
developer exact gate adds explicit validation readback, and that overhead must
not be mixed with ordinary shipping performance measurements.

The physical build records `git_dirty=true` because the user-provided
instruction file remains intentionally untracked. The source SHA and binary
SHA nevertheless matched exactly for all 12 runs, and provenance verification
was `PASS`.
