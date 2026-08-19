# DX12/Vulkan late-fence re-audit — 2026-08-19

This is the implementation and evidence record for
`.codex/MelonPrimeDS_DX12_Vulkan_late-fence実装後再監査_追加修正指示_2026-08-19.md`.
The instruction document is intentionally not modified and remains an
untracked user instruction file. Only the implementation, audit, and this
record are included in the change.

The record separates source/build evidence from physical-runtime evidence. A
successful build or static audit is not promoted to a hardware, driver, or
click-to-photon result.

## Result summary

| Area | Status | Result |
| --- | --- | --- |
| DX12 logical reservation and late physical materialization | PASS by source/build | `Reserve()` creates the logical handle and marks `PendingCreate`; `MaterializePendingCreates()` performs `CreateTexture2D` only after `Commands.Begin(true)`, before pending upload recording. |
| Vulkan logical reservation and late physical materialization | PASS by source/build | `Reserve()` creates the logical handle and marks `PendingCreate`; `MaterializePendingCreates()` performs image allocation, binding, and view creation only after `Frames.BeginFrame(true)`, before pending upload recording. |
| Direct generic texture decode | PASS by source/build | The generic decoder targets backend-owned reusable pending CPU storage through `TextureDecodeTarget`; the former common `DecodingBuffer -> PendingUpload::Data` copy is not used on DX12/Vulkan. |
| DX12/Vulkan structured 2D telemetry routing | PASS by source/build | A backend-neutral wrapper selects DX12 or Vulkan once at frame start; shared Soft 2D producers do not perform per-pixel backend type checks or call a fixed backend directly. |
| Metrics and telemetry contract | PASS by source audit | Decode, pending-copy, resource-create, materialize-count, pending-upload-byte/count, p50/p95/p99/max report fields, and telemetry compile gates are present for both explicit backends. |
| Shipping and measurement builds | PASS | DX12/Vulkan Release shipping build completed `[151/151]`; the developer/telemetry/latency measurement build completed `[157/157]`. |
| Physical A/B and runtime acceptance matrix | OPEN / NOT RUN | Same-ROM physical DX12/Vulkan/OpenGL-Compute comparison, scale/VSync/HUD/low-latency matrix, and warmed p50/p95/p99/max measurements were not run in this task. |

## Implementation delivered

### Explicit texture lifetime

DX12 and Vulkan now separate the logical texture-cache entry from the
physical GPU resource. A reservation records dimensions, layer, handle
identity, `InUse`, and `PendingCreate` state without allocating a resource or
creating a view. Resource creation is performed in the frame's post-reuse
phase:

```text
CPU texture-cache update / polygon preparation
  -> Commands.Begin(true) or Frames.BeginFrame(true)
  -> MaterializePendingCreates()
  -> RecordPendingUploads()
  -> descriptor setup and geometry submission
```

Physical readiness is checked before descriptor/SRV use. A pending entry that
is destroyed before materialization is cancelled and returned to the free-slot
pool without entering a GPU retirement queue. Already-materialized resources
continue to use the existing DX12 graveyard or Vulkan deferred-destruction
queue. Creation and upload failures latch an explicit runtime failure and stop
submission through the existing fallback path.

### Direct decode and reusable pending storage

DX12 and Vulkan expose a reusable pending-upload storage pool. The generic
texture decoder receives a `u32*` target, byte capacity, and upload token,
decodes directly into that storage, and commits the token after successful
decode. This removes the extra common decoded-buffer copy and avoids a fresh
per-frame pending allocation. The fallback path remains available for
OpenGL/Metal and for uploads that cannot be recorded during an active frame.

### Backend-neutral structured 2D measurement

`MelonPrimeStructuredPerf.h` provides the shared `Soft2DTotal` and
`Structured2DMetadata` timer wrapper. `GPU_Soft` selects the renderer backend
once for the frame, and `GPU2D_Soft` uses that captured backend for the
structured producer timers. The wrapper is compile-time disabled in shipping
builds when renderer telemetry is off.

### Metrics

The explicit backend telemetry headers expose these common metrics and
counters:

```text
texture_decode_us
texture_pending_cpu_copy_us
texture_resource_create_us
texture_materialize_count
texture_pending_upload_bytes
texture_pending_upload_count
```

The report includes `p50`, `p95`, `p99`, and `max` for CPU timings and the
corresponding counter values. `texture_pending_cpu_copy_us` measures the
fallback copy into reusable pending storage; the normal DX12/Vulkan decode
path writes directly into that storage.

## Source and build validation

| Check | Status | Evidence |
| --- | --- | --- |
| Explicit late-fence and structured producer audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings were emitted |
| Release DX12/Vulkan shipping build, developer OFF, telemetry OFF | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\release-mingw-shipping-x86_64 --jobs 1 --tail 120` — `[151/151]` |
| Release DX12/Vulkan measurement build, developer ON, telemetry ON, latency capture ON | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\rebuild-mingw-x86_64 --jobs 1 --tail 180` — `[157/157]` |

The configured builds also passed the existing direct test set, including the
82 registered-language Classic HUD layout cases, structured-capture
dependency, Vulkan present timing, Vulkan memory admission/telemetry, queue
sharing, Linux/direct surface lifecycle, presenter timeout, present-pacer
fake dispatch, renderer fallback, Intel XeLL state machine, and DX12
memory-admission tests. These are model/build tests and do not establish
physical GPU runtime coverage.

## Runtime boundary and remaining acceptance gates

The following remain `OPEN / NOT RUN` for this task:

- Physical same-ROM/location/action A/B on current DX12 and Vulkan binaries,
  with OpenGL Compute as the comparator.
- 1x/4x/16x scale, VSync OFF/ON, HUD OFF/ON, and vendor low-latency or Reflex
  OFF/ON matrix runs.
- Warmed p50/p95/p99/max measurements across steady rendering, weapon switch,
  effects, map/room transitions, scoreboard, and capture workloads, with
  three randomized runs per case.
- Physical AMD/Intel/Linux/macOS/BSD coverage, SyncVal/GPU-assisted
  validation, debug-layer validation, device-loss injection, and capture
  inspection.

The one-slot Vulkan policy, bounded waits, fence reuse, VSync/present waits,
and failure latches were intentionally retained. No frame-count increase,
synchronization removal, forced VSync disable, silent texture failure, or
shipping telemetry was introduced by this change.
