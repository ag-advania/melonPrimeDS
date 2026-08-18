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

The Vulkan texture heap has two intentional direct-allocation paths:

| Path | Allocation | Admission/diagnostic accounting | Failure behavior |
| --- | --- | --- | --- |
| `VulkanTextureHeap::Create()` | Persistent device-local texcache image array | Bypasses the shared reservation counters and detailed telemetry; the scale planner covers planned scale resources, not on-demand cache growth | Destroy the image and return handle `0` when allocation/bind/view creation fails |
| `CreateScratchUpload()` | Temporary host-visible upload buffer when the frame staging ring is full | Bypasses the persistent reservation counters and detailed telemetry; `VulkanPerf` still reports scratch-upload count/bytes when renderer telemetry is enabled | Return `false`; the upload caller logs and drops that upload, and successful spill objects retire through the frame destroy queue |

This means the detailed GPU-memory model is intentionally not a complete
driver-allocation census. It is a diagnostic view of the wrapped allocation
path only. The direct paths remain safe because every Vulkan call is checked,
partial objects are destroyed on failure, and the driver is the final
authority for the on-demand allocation. Any future change that moves these
paths into shared accounting must preserve the Release zero-overhead contract
and add a matching A/B measurement.

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
