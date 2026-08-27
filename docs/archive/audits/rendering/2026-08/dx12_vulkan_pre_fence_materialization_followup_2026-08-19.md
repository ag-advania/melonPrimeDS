# DX12/Vulkan pre-fence materialization follow-up re-audit — 2026-08-19

This record covers the residual corrections requested by
`.codex/MelonPrimeDS_DX12_Vulkan_pre-fence_materialization再監査_追加修正指示_2026-08-19.md`.
The baseline is `6411ab7d90e737eccc475a556349c145e7e6ac9f`
(`perf: move DX12/Vulkan texture materialization before fence`). The
instruction document is intentionally not modified or staged.

The result separates source/build evidence from physical-runtime evidence. A
successful build or static audit is not promoted to a hardware, driver, or
frame-time result.

## Result summary

| Area | Status | Result |
| --- | --- | --- |
| P2-001: OOM retire-and-retry | PASS by source/build/model | DX12 and Vulkan keep normal physical creation pre-fence, classify retryable allocation failures, retire the frame slot, collect deferred resources, retry exactly once, and fail closed with a submission plus runtime failure if the retry is not ready. Vulkan deferred texcache destruction now tags the last frame that may reference the resource, and the fake-dispatch OOM model verifies destruction before the retry allocation. |
| P2-001: partial success and bounded state | PASS by source/build | Already materialized entries are marked ready and skipped on retry; the pending-slot worklist is cleared only after the complete pass succeeds; device-loss, invalid-memory-type, and other contract failures are terminal. |
| P2-002: typed decode storage | PASS by source/build | DX12/Vulkan use `std::unique_ptr<u32[]>` with `CapacityWords` and `UsedWords`; growth uses `new (std::nothrow) u32[words]` without value-initializing the decoder target. Allocation failure latches `UploadFailed`, returns an empty decode target, and is checked after `BuildPolygons()`. |
| P3-001/P3-002: evidence and audit contracts | PASS | The Vulkan memory-admission documentation and production-overhead audit describe the current APIs and retry contract; the prior audit record names commit `6411ab7d9` and baseline `f4e5f389`. |
| Physical A/B and runtime acceptance matrix | OPEN / NOT RUN | Same-ROM DX12/Vulkan physical comparison, hardware/driver validation, and warmed frame-time measurements were not run. |

## Implementation contract

The normal renderer order remains:

```text
CPU texture-cache update / polygon preparation
  -> BuildPolygons
  -> MaterializePendingCreates
  -> Commands.Begin(true) or Frames.BeginFrame(true)
  -> reset frame-local resources and collect retired objects
  -> retry only when the pre-fence result was retryable OOM
  -> RecordPendingUploads
  -> barriers, descriptors, submission, and present
```

DX12 classifies `E_OUTOFMEMORY` as retryable and
`DXGI_ERROR_DEVICE_REMOVED`, `DXGI_ERROR_DEVICE_RESET`, and
`DXGI_ERROR_DEVICE_HUNG` as device-loss failures. The one retry occurs after
`Commands.Begin(true)`, descriptor/upload reset, and
`TextureHeap.CollectGarbage()`. A failed retry submits `Commands.Submit()` and
latches the existing runtime failure.

Vulkan classifies `VK_ERROR_OUT_OF_DEVICE_MEMORY` and
`VK_ERROR_OUT_OF_HOST_MEMORY` as retryable. `VK_ERROR_DEVICE_LOST`, invalid
memory types, and other Vulkan results are terminal. The one retry occurs
after `Frames.BeginFrame(true)` has retired the slot and collected its
`DestroyQueue`; a failed retry submits
`Frames.SubmitFrame(Device.GetMainQueue())` and latches the existing runtime
failure. `VulkanTextureHeap::RetireEntry()` uses
`FrameRing::GetResourceRetireFrame()`: CPU-prep invalidations use the last
submitted frame, while invalidations during recording use the current
recording frame. Scratch upload buffers retain the latter semantics explicitly
through `GetCurrentRecordingFrameNumber()`.

The failure path has no unbounded retry or VSync-dependent behavior. The
shipping telemetry gate still compiles the counters and timers out. Enabled
reports expose:

```text
texture_materialize_pre_fence_fail_count
texture_materialize_retry_after_retire_count
texture_materialize_retry_success_count
texture_materialize_retry_fail_count
texture_materialize_failure_reason
```

## Source and build validation

| Check | Status | Evidence |
| --- | --- | --- |
| Explicit DX12/Vulkan late-fence and retry/storage contract audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| GPU memory production-overhead audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` — 24 constants and 11 pinned expressions |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Vulkan retire-frame policy and forced-OOM fake-dispatch test | PASS | `melonprime_vulkan_frame_retire_tests` — no-submission, last-submitted, recording, completion, and old-object-destroy-before-retry-allocation cases |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings were emitted |
| Shipping build, developer OFF and renderer telemetry OFF | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\release-mingw-shipping-x86_64 --jobs 1 --tail 160` — `[148/148]` |
| Measurement build, developer ON, renderer telemetry ON, Vulkan latency capture ON | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\rebuild-mingw-x86_64 --jobs 1 --tail 220` — `[154/154]` |

Both configured Windows builds passed their direct test sets, including the
registered-language Classic HUD layout cases, structured-capture dependency,
Vulkan present timing, Vulkan memory admission/telemetry, queue sharing,
surface lifecycle, presenter timeout, present-pacer fake dispatch, renderer
fallback, Intel XeLL state machine, and DX12 memory-admission tests. These
are model/build tests and do not establish physical GPU runtime coverage.

## Remaining acceptance gates

The following remain `OPEN / NOT RUN`:

- Physical same-ROM/location/action A/B on current DX12 and Vulkan binaries,
  with OpenGL Compute as the comparator.
- 1x/4x/16x scale, VSync OFF/ON, HUD OFF/ON, and vendor low-latency or Reflex
  OFF/ON matrix runs with warmed p50/p95/p99/max measurements.
- Physical AMD/Intel/Linux/macOS/BSD coverage, SyncVal/GPU-assisted
  validation, debug-layer validation, device-loss injection, and capture
  inspection.

Accordingly, the directed residual P2/P3 implementation is source/build/model
complete, while hardware-level latency and driver behavior remain unverified.
