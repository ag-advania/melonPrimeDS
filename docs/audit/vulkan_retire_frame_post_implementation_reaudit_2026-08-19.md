# Vulkan retire-frame post-implementation re-audit — 2026-08-19

This record covers
`.codex/MelonPrimeDS_Vulkan_retire-frame実装後再監査_残件検証指示_2026-08-19.md`.
The source baseline is `5efbad93c76501e6025a5f729689a91ce8a2b60f`
(`fix: correct Vulkan OOM retirement frame tagging`). The instruction file is
intentionally not modified or staged. This follow-up is currently a working
tree change on top of that baseline; the eventual commit SHA is not asserted
here.

The result separates source/build/model evidence, hosted CI evidence, and
physical GPU evidence. A successful local build or static audit is not
promoted to a cross-platform driver or latency result.

## Result summary

| Gate | Status | Evidence |
| --- | --- | --- |
| Existing P2 Vulkan retire-frame and bounded OOM contract | PASS | The production `FrameRing` policy selects current-recording, last-submitted, or completed frame as appropriate; Vulkan texcache retirement and the bounded fake-OOM retry model remain intact. |
| P3 production `FrameRing` mapping direct test | PASS by source/build/model | `VulkanFrameRingTestAccess` seeds lifecycle state without a Vulkan device, while the test calls production `FrameRing::GetResourceRetireFrame()`, `GetLastSubmittedFrameNumber()`, and `GetCurrentRecordingFrameNumber()`. No-submission, submitted, and recording cases pass. |
| Telemetry/layout configuration parity | PASS | The frame-retire test target now receives the same `MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY` definition as `core`, preventing a measurement-build class-layout/ODR mismatch. Both telemetry-ON and telemetry-OFF Windows builds pass the direct test. |
| Windows/Ubuntu/BSD/macOS CI target wiring | PASS by workflow/YAML audit | Each workflow explicitly invokes `melonprime_vulkan_frame_retire_check`; workflow YAML parses successfully. |
| Hosted cross-platform CI at this follow-up revision | NOT RUN | The working-tree follow-up has not been pushed or dispatched, so no hosted Windows/Linux/BSD/macOS result is claimed. |
| Physical DX12/Vulkan/OpenGL Compute A/B and hardware acceptance | OPEN / NOT RUN | The required same-ROM, warmed, randomized 3-run latency matrix and validation-layer/capture evidence were not run. |

## Implementation

`FrameRing::BuildResourceRetireFrameState()` is the single production
extraction point used by `GetResourceRetireFrame()`. The model test has a
test-only friend access seam in the test translation unit; it does not add a
shipping API or production symbol. The direct assertions therefore exercise
the production mapping rather than only duplicating its pure policy helper.

The test target uses
`melonprime_apply_renderer_perf_telemetry_definition()` so the `FrameRing`
layout seen by the test matches the linked `core` library in measurement
builds. This was required because the first telemetry-enabled build exposed
the mismatch: the source compiled, but the injected test state did not reach
the production fields. After the CMake correction, both configured Windows
variants pass.

The following workflow steps now explicitly exercise the target:

- `.github/workflows/build-windows.yml`
- `.github/workflows/build-ubuntu.yml`
- `.github/workflows/build-bsd.yml`
- `.github/workflows/build-macos.yml`

## Validation evidence

| Check | Status | Evidence |
| --- | --- | --- |
| Production late-fence/retire contract audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| GPU memory production-overhead audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Low-latency contract audit | PASS | `python tools/ci/audits/audit-low-latency-contract.py` |
| Linux Vulkan presenter-retire audit | PASS | `python tools/ci/audits/audit-vulkan-linux-presenter-retire-contract.py` |
| Workflow YAML syntax | PASS | Python `yaml.safe_load` over all four modified workflows |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings |
| Measurement Windows build | PASS | `build/rebuild-mingw-x86_64`; developer features, renderer telemetry, and Vulkan latency capture enabled; full build and `vulkan-frame-retire-tests: PASS` |
| Shipping Windows build | PASS | `build/release-mingw-shipping-x86_64`; developer features and renderer telemetry disabled; full build and `vulkan-frame-retire-tests: PASS` |

These local results are source, model, and Windows build evidence only. They
do not establish GPU runtime behavior, physical frame-time parity, or hosted
cross-platform coverage.

## Remaining acceptance gates

The following remain explicitly open:

- Dispatch the modified workflows at the final pushed revision and retain
  Windows/Linux/BSD/macOS logs for the production mapping target. MoltenVK is
  covered only where the macOS workflow target is enabled.
- Run the physical DX12/Vulkan/OpenGL Compute A/B on the same ROM/save,
  location, actions, scale matrix, VSync/low-latency matrix, and randomized
  three-run protocol; report p50/p95/p99/max and renderer telemetry.
- Retain visual parity, no-crash/device-reset/savestate checks, validation
  layer lifetime errors of zero, and capture evidence for the physical runs.

Accordingly, the requested production test hardening and CI wiring are
implemented and locally validated. Hardware-level latency and hosted
cross-platform evidence remain unverified and are not represented as PASS.
