# Vulkan retire-frame post-implementation re-audit — 2026-08-19

This record covers
`.codex/MelonPrimeDS_Vulkan_production-retire-mapping再監査_追加修正・CI検証指示_2026-08-19.md`.
The source baseline is `5efbad93c76501e6025a5f729689a91ce8a2b60f`
(`fix: correct Vulkan OOM retirement frame tagging`). The implementation
commits are `e774e054b` (`fix: preserve Vulkan last successful submission
number`) and `f4c84c9e3` (`test: cover one-slot Vulkan retirement failure
states`). The CI reachability adjustment is `179e2d110`
(`ci: run Vulkan retire check before macOS packaging`). The instruction file is
intentionally not modified or staged.

The result separates source/build/model evidence, hosted CI evidence, and
physical GPU evidence. A successful local build or static audit is not
promoted to a cross-platform driver or latency result.

## Result summary

| Gate | Status | Evidence |
| --- | --- | --- |
| Existing P2 Vulkan retire-frame and bounded OOM contract | PASS | The production `FrameRing` policy selects current-recording, last-submitted, or completed frame as appropriate; Vulkan texcache retirement and the bounded fake-OOM retry model remain intact. |
| P3 independent last-successful-submit semantics | PASS by source/build/model | `FrameRing` stores `LastSubmittedFrameNumber` independently from the one-slot `FrameContext::SubmittedFrame`, resets it on create/destroy, and updates it only after successful `EndCommandBuffer` and `QueueSubmit`. |
| P3 production `FrameRing` one-slot mapping direct test | PASS by source/build/model | `VulkanFrameRingTestAccess` seeds first-recording, same-slot recording reuse, and same-slot submit-failure states without a Vulkan device, while the test calls production `FrameRing::GetResourceRetireFrame()`, `GetLastSubmittedFrameNumber()`, and `GetCurrentRecordingFrameNumber()`. |
| Telemetry/layout configuration parity | PASS | The frame-retire test target now receives the same `MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY` definition as `core`, preventing a measurement-build class-layout/ODR mismatch. Both telemetry-ON and telemetry-OFF Windows builds pass the direct test. |
| Windows/Ubuntu/BSD/macOS CI target wiring | PASS by workflow/YAML audit | Each workflow explicitly invokes `melonprime_vulkan_frame_retire_check`; workflow YAML parses successfully. |
| Hosted Windows CI at `179e2d110` | PASS | Run [`32217654903`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32217654903) checked out `179e2d110c4714baa6a677c8754cbdb285dcc77e`; the workflow/job and `Run production Vulkan retire-frame mapping test` step succeeded. |
| Hosted macOS CI at `179e2d110` | TARGET PASS / JOB FAILURE | Run [`32217654921`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32217654921) ran the dedicated retire step successfully on both x86_64 and arm64. Both jobs later failed in the pre-existing Classic On-Screen Edit geometry check (`T10`/17pt controls), unrelated to this Vulkan change. |
| Hosted Ubuntu CI at `179e2d110` | OPEN / IN PROGRESS | Run [`32217655118`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32217655118) was still in the Vulkan shader-toolchain audit when this record was prepared; x86_64/aarch64 build jobs and their retire steps are not yet verified. |
| Hosted BSD CI at `179e2d110` | OPEN / PARTIAL | Run [`32217655100`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32217655100) had FreeBSD fail during VM startup before the retire target, while NetBSD/OpenBSD VM jobs were still in progress; no BSD-wide PASS is claimed. |
| Physical DX12/Vulkan/OpenGL Compute A/B and hardware acceptance | OPEN / NOT RUN | The required same-ROM, warmed, randomized 3-run latency matrix and validation-layer/capture evidence were not run. |

## Implementation

`FrameRing::BuildResourceRetireFrameState()` is the single production
extraction point used by `GetResourceRetireFrame()`. The model test has a
test-only friend access seam in the test translation unit; it does not add a
shipping API or production symbol. The direct assertions therefore exercise
the production mapping rather than only duplicating its pure policy helper.

The one-slot ring now keeps `LastSubmittedFrameNumber` independently from the
slot's mutable `SubmittedFrame`. `BeginFrameInternal()` changes the slot's
recording number but does not change the last-successful-submit number;
`SubmitFrame()` updates the independent number only after both command-buffer
end and queue submission succeed. The test seam mirrors production's
`AbsoluteFrame` rule: current recording frame, otherwise last successful frame
plus one, otherwise frame one.

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
| Independent last-submit and one-slot failure-state audit | PASS | The explicit late-fence audit checks the independent member, create/destroy resets, successful-submit-only update ordering, production seam formula, and first-recording/same-slot/submit-failure test cases. |
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

- Complete and retain the Ubuntu x86_64/aarch64 and BSD FreeBSD/NetBSD/OpenBSD
  retire-target logs for the final CI verification revision. Windows is PASS;
  macOS's retire target is PASS on both architectures but its surrounding job
  remains red because of the unrelated Classic layout geometry check.
- Run the physical DX12/Vulkan/OpenGL Compute A/B on the same ROM/save,
  location, actions, scale matrix, VSync/low-latency matrix, and randomized
  three-run protocol; report p50/p95/p99/max and renderer telemetry.
- Retain visual parity, no-crash/device-reset/savestate checks, validation
  layer lifetime errors of zero, and capture evidence for the physical runs.

Accordingly, the requested production test hardening and CI wiring are
implemented and locally validated. Hardware-level latency and hosted
cross-platform evidence remain unverified and are not represented as PASS.
