# DX12 NVIDIA Reflex — latency read-back and engagement verification

- Date: 2026-08-24
- Branch: `develop_hud`
- Target: `src/DX12NvidiaReflex.{h,cpp}`, `src/GPU_DX12.{h,cpp}`, `src/DX12Perf.h`
- Companion: `docs/audit/vulkan_reflex_on_preissue_ab_2026-08-24.md`

## Why this existed

`VulkanNvidiaReflex` can prove its integration works: `QueryTimings()` wraps
`vkGetLatencyTimingsNV`, and a non-zero `presentID` matching the ids the markers
carried is end-to-end evidence that the markers, the tagged submission and the
tagged present were correlated by the driver into one frame.

`DX12NvidiaReflex` had no counterpart. It called `NvAPI_D3D_Sleep` and emitted
markers, all of which return void or a status that says nothing about
correlation. A DX12 Reflex integration could therefore be completely inert and
produce exactly the logs a working one produces.

That mattered because DX12 Reflex On measured 533-534 FPS — identical to Off. A
pacing feature costing exactly nothing is a result that needs evidence behind it
before it can be used, as the Vulkan instruction document used it, as the
reference for what Reflex ought to cost.

## What was added

- **`NvAPI_D3D_GetLatency` ABI**, pinned alongside the existing NVAPI
  declarations: `NvLatencyFrameReport` (240 bytes) and `NvLatencyResultParams`
  (15400 bytes), both `static_assert`ed, with the sizeof-derived version word.
- **Runtime resolution at interface id `0x1A587F9C`**, deliberately *not* in the
  required-function list: a driver without latency reporting loses the
  diagnostic, not Reflex.
- **`DX12NvidiaReflex::QueryTimings()`**, mirroring the Vulkan signature and its
  newest-last contract, plus `DX12NvidiaReflexFrameReport` as a backend-neutral
  POD so the header does not leak NVAPI types.
- **`DX12NvidiaReflexLatencyReportStatus`**, because "no reports" has three
  incompatible meanings — see the false start below.
- **`DX12Renderer::ReportReflexLatencyTimings()`**, developer builds only, every
  600 frames, matching `VulkanPresenter::ReportLatencyTimings()`.
- **`DX12Perf::CpuMetric::ReflexSleep`** (`reflex_sleep_us`), the direct
  counterpart of Vulkan's `reflex_latency_sleep_us`, so the two backends'
  sleep costs are measured the same way.

## A false start worth recording

The first implementation resolved `NvAPI_D3D_GetLatency` at id `0x1452F25A`,
taken from memory. It resolved to null, `QueryTimings()` returned 0, and the
diagnostic printed "driver has no completed frame reports yet" in all three
modes including `mode=1 active=1`.

Read naively that is exactly the signature of an inert Reflex integration, and it
appeared to confirm the hypothesis. It was an artifact of the wrong id.

Two things caught it. First, the message conflated *unresolved entry point*,
*rejected query* and *successful-but-empty ring*; splitting those into
`DX12NvidiaReflexLatencyReportStatus` immediately reported `reason=unsupported`,
which is a statement about our pointer, not about the driver. Second, the id was
then taken from NVIDIA's published `nvapi_interface.h` rather than memory, and
validated by checking that the same file reproduces the four ids already working
in this codebase (`SetSleepMode 0xac1ca9e0`, `Sleep 0x852cd1d2`,
`GetSleepStatus 0xaef96ca1`, `SetLatencyMarker 0xd9984c05`). All four matched,
so its `NvAPI_D3D_GetLatency = 0x1a587f9c` is trustworthy.

The lesson generalises: a diagnostic that cannot distinguish "the thing is
broken" from "my probe is broken" will eventually confirm whichever hypothesis
you brought to it.

## Result: DX12 Reflex is engaged and correct

Developer build, F7 savestate, Scale 2, VSync off, frame limit off. Newest
report of each run:

```
mode=1 active=1 reports=8 frameID=11399
  sim=…251099..…252043  renderSubmit=…252044..…252649
  present=…252769..…252876  inputSample=…251097
  gpuActiveRenderUs=33  gpuFrameUs=1804
```

- `frameID` tracks the logical frame ids the markers carried (10799, 11399,
  11999 — the 600-frame report cadence), so the driver **is** correlating our
  markers into frames.
- Stamps are ordered `inputSample < simStart < simEnd < renderSubmitStart <
  renderSubmitEnd < presentStart < presentEnd`.
- Reports are produced in all three modes, including Off, as NVIDIA's QA
  contract requires.

**`gpuActiveRenderTimeUs` is 33-35 us against a `gpuFrameTimeUs` of ~1804 us.**
The GPU is busy roughly 2% of the frame. No render queue accumulates, so the
sleep Reflex should insert is correctly zero — which is why Reflex On costs
nothing on DX12. The feature is working; it simply has nothing to do.

No defect was found in `DX12NvidiaReflex.cpp`. The verification, the status
disambiguation and the sleep metric are kept as permanent instrumentation.

## Cross-backend sleep cost

Same workload, same GPU, same ~34 us of GPU work per frame:

| Backend / mode | sleep call, p50 | p95 | n |
|---|---:|---:|---:|
| DX12 Reflex On (`NvAPI_D3D_Sleep`) | **1.2 us** | 66 us | 503 |
| DX12 Reflex On+Boost | **1.2 us** | 46 us | 500 |
| DX12 Off | 1.8 us | 59 us | 399 |
| Vulkan Reflex On (`vkLatencySleepNV`) | **1247 us** | 1769 us | 344 |
| Vulkan Reflex On+Boost | 1258 us | 1882 us | 335 |
| Vulkan Off | **1798 us** | 1946 us | 534 |

Roughly a thousandfold difference for the same job. Vulkan blocks ~1798 us even
with `lowLatencyMode = VK_FALSE`, where no pacing is requested — so the block
tracks the swapchain's frame cadence rather than a latency target, and on this
driver `VK_NV_low_latency2`'s sleep is where Vulkan's WSI backpressure is
absorbed. That matches the instruction document's §3.2 finding that deleting the
sleep merely relocated ~1 ms into `vkAcquireNextImageKHR`.

Consequence for the companion document: DX12 **is** a valid reference, the
Vulkan-specific cost is real, and the earlier conclusion that Vulkan's 1.25 ms
was legitimate pacing has been withdrawn there.

## Caveat: the latency figures here are focus-uncontrolled

Every latency comparison in this document was measured with
`-Action savestate-load`, which does **not** call the harness's
`Focus-RendererWindow`. The harness itself warns that background-window
scheduling changes both Raw Input delivery and observed FPS, and a later
focus-controlled run collapsed a bimodality that had been misread as a CPU/GPU
power state (sleep alternating between ~13 us and ~1800 us across runs of the
same condition).

The FPS figures reproduce under focus control (Vulkan Off 534, On 344-348) and
stand. **The `inputSample -> presentEnd` comparisons do not**: they must be
re-measured with `-Action steady-state` before being cited. See
`docs/audit/reflex_investigation_handoff_2026-08-24.md` section 6.

## Caveat

The `r4-dx12-off` run reported a median 315 FPS against 497-499 for the On and
On+Boost runs in the same batch, where earlier Shipping runs put all three at
532-534. That run is an outlier of unknown cause (it was last in a batch
following several long builds; thermal or background load are both plausible)
and was not reproduced. It does not bear on the sleep-duration comparison this
run was for — `reflex_sleep_us` was 1.8 us p50 there, in line with the others —
but the FPS figure from it should not be quoted.

## Reproduction

```
$env:MELONPRIME_PERF = '1'
tools\testing\renderer-physical-ab.ps1 -Renderer DX12 `
  -Rom "<rom>.nds" -Savestate "<rom>.ml7" -SavestateSlot 7 `
  -BuildDir build\release-mingw-x86_64 -RunId dx12-on -ExpectedSourceHead <sha> `
  -OutputDir <out> -Scale 2 -NoVSync -NoFrameLimit -LowLatency Reflex `
  -Action savestate-load -Hud On -AllowUnverifiedBinary `
  -CaptureFrames 10 -CaptureIntervalMs 500 -SampleWindowTitlesOnly
```

Latency reports appear in `<RunId>.out.log` as `NVIDIA Reflex timings:`;
`reflex_sleep_us` appears in `<RunId>.err.log` as a `[DX12Perf] cpu` row.
Artifacts: `build/verification/dx12-reflex-latency-20260824/`.
