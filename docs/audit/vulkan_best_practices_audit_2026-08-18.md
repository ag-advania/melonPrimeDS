# Vulkan Best Practices audit — 2026-08-18

## Scope

This audit records the implementation work requested by
`.codex/melonPrimeDS_Vulkan_Best_Practices_監査_改善指示書_develop_hud_2026-08-18.md`.
The instruction file is intentionally left unmodified and untracked. The
baseline instruction HEAD was `c06990bfffb79829e9dce1b077fc6df43d050a19`.

The phase commits are intentionally separate:

| Phase | Commit | Result |
|---|---|---|
| BP1 — validation profiles | `73e2a11577b2ea7ed83d89e4181d37bb8de71a00` | Core, SyncVal, Best Practices, and GPU-AV profile generation/audit wiring |
| BP2 — memory admission | `b1eebd67bcba8a081ada4988051f71e3f69244e5` | Vulkan/DX12 live-budget-aware admission and allocation-limit checks |
| BP3 — device fault diagnostics | `91c601e6d442b7563dcf4d10808e36addafe7e33` | Optional `VK_EXT_device_fault` capability and one-shot loss diagnostics |
| BP4 — allocation telemetry | `af111a5a350bd5ea884cdf9517b178fee8b70683` | Allocation count/size/heap telemetry without a per-frame telemetry path |
| BP5 — split queue experiment | `4d88431fa0fe3e5cc1aedb1e8ae389aeb229a503` | SyncVal-gated, opt-in EXCLUSIVE ownership-transfer experiment |
| BP6 — optional modernization | `09e81728d5eec64c50e5df63965eed913319945a` | Developer-only Vulkan command labels; release/default command stream unchanged |

## Phase evidence

### BP1 — validation profiles

- Added profile generation and verification under `tools/testing` and
  `tools/ci/audits`.
- CI now iterates the four requested validation profiles instead of treating
  Core Validation as the only signal.
- Local profile generation and static audit passed. Full CI execution and a
  real 3D workload through every profile were not run in this environment.

### BP2 — memory admission

- Vulkan admission uses optional `VK_EXT_memory_budget` data when available,
  retains the 75% safety fallback otherwise, and checks heap budget, largest
  allocation size, and allocation count without silently clamping the scale.
- DX12 admission queries live `DXGI_QUERY_VIDEO_MEMORY_INFO` data and shares
  the policy shape while keeping API-specific acquisition separate.
- Pure admission tests passed:
  `melonprime_vulkan_memory_admission_check` and
  `melonprime_dx12_memory_admission_check`.
- The configured Windows Vulkan `core` build passed. Physical live-budget
  pressure behavior was not run.

### BP3 — device fault diagnostics

- `VK_EXT_device_fault` is optional and best-effort; unsupported devices retain
  the existing device-loss path.
- Diagnostics are one-shot at device-loss boundaries and do not attempt to
  persist vendor-specific binary dumps.
- The configured Windows `melonDS` build passed. A physical device-loss event
  with fault data was not induced, so the capture contents remain NOT RUN.

### BP4 — allocation telemetry

- Telemetry tracks current/peak allocation count, total alloc/free count,
  heap bytes, largest allocation, and bounded size buckets.
- Counters update only on allocation admission/release under the existing
  memory lock; there is no per-frame allocation scan, logging loop, or new
  steady-state lock.
- `melonprime_vulkan_memory_telemetry_check` passed and the configured
  Windows `core` build passed.

### BP5 — split graphics/present queue experiment

- The default path remains `VK_SHARING_MODE_CONCURRENT`.
- The EXCLUSIVE path requires both
  `MELONPRIME_VULKAN_SPLIT_QUEUE_EXCLUSIVE=1` and
  `MELONPRIME_VULKAN_SPLIT_QUEUE_SYNCVAL_CLEAN=1`.
- The opt-in path records graphics-family release and present-family acquire
  ownership barriers with a dedicated present-family submission. It does not
  change the default frame-slot or semaphore model.
- `melonprime_vulkan_queue_sharing_experiment_check` passed and the configured
  Windows `melonDS` build passed.
- Physical split-family SyncVal validation, A/B timing, and a reproducible
  performance win were NOT RUN. The experiment must remain opt-in until those
  gates are satisfied.

### BP6 — optional modernization

- Added `src/VulkanDebugLabels.h` with no-op helpers for unavailable extension
  entry points and for non-developer builds.
- Labels cover Vulkan raster frame, span interpolation, binning, rasterisation,
  depth blending, final pass, structured compositor, HUD/OSD uploads, and
  presenter composition.
- A developer-enabled configured Windows `melonDS` build linked successfully.
- In a separate developer-disabled tree, `core` and all `melonDS` objects,
  including the presenter, compiled successfully. The final link was BUILD
  ONLY and stopped at unrelated `DwmFlush`/`DwmSetWindowAttribute` imports in
  that dependency configuration. No shipping runtime behavior was changed.
- RenderDoc/Nsight capture inspection was NOT RUN.

## Acceptance matrix

| Area | Status | Evidence / remaining gate |
|---|---|---|
| Validation profile separation | PASS | Generator, verifier, and CI profile wiring are present |
| Core/SyncVal/Best Practices/GPU-AV zero-runtime-error claim | OPEN | Real 3D workload and physical validation runs remain required |
| Vulkan memory budget fallback | PASS | Optional budget plus 75% fallback policy and pure tests |
| Vulkan allocation limits | PASS | `maxMemoryAllocationSize` and `maxMemoryAllocationCount` admission checks |
| DX12 live budget | PASS | Query-based policy and pure tests |
| Device fault capability | BUILD ONLY | Optional path compiles; physical fault capture NOT RUN |
| Allocation telemetry | PASS | Pure telemetry test and memory-path integration |
| Default presentation path | PASS | Existing concurrent sharing and frame/semaphore model preserved |
| EXCLUSIVE split queue | BUILD ONLY | Model test/build pass; physical SyncVal/A-B NOT RUN |
| Developer command labels | BUILD ONLY | Developer ON link pass; capture inspection NOT RUN |
| Silent scale clamp / extra frames-in-flight / steady-state `WaitIdle` | PASS | No such changes were introduced by these phases |
| P1-02 real 3D/compositor coverage | OPEN | No-ROM smoke still does not exercise the full compositor workload |
| Physical platform matrix | NOT RUN | No claims made for NVIDIA/AMD/Intel/Linux/macOS/BSD runtime coverage |

## Guardrails retained

- Optional capabilities remain optional and are not promoted to mandatory
  device requirements.
- Failure paths report the reason or preserve the existing safe behavior;
  resource scale is not silently reduced.
- The normal split-family path remains concurrent until physical validation and
  benchmark evidence justify an experiment default change.
- No renderer frames-in-flight increase, per-frame allocation scan, or
  steady-state device-wide idle was added.

## Reference material

- [Vulkan synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
- [`vkQueuePresentKHR`](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueuePresentKHR.html)
- [`VkSharingMode`](https://docs.vulkan.org/refpages/latest/refpages/source/VkSharingMode.html)
- [`VK_EXT_memory_budget`](https://docs.vulkan.org/refpages/latest/refpages/source/VK_EXT_memory_budget.html)
- [`VkPhysicalDeviceMemoryBudgetPropertiesEXT`](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceMemoryBudgetPropertiesEXT.html)
