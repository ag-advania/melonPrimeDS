# Vulkan/DX12 memory admission and diagnostic overhead

This document defines the production memory-accounting boundary introduced by
the 2026-08-18 memory/performance audit. It is a safety contract, not a claim
that every driver allocation is observable by the diagnostic model.

## Production contract

The normal Release path keeps only the state needed to make a safe admission
decision:

- Vulkan's live `VK_EXT_memory_budget` query is refreshed at device
  initialization and immediately before a scale-dependent resource
  recreation. It is not queried from `RenderFrame()` or the present steady
  state.
- The shared `VulkanMemory.cpp::AllocateFor()` path reserves a small POD
  count/byte state before `vkAllocateMemory` and releases that reservation if
  the driver allocation fails. The accepted path does not copy a capability
  snapshot, create a diagnostic `std::string`, or copy a detailed telemetry
  snapshot.
- `MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY` defaults to `OFF`. Peak counters,
  totals, per-heap byte counters, size buckets, and boundary log formatting
  are compiled only when that option is explicitly enabled for a diagnostic
  build. The OFF facade is constexpr/no-op.
- Scale admission remains fail-safe: invalid types, allocation limits,
  arithmetic overflow, live-budget reserve exhaustion, and heuristic-budget
  overflow refuse the request. A refused scale/resource creation must keep
  the previous usable resource or follow the existing caller fallback.

DX12 `CreateBuffer()` and `CreateTexture2D()` rely on
`CreateCommittedResource()` for the API's actual alignment/placement and
HRESULT validation. They do not issue a redundant
`GetResourceAllocationInfo()` query for every resource. DX12 scale admission
remains a separate boundary check.

## Direct Vulkan allocations and accounting scope

The Vulkan texture heap separates logical reservation from the physical
device-allocation path and also has a temporary spill path:

| Path | Allocation | Admission/diagnostic accounting | Failure behavior |
| --- | --- | --- | --- |
| `VulkanTextureHeap::Reserve()` | Logical texcache image-array identity only; no Vulkan object is created | No driver allocation occurs at this boundary, so no memory reservation is charged here | Return the opaque handle with `PendingCreate` set; physical creation is deferred to `MaterializePendingCreates()` |
| `VulkanTextureHeap::MaterializePendingCreates()` | Persistent device-local texcache image, memory, binding, and view | Bypasses the scale planner's persistent reservation counters for on-demand cache growth; `texture_resource_create_us` and materialization counters remain available | Create before the reuse-fence wait; on `VK_ERROR_OUT_OF_DEVICE_MEMORY` or `VK_ERROR_OUT_OF_HOST_MEMORY`, retire deferred objects and retry once; final failure latches renderer runtime failure after cleaning partial objects |
| `CreateScratchUpload()` | Temporary host-visible upload buffer when the frame staging ring is full | Bypasses the persistent reservation counters and detailed telemetry; `VulkanPerf` still reports scratch-upload count/bytes when renderer telemetry is enabled | Return `false`; the upload caller logs and drops that upload, and successful spill objects retire through the frame destroy queue |

This means the detailed GPU-memory model is intentionally not a complete
driver-allocation census. It is a diagnostic view of the wrapped allocation
path only. The physical texcache path remains safe because every Vulkan call is
checked, partial objects are destroyed on failure, and the driver is the final
authority for the on-demand allocation. The bounded retire-and-retry slow path
does not turn a device-lost or contract failure into an OOM retry and does not
retry indefinitely. Any future change that moves these paths into shared
accounting must preserve the Release zero-overhead contract and add a matching
A/B measurement.

## Validation contract

The source and Release-binary gate is
`tools/ci/audits/audit-renderer-memory-production-overhead.py`. It verifies
the DX12 preflight removal, boundary-only live queries, POD admission reasons,
the compile-time telemetry split, Release workflow flags, and absence of
detailed-memory log strings from an audited shipping executable.

The relevant local checks are:

```text
python tools/ci/audits/audit-renderer-memory-production-overhead.py
python tools/ci/audits/audit-renderer-perf-zero-overhead.py
```

`MELONPRIME_ENABLE_GPU_MEMORY_TELEMETRY=ON` is reserved for a diagnostic
build that exercises `vulkan-memory-telemetry-tests`; Release and shipping
workflow configurations must state `OFF` explicitly.
