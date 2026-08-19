# DX12/Vulkan texture materialization re-audit — 2026-08-19

This is the implementation and evidence record for
`.codex/MelonPrimeDS_DX12_Vulkan_texture_materialization再監査_修正指示_2026-08-19.md`.
The baseline for this follow-up is `f4e5f3890b2af0ba9e40297b3daf25244b000a1e`
(`docs: record final renderer revalidation`). The implementation described
below was committed at
`6411ab7d90e737eccc475a556349c145e7e6ac9f`
(`perf: move DX12/Vulkan texture materialization before fence`). The
instruction document is intentionally not modified or staged.

The record separates source/build evidence from physical-runtime evidence. A
successful build or static audit is not promoted to a hardware, driver, or
frame-time result.

## Result summary

| Area | Status | Result |
| --- | --- | --- |
| P2-001: independent physical texture creation | PASS by source/build | DX12 and Vulkan materialize pending resources before the reuse-fence wait (`Commands.Begin(true)` / `Frames.BeginFrame(true)`), so host-side resource creation does not serialize on that wait. |
| P2-002: pending-create scan and cancellation | PASS by source/build | Reservation records a slot in `PendingCreateSlots`; materialization visits only that worklist, and destruction erases cancelled slots before a stale slot can be reused. |
| P2-003: pending direct-decode storage | PASS by source/build | DX12/Vulkan pending upload storage is `std::vector<u32>`, sized in words and exposed with a typed `u32*` plus byte capacity; the former reinterpret cast is removed. |
| Pending-storage growth observability | PASS by source/build | Growth count, allocated-byte total, and CPU time are reported as `texture_pending_storage_grow_count`, `texture_pending_storage_grow_bytes`, and `texture_pending_storage_grow_us`. |
| Maintained renderer invariants | PASS by source/build | One frame in flight, bounded waits, post-fence uploads/barriers/descriptors, structured dirty batching, release telemetry compile-out, and fail-closed upload/runtime failure paths remain intact. |
| Physical A/B and runtime acceptance matrix | OPEN / NOT RUN | Same-ROM physical DX12/Vulkan comparison, warmed p50/p95/p99/max measurements, and hardware validation were not run in this task. |

## Implementation delivered

### Materialization ordering

The explicit backends now use this order:

```text
CPU texture-cache update / polygon preparation
  -> BuildPolygons
  -> MaterializePendingCreates
  -> Commands.Begin(true) or Frames.BeginFrame(true)
  -> reset frame-local upload/descriptor resources
  -> RecordPendingUploads
  -> barriers, descriptors, geometry submission, and present
```

`MaterializePendingCreates()` is deliberately limited to host-side physical
resource creation. It does not touch the command list, frame-local allocator,
upload recorder, staging buffer, or descriptor state. Therefore a creation
failure is detected before `Begin()`/`BeginFrame()`; the backend latches the
existing runtime failure and returns without submitting a frame.

The fence wait still protects reuse of per-frame GPU-visible resources. Upload
recording, transitions/barriers, descriptor writes, and submission remain in
the post-wait phase. This preserves the one-slot and bounded-wait policy while
removing the independent host-side creation work from the wait's critical
section.

### Pending-create worklist

DX12 and Vulkan reserve a slot in `PendingCreateSlots` when a logical texture
entry becomes pending. Materialization iterates:

```cpp
for (u32 slot : PendingCreateSlots)
```

It validates the slot and entry state, skips entries that were already
materialized or cancelled, and clears the worklist only after the complete
pass succeeds. If one creation fails, the existing failure-latch behavior is
retained and the worklist remains available for the established retry/failure
semantics; already-created entries are skipped on a subsequent call.

`Destroy()` erases the slot from the worklist as part of cancellation. This
prevents a pending slot from surviving into a later reservation that reuses
the same index.

### Typed reusable decode storage

The backend-owned pending upload pool now stores decoded words as
`std::vector<u32>`. `AcquirePendingUpload()` resizes in words, and
`BeginTextureUpload()` returns the typed data pointer with
`Data.size() * sizeof(u32)` byte capacity. The DX12/Vulkan direct-decode path
therefore no longer relies on `reinterpret_cast<u32*>` over byte storage.

The optional storage-growth telemetry records only actual capacity growth. It
reports growth count, the new allocated capacity in bytes, and the scoped CPU
time for the growth operation. In shipping builds these counters and timers
remain compile-time disabled with the existing renderer telemetry gate.

### Telemetry contract

The explicit backend reports retain the previous texture metrics and add:

```text
texture_decode_us
texture_pending_cpu_copy_us
texture_resource_create_us
texture_materialize_count
texture_pending_upload_bytes
texture_pending_upload_count
texture_pending_storage_grow_us
texture_pending_storage_grow_count
texture_pending_storage_grow_bytes
```

The report still exposes p50, p95, p99, and max for CPU timings plus counter
values. No shipping telemetry or unbounded history was introduced.

## Source and build validation

| Check | Status | Evidence |
| --- | --- | --- |
| Explicit DX12/Vulkan materialization and structured producer audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` — 24 constants and 11 pinned expressions |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings were emitted |
| Shipping build, developer OFF and renderer telemetry OFF | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\release-mingw-shipping-x86_64 --jobs 1 --tail 120` — `[149/149]` |
| Measurement build, developer ON, renderer telemetry ON, Vulkan latency capture ON | PASS | `cmd /c tools\build\windows\build-mingw-existing.bat --build-dir build\rebuild-mingw-x86_64 --jobs 1 --tail 160` — `[153/153]` |

Both configured builds passed the existing direct test set, including the 82
registered-language Classic HUD layout cases, structured-capture dependency,
Vulkan present timing, Vulkan memory admission/telemetry, queue sharing,
Linux/direct surface lifecycle, presenter timeout, present-pacer fake
dispatch, renderer fallback, Intel XeLL state machine, and DX12
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
  repeated randomized runs per case.
- Physical AMD/Intel/Linux/macOS/BSD coverage, SyncVal/GPU-assisted
  validation, debug-layer validation, device-loss injection, and capture
  inspection.

Accordingly, this task is source/build complete for the directed P2-001,
P2-002, and P2-003 corrections, but it does not claim that hardware-level
texture-materialization latency has been completely eliminated without the
physical A/B evidence above.
