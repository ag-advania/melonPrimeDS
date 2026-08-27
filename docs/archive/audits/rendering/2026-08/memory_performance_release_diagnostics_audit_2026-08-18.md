# Memory/performance/release diagnostics audit — 2026-08-18

This is the implementation and evidence record for the instruction document
`.codex/MelonPrimeDS_memory_performance_release_diagnostics_audit_2026-08-18.md`.
The instruction document is intentionally not modified or committed. Generated
runtime logs and CSV files remain under the ignored `build/audit-runs/` tree.

The record separates source/build evidence from physical-runtime evidence. A
successful build is not promoted to a hardware, driver, or click-to-photon
result.

## Result summary

| Area | Status | Result |
| --- | --- | --- |
| DX12 per-resource allocation overhead | PASS | Removed redundant `GetResourceAllocationInfo()` preflights from `CreateBuffer()` and `CreateTexture2D()`; `CreateCommittedResource()` HRESULT handling and scale admission remain. |
| Vulkan Release allocation path | PASS | Detailed memory telemetry is compile-gated and defaults OFF; accepted reservation carries only minimal POD count/byte state. |
| Scale and memory safety | PASS | Admission limits, live-budget boundary checks, arithmetic-overflow rejection, and allocation rollback are retained and unit-tested. |
| Release diagnostic gate | PASS locally / mixed CI | Static source audit, shipping cache audit, shipping-binary string audit, and exact-head Windows release gate pass. The exact-head cross-platform workflow results are recorded below. |
| Same-GPU Vulkan/DX12 frametime smoke | PASS, diagnostic | Windows/NVIDIA RTX 5070 Ti, same ROM, 20-second startup/boot runs; no savestate result is used. |
| Physical memory-pressure failure run | OPEN | No controlled VRAM exhaustion or allocation-failure injection was available in this run. |
| AMD/Intel physical coverage | NOT RUN | No matching physical devices were available. |

## Implementation delivered

### DX12

`src/DX12Context.cpp` no longer calls `GetResourceAllocationInfo()` for every
ordinary buffer or texture creation. The actual `CreateCommittedResource()` call
remains the alignment, placement, and HRESULT authority. The independent scale
admission check remains in place so the optimization does not remove the
fail-safe refusal path.

### Vulkan

The shared reservation path now keeps only the minimal accepted allocation
count and reserved-byte state needed by admission. Detailed snapshot copies,
diagnostic `std::string` construction, heap buckets, peak counters, and
boundary formatting are compiled only with
`MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=ON`. The default and all shipping
presets/workflows explicitly set this option to `OFF`.

Admission reasons are a small enum rather than a dynamically constructed
string. The evaluator rejects invalid memory types/heaps, allocation/count
limits, projected-byte overflow, and budget exhaustion. A failed Vulkan driver
allocation releases the reservation.

The direct Vulkan texcache image-array and scratch-upload allocations are
documented as intentionally outside the shared reservation counters. Every
Vulkan call in those paths is checked; partial objects are destroyed and the
caller follows the existing safe fallback/drop behavior. Renderer telemetry, if
enabled, still reports scratch-upload counters, but the detailed GPU-memory
model is not presented as a complete driver-allocation census.

The production contract is maintained in
[`vulkan-memory-admission.md`](../../../../development/performance/vulkan-memory-admission.md).

## Source and build validation

| Check | Status | Evidence |
| --- | --- | --- |
| Memory production-overhead source audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py` |
| Renderer general zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Mouse input/savestate contract | PASS | `python tools/testing/test_mouse_input_savestate_contract.py` |
| Debug Vulkan validation, memory telemetry OFF | PASS | `build/debug-mingw-vulkan-validation2`, build script completed 242/242 steps |
| Release/dev-on renderer diagnostic build | PASS | `build/rebuild-mingw-x86_64`, GPU memory telemetry OFF; build script completed 251/251 steps |
| Release/dev-on latency-capture build | PASS | Same build tree with `MELONPRIME_VULKAN_LATENCY_CAPTURE=ON`; build script completed 242/242 steps |
| Shipping Release build | PASS | `build/release-mingw-shipping-x86_64`, Vulkan/DX12 enabled, GPU memory telemetry OFF; build script completed 290/290 steps |
| Shipping binary memory audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py --binary build/release-mingw-shipping-x86_64/melonPrimeDS.exe` |
| Whitespace/error check | PASS | `git diff --check` |

The Debug OFF and ON configurations both passed the Vulkan memory-admission
and telemetry model tests, including projected-reservation overflow handling.
The full validation sets also passed the 82 registered-language Classic HUD
geometry cases, structured capture dependency tests, Vulkan lifecycle/pacer/
timeout/fallback tests, and DX12 memory-admission tests.

The shipping cache recorded:

```text
CMAKE_BUILD_TYPE=Release
MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF
MELONPRIME_ENABLE_DX12=ON
MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=OFF
MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY=OFF
MELONPRIME_ENABLE_VULKAN=ON
MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=OFF
```

The audited shipping executable SHA-256 was
`0C0CEC903A31A22FB196287F590F14866842818351913E6353665AAAEC9E0CD8`.

## Physical frametime A/B

Host and run conditions:

- Windows host; physical GPU: NVIDIA GeForce RTX 5070 Ti.
- Vulkan instance API 1.4.357; device API 1.4.341; driver reported as
  610.74.0.0.
- ROM: `0367 - Metroid Prime - Hunters (USA) (Rev 1).nds`.
- Renderer perf telemetry enabled, GPU memory telemetry disabled, VSync off,
  native 1x scale, 20 seconds after startup.
- Baseline executable: historical dev-on diagnostic binary copied from the
  ignored audit run (`SHA-256 3BEC206F85B247661D5E8C9947F522DE2F4EB03C928A286D123E23C3B9DFE980`).
  It is not an exact-current-SHA rebuild, so these numbers are diagnostic A/B
  evidence rather than a causal performance proof.
- The old binary's slot-4 savestate path produced an ARM9 data abort. That
  measurement was excluded; the table is startup/boot only and does not claim
  savestate parity.

`1% low equivalent` below is the conventional `1000 / frame_ms_p99` conversion
from the shutdown frame distribution. It is not a measured display-present FPS
and does not replace a controlled game-scene or click-to-photon test.

| Renderer | Build | Frames | p50 ms | p95 ms | p99 ms | Longest frame ms | 1% low equivalent |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Vulkan | baseline | 1,170 | 16.674 | 19.822 | 21.181 | 123.286 | 47.21 FPS |
| Vulkan | current source | 1,169 | 16.689 | 19.008 | 20.535 | 140.159 | 48.70 FPS |
| DX12 | baseline | 1,159 | 16.605 | 19.287 | 20.615 | 178.958 | 48.51 FPS |
| DX12 | current source | 1,155 | 16.686 | 19.029 | 20.700 | 184.424 | 48.31 FPS |

The current Vulkan runner also passed the 1x raster differential, with zero
mismatched pixels in the reported frames. The current DX12 runner likewise
passed with zero mismatched pixels. The no-savestate run exercised renderer
startup, normal frame submission, and shutdown/recreate transitions; it did not
exercise a map transition, weapon switch, effects stress scene, or injected
VRAM exhaustion.

## High-resolution and upload-pressure observations

The same baseline/current binaries were run at `3D.GL.ScaleFactor = 16` for
the same 20-second startup/boot window. These are useful stress observations,
not a strict regression gate because the baseline is historical and the run
does not drive a controlled gameplay scene.

| Renderer | Build | p50 ms | p95 ms | p99 ms | Longest frame ms | Upload-pressure observation |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Vulkan | baseline | 16.703 | 19.077 | 20.382 | 2,038.850 | `scratch_uploads=0`, `scratch_upload_B=0` |
| Vulkan | current source | 16.681 | 18.966 | 20.416 | 157.192 | `scratch_uploads=0`, `scratch_upload_B=0` |
| DX12 | baseline | 16.633 | 19.068 | 20.189 | 1,984.562 | `spill_B=0`, `upload_overflows=0` |
| DX12 | current source | 16.644 | 19.147 | 20.390 | 526.303 | `spill_B=0`, `upload_overflows=0` |

The available renderer counters expose upload bytes and spill/scratch counts,
but not a complete resource-create/destroy or texcache-create/destroy count.
Those two requested counts are therefore `NOT AVAILABLE` in this run rather
than inferred from descriptor counters. The normal-scale current 10-window
counter summaries recorded Vulkan `texture_upload_B=873,728` and DX12
`texture_upload_B=903,168`; scratch/spill and overflow counters remained zero.

## Input-to-present capture

The current Vulkan source was rebuilt with
`MELONPRIME_VULKAN_LATENCY_CAPTURE=ON` while keeping GPU memory telemetry OFF.
The run wrote 1,169 samples; after the required 600-frame warmup, 569 samples
were aggregated:

| Metric | Current Vulkan capture |
| --- | ---: |
| Frame p50 / p95 / p99 | 16.662 / 19.432 / 20.798 ms |
| Host input-sample → present-end p50 | 4.494 ms |
| Host input-sample → present-end p95 | 7.617 ms |
| Host input-sample → present-end p99 | 8.875 ms |
| Invalid rows | 0 |

This is a host input-marker-to-present-end interval, not click-to-photon. The
runner did not inject physical mouse input (`input_src` remained zero), and the
historical baseline executable was not capture-enabled, so a same-runner
baseline input-to-present A/B is `OPEN/NOT RUN`. The one current run is also
not a statistically sufficient latency study; the aggregation tool explicitly
requires at least three randomized runs per mode.

## Memory-safety and failure behavior

| Failure or safety case | Status | Evidence/limit |
| --- | --- | --- |
| Invalid type/heap and allocation/count limit refusal | PASS | Admission unit tests and enum reason coverage |
| Projected-byte arithmetic overflow | PASS | `vulkan-memory-admission-tests` regression vector |
| Driver allocation failure releases reservation | PASS by code/test contract | Reservation rollback is retained; no forced physical OOM was run |
| Scale admission refuses unsafe recreation | PASS | Boundary admission tests and existing fallback path |
| Direct texcache/scratch partial-allocation cleanup | PASS by source contract | Vulkan call checks and cleanup paths audited/documented |
| Real VRAM pressure / driver OOM | OPEN | No reproducible controlled pressure injector in the available host |

## Exact-head GitHub Actions

The implementation commit is `a2c2d304fb1b91c21c0ee4d7f6b7ba85aed19980`.
The branch's workflow triggers do not run on `develop_hud` pushes, so these
workflow-dispatch runs were explicitly launched against that SHA:

| Workflow | Run | Workflow result | Evidence |
| --- | ---: | --- | --- |
| Windows | `32139893658` | PASS | The complete build job passed, including the GPU memory source audit, shipping Release feature-gate check, shipping-binary memory-string audit, raster edge vectors, shader sync, and Vulkan-disabled variant. |
| Ubuntu | `32139893296` | SUCCESS with non-gating platform failures | The `Audits` job passed, including the GPU memory audit. The aarch64/x86_64 build jobs and artifact aggregation reported runner `git` exit 128/build-artifact failures. |
| macOS | `32139893246` | FAILURE | arm64/x86_64 GPU memory audits passed, but both builds failed in the existing runner git/Homebrew trust/CMake environment before runtime validation. |
| BSD | `32139893725` | FAILURE | The GPU memory source audit passed; OpenBSD/NetBSD BSD validation builds failed, and FreeBSD failed during dependency installation. |

All four runs reported `headSha` equal to the implementation commit above. The
platform failures are retained as failures and are not reclassified as product
passes based on the local Windows build.

## Remaining evidence gates

| Gate | Status |
| --- | --- |
| Exact-head GitHub Actions result | MIXED; Windows and audit gates PASS, platform matrix has environment/build failures |
| AMD Anti-Lag / AMD Vulkan physical run | NOT RUN |
| Intel XeLL / Intel Vulkan physical run | NOT RUN |
| Linux/macOS/BSD physical renderer run | NOT RUN |
| Savestate-based texture churn/map transition/weapon/effects matrix | OPEN; historical binary aborted on the supplied state |
| Controlled texcache churn and upload-ring spill injection | OPEN |
| Physical click-to-photon or Reflex Analyzer | NOT RUN |

These limits are intentionally preserved. The source, configured builds, local
binary gate, same-GPU renderer smoke, high-resolution startup stress, and
current Vulkan host-latency capture are complete; unavailable hardware and
uncontrolled scenarios are not represented as PASS.
