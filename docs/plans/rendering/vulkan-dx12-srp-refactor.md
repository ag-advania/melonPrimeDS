# Vulkan / DirectX 12 SRP refactor

Source audit: `MelonPrimeDS Vulkan / DirectX 12 SRP・ベストプラクティス監査` (2026-08-25).
The audit scored the Vulkan backend at B (strong low-level foundation, monolithic
`VulkanRenderer3D`) and DX12 at C+ (same monolith, plus low-latency control and
device/compiler/factory responsibilities collapsed into two classes).

This document tracks execution of that audit's staged plan. It records what has
landed, what has been verified, and — for the part that has not landed — what
the remaining work actually costs, so the next session does not have to
re-derive it.

## Status

| Audit ID | Component | Priority | Status |
|---|---|---|---|
| DX-SRP-002 | `DX12Renderer` vendor low-latency ownership | HIGH | **Done** |
| VK-SRP-004 | `VulkanRenderer3D` monolith | HIGH | **Done** — pipeline cache, output ring, capture provenance, upload plan, capture bridge and the GPU2D compositor extracted |
| DX-SRP-003 | `DX12Renderer3D` monolith | HIGH | **Done** — pipeline repository, output ring, capture provenance, upload plan, capture bridge and the GPU2D compositor extracted |
| DX-SRP-001 | `DX12Context` split | MEDIUM-HIGH | **Done** |
| VK-SRP-003 | `VulkanRenderer::VBlank()` publication policy | MEDIUM | **Done** |
| BOTH-SRP-002 | `GPU_Vulkan` / `GPU_DX12` policy duplication | MEDIUM | **Done** |
| BOTH-SRP-003 | DX12 / backend-neutral test seams | — | **Partly done** |
| Phase 3 #8 | Pipeline repository, both backends | — | **Done** |
| VK-SRP-005 | `VulkanPresenter` latency controller | MEDIUM-LOW | **Done** |
| DX-SRP-004 | `DX12SurfacePresenter` swapchain/layer split | LOW | Not needed yet |
| VK-SRP-002 | `VulkanDevice` budget/diagnostics | LOW | Not started |

## Phase 1 — landed

### 1. `GPU2DFramePolicy` (BOTH-SRP-002, VK-SRP-003)

`src/GPU2DFramePolicy.h`. `VulkanRenderer::VBlank()` and
`DX12Renderer::VBlank()` each carried the same nine-input decision tree
deciding whether the native compositor's frame is published, whether the last
frame is retained, whether the exact gate refuses, and which fallback counters
and one-shot log lines fire. That is a policy, not a graphics API, and it is now
a single pure function that both call.

The header holds no `VkImage`, no `ID3D12Resource`, no command list and no
descriptor, and it must stay that way.

What stays backend-specific and deliberately did not move: the log tags, the
`VulkanPerf` / `DX12Perf` counters, the compose calls themselves, and the
Vulkan-only `!GPU.GPU3D.AbortFrame` guard on the raster-differential compare.

`VulkanRenderer::VBlank()`'s observer notification and `DX12Renderer::VBlank()`'s
render-submit marker are now scope guards rather than a call before every
`return`, so a future change to the publication policy cannot silently drop one.

### 2. `DX12LowLatencyController` (DX-SRP-002)

`src/DX12LowLatencyController.{h,cpp}`. Sole owner of `DX12NvidiaReflex`,
`DX12AmdAntiLag2`, `DX12IntelXeLL`, the pacing policy, the XeLL frame-cap
request and the developer Reflex timing report.

Before, `DX12Renderer` owned all three and re-exported eighteen public methods
so that `Screen.cpp` could fire Present markers around
`DX12SurfacePresenter::Present()` from the outside. Present responsibility was
spread across three layers.

Now:

- `DX12SurfacePresenter::Present()` fires `BeginPresent()`/`EndPresent()` itself,
  around the real `IDXGISwapChain::Present`, through a scope guard.
- `DX12SurfacePresenter::BeginFrame()` decides the DXGI frame-latency wait
  itself by asking the controller, so the `waitForPresentSlot` parameter is
  gone.
- `Screen.cpp` contains no Reflex/XeLL/Anti-Lag reference at all.
- `EmuThread.cpp` speaks frame phases (`BeginFrame`, `MarkInputSample`,
  `MarkSimulationStart`, `EndRenderPhase`, `FinishFrame`) to the controller
  rather than vendor-named methods to the renderer.
- `DX12Renderer` reports render-submit boundaries — because it is what submits —
  and forwards the configured settings. It owns no vendor object.

Ownership is a process-wide service, matching `DX12Context::Get()`: the three
vendor sessions are D3D12-device scoped, and their markers bracket events in
three different subsystems. The renderer drives initialize/shutdown and installs
a queue-idle hook, so the controller never learns what a `Renderer3D` is.

One deliberate, non-observable ordering change: at shutdown the XeLL
end-of-frame marker is now sent after the queue-idle wait instead of before it.

### 3. `DX12Context` split (DX-SRP-001)

`DX12Context.{h,cpp}` was a device owner *and* a runtime loader, a shader
compiler, a resource factory, a memory-budget gateway, and the home of three
unrelated classes. It is now:

| New module | Responsibility |
|---|---|
| `DX12RuntimeLoader.cpp` | resolves d3d12/dxgi/d3dcompiler; contract stays in `DX12Common.h` |
| `DX12ShaderCompiler.{h,cpp}` | `D3DCompile`, compile flags, macro assembly, diagnostics |
| `DX12ResourceFactory.{h,cpp}` | `CreateBuffer` / `CreateTexture2D` over a borrowed device |
| `DX12DescriptorRing.{h,cpp}` | moved out verbatim |
| `DX12CommandContext.{h,cpp}` | moved out verbatim |
| `DX12UploadRing.{h,cpp}` | moved out verbatim; `Init()` now takes `ID3D12Device*` |

`DX12Context` keeps factory/adapter/device/queue/profile plus the memory
admission gateway, and forwards `CompileShader` / `CreateBuffer` /
`CreateTexture2D` to the new owners so the ~38 existing call sites did not have
to move. The forwarding is the facade the audit endorses: changing compile flags
or resource descriptions now edits the owning module, not the device owner.

Dependency direction is now one-way — `DX12UploadRing` used to reach back to
`DX12Context` purely to call `CreateBuffer`, and no low-level module includes
`DX12Context.h` any more.

`DX12MemoryBudget` was **not** split out. `DX12MemoryAdmission` already holds
the pure calculation, and the live budget refresh needs the adapter the device
owner holds — the same shape `VulkanDevice` has, which the audit rated PARTIAL
PASS rather than FAIL.

### 4. Test seam (BOTH-SRP-003)

`tools/testing/gpu2d-frame-policy-tests.cpp`, wired as
`melonprime_gpu2d_frame_policy_check` and run on every build, on every platform,
not gated on developer features.

It carries a second implementation of the decision tree, transcribed from the
pre-extraction inline branches, and runs both over the complete input
cross-product — every boolean combination against every compose result, with the
impossible `NativeComposed` combinations excluded. Every field of the decision
must match. **1600 cases, 0 divergences.**

That is the evidence that the extraction is behaviour-preserving, which
compilation alone would not be.

## Phase 2/3 — pipeline management extracted from both Renderer3D classes

The audit lists pipeline management as responsibility C of `DX12Renderer3D`
(§6 DX-SRP-003) and pipeline cache as part of E on the Vulkan side, and
sequences a `PipelineRepository` split as Phase 3 item 8 because
`PipelineLibrary` / `CachedPsoBlobs` / `RootSignatureHash` / `ShaderBlobHash` /
save-load "は独立変更理由が強い" — they change for reasons of their own.

That turned out to be the one part of the monoliths that separates cleanly
today, so it was done first rather than last.

### `DX12PipelineRepository`

`src/DX12PipelineRepository.{h,cpp}`. Owns the root signature and its hash, the
indirect dispatch command signature, the `ID3D12PipelineLibrary`, the per-PSO
cached blob fallback, and both on-disk cache files with their validation
headers. `DX12Renderer3D` lost nine members and six member functions and now
holds one `PipelineRepo`.

Two things worth knowing about the shape:

- **The repository does not know which shaders exist.** The generated blob
  table is a ~190k-line `.inc` owned by `GPU3D_DX12.cpp`, and unity-include
  ownership requires it to stay in exactly one translation unit, so bytecode
  arrives through a lookup callback. That constraint produced the better
  boundary anyway: pipeline *caching* and the *shader inventory* are not the
  same responsibility.
- **The repository does not do telemetry.** `BuildComputePipeline()` returns
  `LibraryHit` / `CachedBlobHit` / `Compiled` / `Failed`, and the renderer
  turns that into its startup profile counters. Renderer startup profiling is
  not a pipeline-cache concern.

The root-signature binding contract (`DX12RootSignatureLayout`) moved into the
repository header, so the descriptor rings and the signature they are bound
against are now sized from one set of constants — the same single-source-of-
truth property the audit praised in `VulkanDescriptors` (§4.3). A ring sized
from a different constant than the signature declares is silent GPU-side
corruption, and that pairing was previously implicit.

The on-disk cache formats are byte-identical. The fixed-size PSO size table
became a `std::vector<u32>` of the same entry count, so an existing warm cache
still validates.

### `VulkanPipelineCache`

`src/VulkanPipelineCache.{h,cpp}`. Owns the `VkPipelineCache`, its framing,
device-identity plus `pipelineCacheUUID` validation, and driver-rejection
handling.

Smaller than the DX12 counterpart on purpose. Pipeline *creation* stays in
`VulkanRenderer3D`, because Vulkan folds the resolution-dependent
specialization constants (`ScreenWidth` / `ScreenHeight` / `MaxWorkTiles`) in
at `vkCreateComputePipelines` time, so that call belongs with the geometry
state. DX12 has no equivalent coupling, which is why its split is larger. Both
backends now have the same *responsibility* boundary with different shapes —
the audit's §9 target, and explicitly not a 1:1 copy (§11.4).

## Phase 2 — the presentation slot ring is out of both monoliths

`src/RendererOutputRing.{h,cpp}`, plus
`tools/testing/renderer-output-ring-tests.cpp`.

This is the `OutputPublisher` step, taken along the seam that actually exists
in this tree rather than the one the audit assumed.

### What moved

Both backends published frames through the same protocol, written out twice:

- a published-slot index and a monotonic serial sequence,
- a per-slot presenter refcount and the lease release callback,
- one mutex covering all of it,
- a scan that skips the published slot, any slot a presenter still holds, and
  any slot whose GPU work has not retired,
- and, on DX12, a round-robin cursor shared between the two production paths.

Only one part of that is backend-specific: "has the GPU finished with this
slot" is `LastSubmittedFrame <= completedFrame` on Vulkan and
`Commands.IsIdle()` plus a work-slot cross-check on DX12. That is now the one
thing the ring asks the caller, through a plain function pointer evaluated
inside the same critical section as before — so the lock scope is unchanged.

`RendererOutputRing` holds no `VkImage`, no `ID3D12Resource`, no command
buffer and no fence. The frame descriptor a slot publishes stays with the
backend that can describe it; the ring owns identity and lease bookkeeping.

### Why this and not the resource reshuffle first

The previous revision of this document concluded that `OutputPublisher` could
not be cut before splitting `OutputState::Slot` apart, because that struct
bundles presentation, composer upload and capture members together. That is
true of the *resources* and still is. It is not true of the *protocol*: the
published-slot index, the serial, the refcounts and the free-slot scan were
never entangled with the buffers — they just happened to live in the same
struct. Taking the protocol out first is a smaller, checkable change, and it
removes the duplicated ring logic from both files without touching a single
GPU resource.

The resource reshuffle is still needed before `Gpu2DComposer` and
`CaptureBridge` can be extracted. It is no longer needed for output
publication.

### Lease ownership detail worth keeping

A `RendererOutputLease` carries one `void*` for its release callback, so the
thing being refcounted must be addressable on its own. The ring therefore owns
an array of `LeaseCounter` objects with stable addresses, and hands out a
`LeaseCounter*` rather than a slot index. The lease still holds the
`shared_ptr<OutputState>` that keeps the ring alive.

### Behaviour preserved

- Serials still start at 1 per output state, still advance by exactly one per
  publication, and `Unpublish()` does not rewind them — the presenter's
  monotonic gate compares serials across renderer transitions.
- The native path still *reserves* a serial before recording the work that
  carries it, and commits only if that work reaches a presentable slot.
- A fully occupied ring still reports backpressure rather than reusing a slot,
  which is what keeps a half-composed frame off the screen.

`renderer-output-ring-tests` pins each of those down, including the cases that
are easy to get wrong under refactoring: a released-but-still-published slot
must not be reused, nested leases need both releases, and an empty ring must
not index out of its array.

## Phase 2 — capture provenance is out of both monoliths

`src/CaptureProvenanceState.{h,cpp}`, plus
`tools/testing/capture-provenance-tests.cpp`.

The audit's ownership rule for capture (§13) already prescribed the split:

```text
semantic owner:  backend-neutral capture provenance
physical owner:  VulkanCaptureBridge / DX12CaptureBridge
```

The semantic half is now that backend-neutral owner. Both renderers carried
the same seven fields — epoch, last semantic frame, capture generation,
semantic epoch, submission serial, completion value, plus the high-resolution
sidecar tracker — and the same eleven-clause acceptance predicate, character
for character. The only difference was which `CaptureOwner` enumerator each
compared against, which is now an argument.

The physical half stays where it belongs: the copy, the fence wait and the
mapping in `ReadNativeCapture()` are still the backend's, because only the
backend can issue them.

### Why this matters more than its size suggests

The predicate answers "may this recorded capture block still be served from
the native mirror?". A wrong *no* costs a frame of fidelity. A wrong *yes*
hands the emulated program VRAM contents from the wrong frame, or from a
renderer that no longer exists, and shows up much later as a corrupted in-game
camera feed rather than as a crash. It was duplicated, so any drift between
the two copies would have been a backend-specific capture bug of exactly the
kind that is hardest to attribute.

Every comparison in it is deliberately one-sided — older is acceptable, newer
is not, because a block from a future frame means the provenance record and
the renderer disagree about time. `capture-provenance-tests` pins each
asymmetry down individually, along with owner isolation, the two separate
epoch tests, and the rule that invalidating the mirror must not rewind the
submission serial.

### One new operation

`InvalidateMirror()`. The Vulkan scale-resource path cleared
`NativeCaptureStateInitialized` on its own, without touching the epoch or the
serial — "the resources behind the mirror were replaced, but the renderer's
identity did not change". That was a bare field assignment before; it is now a
named operation, which is what made it visible at all.

## Phase 2 — the structured upload plan is out of both monoliths

`src/StructuredUploadPlan.h`, plus
`tools/testing/structured-upload-plan-tests.cpp`.

The compositor's structured input is one buffer holding fourteen 2D planes,
two line-metadata blocks and the capture command stream. Each of those
seventeen units carries its own content generation, so a frame that changed one
plane should re-upload one plane — and adjacent dirty units coalesce into a
single copy.

Both backends computed that identically, down to the coalescing loop; only
`VkDeviceSize` versus `u64` differed. It is not a graphics question, and a unit
wrongly considered clean leaves last frame's 2D content on screen for that
plane, silently.

The plan now also owns the unit layout (offsets and sizes), because the packing
path writes at those offsets while the copy path uses the ranges — computing
them in two places is how the two can disagree about where a unit lives.

One asymmetry worth naming, because it is the easiest thing to lose in a
rewrite: the capture command stream is re-uploaded when a capture *source*
plane (3, 7 or 13) changes, even though its own generation did not. The tests
check that in both directions.

**37450 cases, 0 divergences** against a transcription of the pre-extraction
computation, plus property tests for full upload, isolated units, adjacent
coalescing, non-adjacent separation, and layout contiguity.

## Phase 2 — CaptureBridge, the physical half

`src/DX12CaptureBridge.{h,cpp}` and `src/VulkanCaptureBridge.{h,cpp}`.

With the readback-context question resolved above, this became the mechanical
step it was blocked from being. Each bridge owns:

- the high-resolution capture sidecar the compositor writes,
- the readback buffer a demanded block lands in,
- and the copy/wait/map that fetches blocks out of the native mirror.

It does **not** own the demand-driven readback context. That stays with the
facade and is passed in, which is what keeps the rasterizer's `GetLine()` path
from having to reach sideways into a capture component. The bridge's contract
says so explicitly: the caller must already have retired any other submission
on the context, because the bridge cannot know what else is queued there.

Nor does it judge provenance. `CaptureProvenanceState` decides *whether* a
recorded block may still be served; the bridge decides *how* it is fetched. By
the time a read reaches `ReadBlocks()` the answer is already yes. That is the
audit's semantic-owner / physical-owner split (§13), now real on both backends.

The compositor still writes the sidecar — it borrows the handle through
`GetSidecarBuffer()` / `GetSidecarHandle()` rather than owning it, so the
descriptor tables are unchanged and no barrier moved.

Both `ReadNativeCapture()` implementations are now a guard, a shared-context
retire, and one delegated call.

## Phase 2 — Gpu2DComposer, the last of the monolith

`src/DX12Gpu2DComposer.{h,cpp}` and `src/VulkanGpu2DComposer.{h,cpp}`.

The audit's item 6 names four things to cut out: `ComposeStructuredOutput`,
`ComposeNativeGPU2D`, the native GPU2D pipelines and resources, and the
compositor resources. All four moved, in three steps per backend.

### What each module owns

| | DX12 | Vulkan |
|---|---|---|
| `<Backend>Gpu2DComposer` | four compute pipelines, three descriptor rings, cached table bases, publication state, both compose passes | its own `Vk::FrameRing`, publication state, both compose passes |
| `<Backend>Gpu2DOutput` | presentation slots, work slots, publication ring | same |

The split into two classes per backend is a lifetime split, not a taxonomy
one: the output set is recreated on every resolution change and is held by
`shared_ptr` so a presenter lease survives that; everything else is created
once with the renderer.

DX12 has a pipeline owner and Vulkan does not, for a reason the headers state.
On DX12 the four compositor PSOs were separate named members and moved
cleanly. On Vulkan they are entries in one shader-step-indexed array shared
with the rasterizer, and splitting that array would change how pipelines are
built and resolution-specialized — a mechanism change, which §11.3 rules out
folding into a responsibility move. The renderer resolves the three handles a
compose needs and passes them in.

### The borrow contract, written down

`DX12Gpu2DComposeContext` / `VulkanGpu2DComposeContext` is the point of the
exercise as much as the code motion is. Every handle the compose passes read
from outside the compositor is now named in one struct, rebuilt per call so it
cannot hold a stale handle across a resolution change:

- the device context, root signature / pipeline layout, descriptor pool
- capture's bridge, provenance state and sidecar pipeline
- the frame geometry and the shader-ready / renderer-failed / abort flags
- **`FinalFB`** — the 3D rasterizer's finished image, which the compositor
  samples and never writes. That is the read-only contract the audit asked to
  be made explicit, and it is now a comment on a field rather than a
  convention.

Three (DX12) or two (Vulkan) plain function pointers cover what has to run
back on the renderer: latching a runtime failure, dropping the texture-SRV
binding cache when a compose list resets the shared descriptor ring, and — on
DX12 — building the developer-only diagnostic UAV block, which describes
rasterizer buffers. Function pointers rather than `std::function` because this
runs once per DS frame on the emulation thread and must not allocate.

### Descriptor strategy deliberately untouched

Neither backend's table shape moved. DX12 still binds one fourteen-entry UAV
table per dispatch, assembled from borrowed handles in the same order, and
three of those slots still mean different things depending on which shader
runs. Giving the compositor its own table would mean recompiling shaders
against a different register layout. Same reasoning as the Vulkan pipeline
array: §11.3 says structure first, same behaviour.

### Contract that moved with them

Two pieces of shared contract were living in the wrong place and blocked the
move until they were relocated:

- `DispatchUniform` mirrors an HLSL cbuffer and is set by every DX12 compute
  dispatch, but was a private nested type of `DX12Renderer3D`. It is now
  `DX12DispatchUniform` in `DX12PipelineRepository.h`, beside
  `DX12RootSignatureLayout`, along with the four stateless recording helpers.
- `BufferBarrier` is now `Vk::BufferBarrier` in `VulkanSync.h`, and
  `DivRoundUp` / `AlignUp` moved to the two `*Common.h` headers, since two
  translation units per backend now record dispatches.

### Size

| File | Before | After |
|---|---|---|
| `GPU3D_DX12.cpp` | ~4,800 | 3,262 |
| `GPU3D_Vulkan.cpp` | ~4,900 | 3,066 |

Each facade keeps three thin wrappers, because `Renderer3D` is the interface
the emulator calls in through.

## VK-SRP-005 — the Vulkan vendor latency state machine

`src/frontend/qt_sdl/MelonPrimeVulkanLatencyController.{h,cpp}`.

The audit rated the Vulkan placement of Reflex / Anti-Lag *better* than the
DX12 one it replaced: the markers already sit around the real
`vkQueueSubmit` / `vkQueuePresentKHR`, which is where they have to be. Its
complaint was size — the presenter had absorbed the vendor state machine on
top of everything else.

`VulkanLatencyController` now owns both vendor sessions, the logical frame
index they share, and the log-on-change latch. The presenter dropped four
members and two logging functions, and `MelonPrimeVulkanPresenter.cpp` went
from 3,535 to 3,057 lines.

Two things stayed behind, and the header says why:

- **`VulkanPresentPacer`** participates in swapchain *construction* — surface
  capability queries, `VkSwapchainCreateInfoKHR` flags, NV low-latency
  present-mode selection. It belongs with the object that builds swapchains.
- **`VulkanPresentLatencyCapture`** is an A/B measurement instrument spanning
  both the vendor markers and the pacer, so it cannot sit on either side
  alone.

`BeginLowLatencyFrame()` also stayed on the presenter. It is orchestration,
not vendor state: it consults the pacer, may rebuild the swapchain, may fail
the renderer, and waits on the presenter's own frame ring. What it now calls
into the controller for is exactly the vendor half —
`ApplyPreferences`, `BeginReflexSleep`, `BeginAntiLagInput`.

One sequence became a named operation rather than five loose calls:
`PrepareForTeardown()`. Anti-Lag applies state on its next `BeginFrame`, so
handing the driver an "off" state requires an End/Begin pair afterwards —
which reads as redundant until you know that. Doing it after the device drain
instead left NVIDIA's driver holding active low-latency state when switching
away from Vulkan mid-game, so the ordering is load-bearing.

## Verification performed

- `tools/build/windows/build-mingw.bat --jobs 1`: **succeeded**, developer
  Release profile, MinGW x86_64. All 51 build steps including every in-build
  model test.
- `gpu2d-frame-policy-tests`: PASS (1600 cases, 0 divergences).
- `renderer-output-ring-tests`: PASS.
- `capture-provenance-tests`: PASS.
- `structured-upload-plan-tests`: PASS (37450 cases, 0 divergences).
- Standing audits, all PASS: `audit-config-defaults`, `audit-hud-key-parity
  -Strict`, `check-inc-ownership` (94 files; the generated DX12 blob table
  still has exactly one owner), `audit-metroid-literal-budget -Budget 1`,
  `audit-platform-scatter-budget -Budget 22`, `audit-color-dialog-prefs`,
  `audit-melonprime-srp-performance`, `audit-melonprime-thread-boundary
  -Strict`, `audit-melonprime-instance-state -Strict` (22/22 baseline held).
- `check-dx12-shaders.py`: PASS, all 117 DX12 shader variants across 3 scales.
- `git diff --check`: clean.

Statically confirmed against the audit's Definition of Done:

- `DX12Renderer` owns no vendor low-latency object.
- `Screen.cpp` mediates no vendor Present marker.
- The `GPU_Vulkan` / `GPU_DX12` frame-policy duplication is gone.
- The backend-neutral module holds no native Vulkan/DX12 handle.
- No cycle in the DX12 ownership direction.

Build-gate structure, checked statically because the gate builds themselves
could not run here (see below):

- Every new DX12 module carries the full
  `MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12` guard; every new Vulkan
  module carries its `MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN` guard.
- Every new DX12 source is added to CMake inside the `MELONPRIME_DX12_ACTIVE`
  block, and `VulkanPipelineCache.cpp` inside `MELONPRIME_VULKAN_ACTIVE`, so a
  disabled backend still contributes no translation unit.
- The four backend-neutral modules carry no backend guard and include no
  backend header, which is what lets them compile in every configuration.

### Runtime, on hardware

`tools/testing/run-backend-runtime-smoke.py`. Drives the real binary against
Metroid Prime Hunters (USA Rev 1) and a savestate, then reads the backend's own
log markers. NVIDIA GeForce RTX 5070 Ti, Windows, developer build.

| Run | Vulkan | DX12 |
|---|---|---|
| 1x / 2x / 4x / 8x / 16x | PASS | PASS |
| VSync ON | PASS | PASS |
| 180 injected SemanticOnly frames | PASS | PASS |
| GPU2D exact-validation gate ON | PASS | PASS |
| Reflex On / On+Boost | PASS | PASS |
| AMD Anti-Lag 2 requested (no AMD GPU) | PASS | PASS |
| Intel XeLL requested, IntelRecommended policy (no Intel GPU) | — | PASS |
| Vulkan present pacing policy 4 | PASS | — |
| Injected runtime failure (init stage) | PASS | — |
| Injected capture-readback failure | PASS | — |
| Resize / maximize / minimize / restore | PASS | PASS |
| <-> Software, 6 switches | PASS | PASS |
| DX12 <-> Vulkan, 8 switches | PASS | PASS |
| Vulkan -> DX12 as the first DX12 probe | **FAIL — pre-existing, see below** | |

Re-run after the compositor extraction, on the same hardware:

| Run | Vulkan | DX12 |
|---|---|---|
| 1x / 4x / 8x, window driven, 60 injected SemanticOnly frames | PASS | PASS |
| Vulkan <-> DX12, 4 switch iterations | PASS | PASS |
| 3D raster differential at 1x | 1999 frames, `mismatchedPixels=0` | 1888 frames, `mismatchedPixels=0` |
| GPU2D exact-stage validation | 12,986 records, 0 mismatches | 12,319 records, 0 mismatches |
| Frames published through the native path | 2029 / 2029 | 1918 / 1918 |

Each run checks that the requested renderer initialised and stayed initialised,
that the savestate loaded, that the **native GPU2D path actually composed**
(`gpu2d=<backend> gpu3d=<backend> fallback=0`), and that nothing fell back to
Software or latched a runtime failure.

The SemanticOnly rows are the load-bearing ones.
`MELONPRIME_TEST_GPU2D_PRESENTATION_STALL_FRAMES` forces the compositor to
report `SemanticOnly`, which is exactly
the case `GPU2DFramePolicy::IsRetainedFrameResult()` governs: the last visible
frame must be retained and capture ownership kept, never degraded into a
Software hybrid frame. Both backends held, then resumed native composition —
so the extracted policy is confirmed on hardware, not just against its
transcription.

The switch rows drive the production settings-dialog path
(`MainWindow::onUpdateVideoSettings`), which destroys the screen panel and
replaces the 3D renderer -- so they exercise output-lease invalidation, ring
recreation, pipeline-repository teardown, capture-bridge release and
latency-controller shutdown, which is most of what this refactor moved.

### What the exact-validation run proves

`MELONPRIME_GPU2D_EXACT_VALIDATE=1` makes the compositor validate its native
GPU2D output against the software reference every frame and latch
`FailNativeGPU2DExact()` on any disagreement. The gate engaged — 15,013
`[GPU2DStage]` records on Vulkan, 13,520 on DX12 — and neither backend
rejected a frame.

The same run also checks Display Capture address agreement, which is the path
`CaptureProvenanceState` and the capture bridges own:

```
2264 destinationAddressMismatch=0      (Vulkan)
2264 provenanceAddressMismatch=0
2264 sourceBAddressMismatch=0
2016 destinationAddressMismatch=0      (DX12)
```

Zero mismatches on either backend. That is per-frame correctness evidence for
the 2D and capture paths, produced by the emulator's own comparison rather
than by a screenshot diff. It is not a substitute for per-frame comparison of
the *3D* image, which remains uncovered.

### Presentation, driven through the real window

Configuration cannot tell a swapchain the surface changed; only the window
manager can. The runner therefore drives the app's own window through user32 —
two resizes, maximize, restore, minimize, restore, resize — and the Vulkan
presenter recreates its swapchain at each one:

```
extent=256x384 -> 704x476 -> 404x696 -> 2560x1344 -> 404x696 -> 624x416
```

Both backends survive the sequence, including the minimize/restore pair where
the surface extent goes to zero and back.

### Structured fallback is unreachable in normal operation

Worth stating as a measurement rather than an assumption. With stage
diagnostics on, every published frame reports its source:

```
2366 publicationSource=native      (Vulkan)
2118 publicationSource=native      (DX12)
```

100% native, zero structured, zero retained. The reason is in
`CanComposeNativeGPU2D()`: it requires `ShaderStepIdx >= ShaderStepCount`, and
the emulator does not run frames until pipeline compilation finishes — so the
window in which native is unavailable never contains an emulated frame. The
structured composition path is a genuine fallback for conditions that do not
arise on these two backends in normal play, which is why exercising it needs
an injection hook that does not exist.

### 3D output is bit-identical to the software rasterizer

`tools/testing/run-vulkan-raster-diff.py` runs the software rasterizer as an
oracle beside the native one and hashes both 256x192 outputs every frame.

```
PASS: Vulkan 1x native 3D output exactly matched Software (6389 raster frames)
[RasterDiff] backend=DX12 frame=7122 ... mismatchedPixels=0
             candidateHash=257D7461079C62ED referenceHash=257D7461079C62ED
```

Zero mismatched pixels across 6389 frames on Vulkan and 7122 on DX12, with
`nonZeroPixels=49152` throughout — every pixel of every frame carries content,
so this is a comparison of real images rather than of two blank buffers.

Getting there needed one fix. The comparison call sat inside the
structured-composition branch of `VBlank()` on both backends, and that branch
never runs, for the reason measured above. So the harness had never observed a
frame: it reported "no Vulkan RasterDiff frames were reported" against this
build and, being in the same place at the base commit, against every build
before it. `CompareRasterDifferentialFrame()` is now called from the native
path as well — it compares the two `Renderer3D` outputs, which is independent
of which 2D path composed. The harness's exit-code check also treated a
Windows kill as a crash, which failed every run on this host.

### The failure branches, exercised

Two injection hooks reach branches the publication policy owns, and both
degrade the way they are supposed to:

- `MELONPRIME_TEST_FORCE_VULKAN_RUNTIME_FAILURE=1` faults at initialization,
  so there is no renderer to disable. The frontend swaps to Software before
  the first frame: `Renderer fallback requested=Vulkan actual=Software
  stage=3D-renderer-init`.
- `MELONPRIME_TEST_GPU2D_CAPTURE_READBACK_FAIL=1` faults mid-session. Native
  capture readback is demand-driven, so it needs a renderer transition to be
  reached at all — run together with the switch driver it produces
  `[Vulkan] runtime failure: native Vulkan GPU2D capture readback failed`
  followed by repeated `gpu2d=Software fallback=1 disabled=1`, with the app
  still running.

That second one is `GPU2DFramePolicy::Outcome::ReportRuntimeFailure` firing on
hardware, reached through `CaptureProvenanceState` and the Vulkan capture
bridge. Both degradation shapes are accepted by the runner, because requiring
only one would fail a correct degradation for taking the other route — which
is what the first run did before the expectation was corrected.

### Vendor low-latency, confirmed live

The Reflex runs report `authority=NvidiaReflex ... frameLatencyWaitBypass=1`,
which is the pacing decision travelling from config through
`DX12LowLatencyController` to the presenter's own frame-latency-wait choice —
the path this refactor rewired, previously mediated by `Screen.cpp`.

On Vulkan, `actual=active` in the Reflex line comes from
`VulkanLatencyController::LogVendorState()`. And with XeLL requested on an
NVIDIA adapter, `xellPolicy=IntelRecommended xellRequested=1 xellActual=0`
shows the policy propagating through `ApplySettings()` while the vendor
session correctly declines.

### A pre-existing defect this uncovered

Starting in Vulkan and switching to DX12 **for the first time** fails:

```
MelonPrime DX12 probe: available=0 reason=no Direct3D 12 feature level 11_0 adapter was found
[Vulkan] presenter: vkQueuePresentKHR failed: VK_ERROR_DEVICE_LOST (-4)
Renderer transition begin previous=3 requested=0        <- degraded to Software
```

The DX12 adapter probe is a once-per-process cached call. When it first runs
while a Vulkan device is live, `EnumAdapterByGpuPreference` /
`D3D12CreateDevice` find no feature-level-11_0 adapter, DX12 is recorded as
unavailable for the rest of the session, and the app silently drops to
Software. The live Vulkan device is lost in the same moment.

**Not caused by this refactor.** A pre-refactor binary (2026-08-15) reproduces
the identical `available=0 ... no Direct3D 12 feature level 11_0 adapter was
found` line and the same fall back to Software.

It is also not the switching path: with the probe already positive from
startup, the same build performs 8/8 DX12 <-> Vulkan switches cleanly, and
Vulkan <-> Software and DX12 <-> Software both pass 6/6.

This matches a known invariant recorded during the 2026-08-25 renderer-switch
optimization: *the Vulkan device release in transition step 4 exists so D3D12
can enumerate adapters, and must not be skipped for speed.* The feature-check
probe does not go through that transition path, so nothing releases the Vulkan
device before it enumerates.

Fixing it means either running the probe before any Vulkan device exists, or
releasing the Vulkan device around it. That is a renderer-transition change,
not an SRP one, so it is reported rather than folded in here.

The 4x rows exercise the resolution-dependent resources the refactor touched:
the capture sidecar's sizing, the output ring's recreation, and the pipeline
rebuild.

The `DX12 low-latency pacing` and `[Vulkan] low-latency:` lines in the logs are
emitted by the two extracted controllers, so their reporting paths are
confirmed live as well.

**Not verified.** Everything below is still open and must not be reported as
covered:

- Live resolution change. The switch driver re-applies video settings but only
  ever changes the renderer id; nothing re-reads the scale factor mid-session,
  so the resolution rows are covered per-process rather than as a live
  transition.
- True exclusive fullscreen. Maximize exercises the same surface-change path
  and is covered above; a borderless or exclusive fullscreen mode change is
  not.
- The structured fallback composition path and stale-generation rejection.
  Neither has an injection hook -- there is no environment switch that turns
  the native GPU2D producer off while leaving a valid structured frame, which
  is what those branches need. The `[GPU2DFallbackCounters]` line would at
  least report how often they were taken, but it is emitted from
  `Renderer::Stop()` and does not reach the captured pipe at process exit, even
  when the window is closed gracefully rather than killed.
- Linux / macOS / BSD builds. In particular the new
  `melonprime_gpu2d_frame_policy_tests` target builds on every platform and has
  only been compiled with MinGW g++ 14.2.
- `MELONPRIME_FORCE_DISABLE_DX12`, `MELONPRIME_FORCE_DISABLE_VULKAN`, and a
  release-profile build with developer features OFF. These were attempted and
  could not be completed in this environment: each needs a fresh build tree,
  and a fresh tree runs a vcpkg manifest install, which fails with "both
  %LOCALAPPDATA% and %APPDATA% were unreadable". Only the already-provisioned
  `build/release-mingw-x86_64` tree can build here. The static gate checks
  above cover the structural half of what those builds would have caught; they
  do not cover a guard missing inside an existing file.

## Against the audit's component matrix (§8)

§8 grades twenty components per backend, which is finer than §15's checklist
and grades different things. Ratings below are the audit's own vocabulary; a
cell only moves where the change is real.

| Component | Vulkan (audit → now) | DX12 (audit → now) |
|---|---|---|
| Build gate | PASS → PASS | PASS → PASS |
| Backend selection | PASS → PASS | PASS → PASS |
| Runtime loader | PASS → PASS | **PARTIAL → PASS** |
| Instance / factory bootstrap | PASS → PASS | PASS → PASS |
| Device owner | PARTIAL PASS → PARTIAL PASS | **PARTIAL → PARTIAL PASS** |
| Memory allocator / resource wrapper | PASS → PASS | **FAIL/PARTIAL → PASS** |
| Memory admission | PASS/PARTIAL → PASS/PARTIAL | PASS/PARTIAL → PASS/PARTIAL |
| Descriptor management | PASS → PASS | **PARTIAL → PASS** |
| Command / fence infrastructure | PASS → PASS | **PARTIAL PASS → PASS** |
| Texture cache | PASS → PASS | PASS → PASS |
| 3D raster responsibility | **FAIL → PASS** | **FAIL → PASS** |
| GPU2D compositor | **FAIL → PASS** | **FAIL → PASS** |
| Capture bridge | **FAIL → PASS** | **FAIL → PASS** |
| Output publisher | **FAIL → PASS** | **FAIL → PASS** |
| Pipeline repository | **PARTIAL → PASS** | **FAIL/PARTIAL → PASS** |
| Presenter | PARTIAL PASS → PARTIAL PASS (smaller) | PARTIAL PASS → PARTIAL PASS |
| Present pacing | PASS/PARTIAL → PASS | **FAIL placement → PASS** |
| Low-latency pure policy | PASS → PASS | PASS → PASS |
| Surface lifecycle | PASS → PASS | PARTIAL → PARTIAL |
| Telemetry separation | PASS → PASS | PASS/PARTIAL → PASS/PARTIAL |

Where the reasoning is not obvious:

- **Device owner, DX12 → PARTIAL PASS.** The audit's complaint was that
  `DX12Context` was "wider" than `VulkanDevice`: loader, shader compiler,
  resource factory and memory budget on top of device ownership. Three of the
  four are out. What remains — device ownership plus a memory-admission
  gateway — is the same shape `VulkanDevice` has, which the audit rated
  PARTIAL PASS. The two backends now sit in the same cell.
- **3D raster responsibility → PASS, both.** The FAIL was for owning GPU2D,
  capture, output and resources on top of raster. All four are now separate
  modules. What remains in `Renderer3D` is polygon/span setup, binning, raster,
  texture, depth, fog and the 3D framebuffer — the audit's own list for Phase 2
  item 7 — plus the sub-components it composes and the thin wrappers the
  `Renderer3D` interface requires.
- **GPU2D compositor → PASS, both.** `<Backend>Gpu2DComposer` owns the compose
  passes, the publication state and the compositor's resources;
  `<Backend>Gpu2DOutput` owns the per-resolution resource set. What the
  compositor borrows from the rasterizer is enumerated in one context struct,
  including the read-only `FinalFB` contract §item 6 asked for.
- **Output publisher → PASS, both.** `RendererOutputRing` owns the ring, the
  lease and the serial sequence; the content and published generations moved
  onto the compositor with the compose passes that maintain them. The renderer
  keeps only the resource-lifetime counter, which it advances because it is the
  thing that recreates resources.
- **Presenter, Vulkan → still PARTIAL PASS.** The audit's grade was "coherent
  as final presentation, but large". `VulkanLatencyController` removed the
  vendor state machine, so it is smaller, but swapchain, layers and pacing are
  still one class — the same grade, honestly earned rather than upgraded.
- **Present pacing, DX12 → PASS.** This was the audit's only outright
  "FAIL placement": vendor orchestration living on the renderer. The markers
  now fire from the presenter, around the real `IDXGISwapChain::Present`.

Net: 10 cells improved per backend, no cell regressed, and no FAIL remains on
either backend. Every remaining non-PASS is a component the audit itself rated
LOW priority (`VulkanDevice` budget/diagnostics, DX12 surface lifecycle) or one
it graded PARTIAL for size rather than for mixed responsibility (both
presenters).

## Against the audit's own Definition of Done (§15)

Checked item by item rather than against a running list, because the two are
not the same thing.

### Architecture — 6 of 6

| Item | Status |
|---|---|
| `DX12Renderer` owns no vendor low-latency object | **met** |
| `Screen.cpp` does not mediate DX12 Present markers | **met** |
| `GPU_Vulkan` / `GPU_DX12` frame-policy duplication reduced | **met** |
| Renderer3D facade separated from GPU2D / Capture / Output | **met** — all three are separate modules on both backends |
| backend-neutral modules hold no native handle | **met** |
| no cycle in the ownership direction | **met** |

The four backend-neutral modules were grepped for `Vk*` / `ID3D12*` / `D3D12_`
/ `DXGI` and hold none, and no low-level DX12 module includes `DX12Context.h`.

### Correctness — 6 of 6

| Item | Evidence |
|---|---|
| current frame identity preserved | `renderer-output-ring-tests` pins the serial rules; 2366/2118 frames published in order on hardware |
| capture provenance preserved | `capture-provenance-tests`; 2264 (Vulkan) / 2016 (DX12) address checks, every mismatch counter zero |
| native GPU2D exact path preserved | exact-validation gate engaged and rejected no frame on either backend |
| Software parity preserved | **met** — 2D via the exact gate, 3D via the raster differential: 6389 (Vulkan) and 7122 (DX12) consecutive frames bit-identical to the software rasterizer |
| renderer switch touches no stale resource | 8/8 DX12 <-> Vulkan and 6/6 <-> Software switches with no fault |
| derived GPU state rebuilt after savestate | savestate loads, then native composition resumes, in every run |

### Performance — 6 of 6

These are all "do not add X" properties, so they are verified structurally
against the base commit rather than by timing.

| Item | Evidence |
|---|---|
| no added per-frame heap allocation | the per-frame entry points into the new modules allocate nothing: `GPU2DFramePolicy` is `constexpr` over PODs, `RendererOutputRing` allocates its lease array once per output state, `CaptureProvenanceState` is POD, `BuildStructuredUploadPlan` returns `std::array` members by value. The only `std::string` construction is in vendor-state logging, which runs on state change and is unchanged from the original |
| no virtual dispatch added to a hot path | none of the new classes declares a `virtual` member. The three callbacks are plain function pointers, and the only per-frame one runs at most three times |
| no queue idle added to steady state | `WaitIdle` / `WaitQueueIdle` / `DeviceWaitIdle` counts are unchanged; `WaitForSubmittedValue` went 3 -> 2 in `GPU3D_DX12.cpp` because one moved into `DX12CaptureBridge`, which holds exactly 1 |
| no added descriptor rewrite | `CreateUnorderedAccessView` 1 -> 1 and `Descriptors.Allocate` 9 -> 9; the UAV table conversion changed how the fourteen entries are written, not how many |
| frames-in-flight unchanged | DX12 compositor 3, Vulkan compositor 3, Vulkan renderer 2, `FramesInFlight` 3 / 2 — all identical to the base |
| no readback added to a visible path | `CaptureBridge::ReadBlocks` is reachable only from `ReadNativeCapture`, itself reachable only from `SyncVRAMCapture` — the demand-driven path, as before |

### Build — 7 of 7

| Item | Status |
|---|---|
| Vulkan build gate | **met** — `MELONPRIME_FORCE_DISABLE_VULKAN=ON` configures ("Vulkan backend: disabled") and builds |
| DX12 build gate | **met** — `MELONPRIME_FORCE_DISABLE_DX12=ON` configures ("DirectX 12 backend: disabled") and builds |
| Windows | **met** |
| Linux Vulkan | **met** — Ubuntu workflow green on `develop_hud`, x86_64 and aarch64 |
| BSD Vulkan build | **met** — BSD workflow green: FreeBSD, NetBSD and OpenBSD, all x86_64 |
| macOS Vulkan / MoltenVK | **met** — macOS workflow green: x86_64, arm64 and the universal binary |
| developer flags OFF, release-equivalent | **met** — configured with `MELONPRIME_ENABLE_DEVELOPER_FEATURES=OFF` and built |

The gate builds were the ones previously written off as impossible. They are
not: the vcpkg failure ("both %LOCALAPPDATA% and %APPDATA% were unreadable")
only affects a *fresh* tree, because only a fresh tree runs the manifest
install. Reconfiguring the already-provisioned
`build/release-mingw-x86_64` tree with the gate flags reuses the existing
`vcpkg_installed` and builds fine. Each gate was built in turn and the tree was
then restored to the developer configuration and rebuilt; the cache now reads
`MELONPRIME_ENABLE_DEVELOPER_FEATURES=ON` with both force-disable flags `OFF`,
and the full build with all suites passes.

The three platform rows were closed by running the repository's own CI on
`develop_hud` (`gh workflow run <workflow>.yml --ref develop_hud --repo
ag-advania/melonPrimeDS`), which is real host validation on real Linux,
macOS and BSD machines rather than a cross-compile.

They could not be closed earlier because all three workflows were failing on
the audit gate before they ever reached a compiler — see the audit gap
noted above. Once those audits were retargeted, every platform built clean:

| Workflow | Jobs |
|---|---|
| Ubuntu | Audits, x86_64, aarch64, artifact assembly |
| macOS | x86_64, arm64, Intel MoltenVK diagnostic, universal binary |
| BSD | FreeBSD x86_64, NetBSD x86_64, OpenBSD x86_64, artifact assembly |

The remaining annotations on those runs are pre-existing environment noise
— a `.gitmodules` submodule warning, Homebrew tap trust, and an
artifact-upload option in the release step — and none of them gate a job.

Re-confirmed on the tree that closes REAUDIT-P2-002: Ubuntu and BSD green
first time, macOS green on a re-run of the same commit. The first macOS
attempt failed in the no-ROM MoltenVK smoke, which waits for presenter
readiness on a paravirtual GPU inside a VM and timed out before
`[Vulkan] presenter ready:` appeared. That step is flaky rather than a
regression, and the reasoning is checkable rather than assumed: every
source change in that range is inside `_WIN32 && MELONPRIME_ENABLE_DX12`
or on a path the no-ROM smoke never reaches, and the same commit passed
unchanged on the retry.

### Runtime — 8 of 8

| Item | Status |
|---|---|
| Vulkan | **met** |
| DX12 | **met** |
| renderer switch | **met** — 8/8 and 6/6, plus one pre-existing defect found and attributed |
| resize / fullscreen | **met** for resize, maximize, minimize, restore; exclusive fullscreen not covered |
| GPU2D | **met** — native path, startup fallback, backpressure, exact gate |
| capture | **met** — provenance, address agreement, injected readback failure |
| low latency | **met** — Reflex Off/On/On+Boost, Anti-Lag, XeLL and its policy |
| savestate | **met** |

### Summary

**33 of 33 met.**

The 2026-08-26 re-audit reopened two of the Runtime rows -- renderer switch and
low latency -- and the push-review of `b71843c65` reopened the first of those a
second time as REAUDIT-P2-002. All three findings are closed by
REAUDIT-P1-001, REAUDIT-P2-001 and REAUDIT-P2-002, with the evidence recorded
with each. The three platform build rows are closed by the repository's own CI
on `develop_hud`: Ubuntu (x86_64, aarch64), macOS (x86_64, arm64, universal)
and BSD (FreeBSD, NetBSD, OpenBSD) all green.

The `Gpu2DComposer` item that was previously listed here as blocked is done.
The blocker was mis-stated: §11.3 forbids *mixing* a descriptor-strategy change
into a responsibility move, not the move itself. Separating ownership while
leaving the fourteen-slot UAV table assembled from borrowed handles is exactly
the "structure only, same behaviour" step it asks for first, and that is what
landed. Splitting the table itself remains out of scope, and the module headers
say so.

## Re-audit, 2026-08-26: the two reopened runtime items

A second pass over the landed tree accepted the SRP work -- the component
matrix stays at zero FAIL -- but reopened two runtime Definition-of-Done rows
that the first pass had not applied strictly enough, and confirmed one stale
comment. Both runtime items are correctness bugs that the responsibility
refactor neither caused nor fixed.

### REAUDIT-P1-001 -- the first DX12 probe in a Vulkan process

This was already recorded below as a pre-existing defect. It is now fixed.

The cause was ordering, not capability. `NormalizeRendererForPlatform()` asked
`DX12FeatureCheck::IsRuntimeAvailable()`, and that call created a real D3D12
device and probed the vendor low-latency runtimes. Normalization runs at the
top of a renderer transition, before the outgoing backend is released, so on a
Vulkan -> DX12 switch it created a D3D12 device while a VkDevice was still
live. The adapter enumeration came back empty, the answer was cached as
permanent, and every later DX12 request in that process fell to Software.

Availability is now two stages:

| Stage | What it does | When it may run |
|---|---|---|
| `IsPlatformEligible()` | asks the runtime loader whether d3d12, dxgi and the shader compiler resolve -- LoadLibrary/GetProcAddress, nothing else | any time |
| `ProbeRuntimeAdmission()` | creates the device, picks the adapter, probes the vendor runtimes | only after the outgoing backend is released |

`IsRuntimeAvailable()`, which normalization calls, answers from the cached
admission result when there is one and from Stage A eligibility otherwise.
Optimistically, on purpose: a machine with no usable D3D12 device now fails at
admission, which happens after teardown and degrades cleanly, instead of
failing at a probe that runs at the worst possible moment.

Transient admission failures are no longer cached. Only a missing runtime is a
durable answer about a machine; adapter enumeration and device creation can
fail for reasons that are not about capability, so those leave the cache alone
and the next attempt re-probes. The probe log says which it was (`durable=0/1`).

The transition also carries the invariant itself: when both the outgoing and
incoming renderers own a native GPU device, the outgoing one is released before
the incoming one touches the GPU, and the transition aborts rather than
continuing if that release is refused. On the current path the panel rebuild
already drops to Software between the two, so the guard does not fire today --
it is there so a future change to that rebuild cannot silently restore the
overlap.

Evidence, RTX 5070 Ti, driving the production settings-dialog switch path:

| Run | Before | After |
|---|---|---|
| Vulkan start, first DX12 switch | `available=0 ... no Direct3D 12 feature level 11_0 adapter was found`, then Software for the rest of the process | `available=1 adapter="NVIDIA GeForce RTX 5070 Ti"`, `transition complete actual=4` |
| Vulkan <-> DX12, 4 iterations | 0 / 4 DX12 switches reached DX12 | 4 / 4 |
| Software <-> DX12, 3 iterations | PASS | PASS |
| DX12 <-> Vulkan, 3 iterations | PASS | PASS |
| `VK_ERROR_DEVICE_LOST` / renderer fallback / runtime failure | present | none |

A developer-only injection
(`MELONPRIME_TEST_DX12_TRANSIENT_PROBE_FAILURES=N`) covers the caching rule
directly: the injected failure logs `available=0 durable=0`, DX12 still
activates, and the next admission re-probes to `available=1`.

### REAUDIT-P2-001 -- Present markers without a Present

`DX12SurfacePresenter::Present()` built its PresentStart/PresentEnd scope guard
at the top of the function, before the readiness checks. A frame that returned
early therefore told Reflex and XeLL that a present had started and finished
when `IDXGISwapChain::Present` was never called. The vendor runtimes model
display latency from those markers, so a phantom pair is telemetry that is
simply untrue.

The guard now wraps the API call and nothing else. RAII still closes the pair
when Present returns an error, which is correct -- the call really was made.

Three developer-only counters make it checkable instead of asserted:
`DX12ActualPresentCallCount`, `DX12PresentMarkerBeginCount` and
`DX12PresentMarkerEndCount`. All three were equal in every one of 13 report
windows across a run with window resize/maximize/minimize/restore and three
DX12<->Vulkan transitions, and again with Reflex On
(`authority=NvidiaReflex`, `active=1`, timings reporting a real narrow present
window).

One honest limit: `present_skip_count` stayed 0 in every run, because
`Screen.cpp` only calls `Present()` after `EndFrame()` succeeded. The phantom
pair was latent rather than observed, and forcing the early return is
indistinguishable from a real presentation failure -- the caller treats it as
fatal and drops DX12 -- so it cannot be exercised in isolation. What the fix
buys is a structural guarantee in place of an incidental one: no statement now
sits between the guard's construction and the Present call.

### A gap the re-audit did not find, and I should have

Five CI audits had been failing since `ee97805fc`, and I had not noticed,
because I was only running the PowerShell audits named in the working rules
plus the DX12 shader check. Every "audits pass" claim in the sections above was
based on that subset, and the Ubuntu, macOS and BSD workflows had all been
failing on the same checks before they ever reached a compiler -- which is also
why the platform builds below stayed unverified for longer than they needed to.

None of the five found a broken invariant. They are textual ratchets, and the
refactor moved the code they pointed at; `audit-low-latency-contract` was still
expecting `BeginIntelXeLLPresent()` in `Screen.cpp`, which is precisely the
placement the SRP audit told us to dismantle. Each expectation now asserts the
same invariant at its new home, and where a responsibility genuinely moved the
audit also forbids it moving back.

The lesson is the boring one: run the gate the CI runs, not the subset the
notes list. Take the list from `.github/workflows/build-ubuntu.yml`, which is
the authority, rather than from a count written down here -- that count was
already stale twice, because the gate grows and a number in prose does not.

### REAUDIT-P2-002 -- explicit retry after a DX12 runtime failure

A DX12 renderer-initialization failure latches the backend unavailable for the
rest of the process. That is right for the transition that failed. What had
never been demonstrated is that an explicit user re-selection clears it -- and
the one piece of evidence offered for it was wrong.

`c664e109c` read a switch-stress result as "later DX12 requests stay on
Software, which is the sticky runtime-failure latch behaving as designed". The
latch is real, but that run does not show it:
`MelonPrimeRendererSwitchStress::Apply()` returned early when the config already
held the requested renderer, and a DX12 failure leaves `3D.Renderer` set to DX12
because only the emulation thread's local fell back. The driver never issued
those requests, so nothing was being latched away. The property was untested,
not demonstrated.

**Three answers instead of two.** `HardUnsupported` means the runtime is not
installed and nothing a user does changes that; `RuntimeFailure` means the
runtime is there but the renderer did not come up. Only the second is
clearable, and `ReportRuntimeFailure()` will not downgrade the first into it --
otherwise a renderer failure on a machine with no D3D12 would make a permanent
answer look retryable.

**One place announces a request.**
`VideoBackend::NotifyRendererRequest(renderer, origin)`. A `User` origin clears
a latched `RuntimeFailure`; an `Automatic` origin does nothing, which is what
keeps a failing backend from becoming a fail/reset/retry loop. It logs the
origin, whether a latch cleared, and the resulting admission state.

`VideoSettingsDialog::onChange3DRenderer` calls it, and that slot fires on a
click including a click on the already-checked button -- which is exactly how a
user retries a backend that failed. The dialog's own refresh no longer resets
anything: refreshing a dialog is not a request to use a backend, and doing so
silently re-enabled a radio button for something that had just failed.

`DX12FeatureCheck::ResetProbeForRetry()` is gone. It had no callers left, and an
unreachable retry API misdirects a reader the same way a stale comment does.

**Making it testable was part of the fix.** The switch-stress driver announces
through the same production function rather than around it, and in `User`
origin no longer skips a repeat request. The smoke harness gained two verdict
shapes it did not have -- `--expect-recovery` (degrade, then come back) and
`--expect-unsupported` (never probe at all). Neither could be expressed by the
existing pass/degrade modes, which is part of why this went unnoticed.

| Test | Result |
|---|---|
| A: init failure, then explicit re-selection | **PASS** -- latch cleared once, admission re-probed `available=1`, `transition complete actual=4`, same process, no restart |
| B: same failure, `Automatic` origin | **PASS** -- 0 latch clears, 0 recoveries; the latch holds |
| C: Vulkan -> first DX12 (P1 regression) | **PASS** -- 4/4 switches reach DX12, 0 device losses |
| D: hard unsupported, 8 explicit requests | **PASS** -- **0 device probes attempted**, 0 latch clears, DX12 never activated |
| E: Vulkan/DX12/Software cycle, 19 transitions | **PASS** -- 0 device losses, 0 fallbacks, 1 probe for the whole process |

Test D is the one worth reading twice. On a machine without the runtime, passive
eligibility alone rejects DX12, so the heavy probe is never attempted no matter
how many times the user asks. `MELONPRIME_TEST_DX12_FORCE_HARD_UNSUPPORTED`
models that machine, which one with D3D12 cannot otherwise reach.

### REAUDIT-P3-001 -- a comment pointing the wrong way

`VulkanGpu2DOutput`'s header still said the renderer records the compose
dispatches, which stopped being true when they moved. Corrected on both
backends, along with an orphaned doc block and a count that said four pipeline
handles where a compose dispatches three. Small, but this refactor's whole
point was to write ownership down, and a stale ownership note points the next
implementer back at Renderer3D.

## Ownership closure, 2026-08-26

The Definition of Done above stays at 33/33. This section adds a stricter
criterion on top of it, from a review of the landed tree:

> the unit that **declares** a responsibility is the unit that **creates,
> destroys, resets and mutates** its state.

Both compositors passed the first test and failed the second. They declared
themselves the owner of the output resource set and of publication state, and
the renderers did the owning: they built the `shared_ptr`, reset it, wrote the
four publication fields, walked the ring and every slot on reset, and held the
resource generation counter. Nothing was visibly broken -- this is a P3
hardening, not a defect report -- but a declared owner whose state someone else
writes is half an owner, and the missing half is where a lifetime bug lives.

### What each compositor now offers

| Operation | Replaces |
|---|---|
| `RecreateOutput` | `Output = make_shared<...>` |
| `ReleaseOutput` | `Output.reset()` |
| `ResetForRendererEpoch` | walking `Ring` / `Slots` / `WorkSlots` in `ResetInternal` |
| `MarkFatal` | `LastComposeResult = Fatal` |
| `GetComposedOutput` | reading `Output` plus the published slot |
| `AcquireComposedOutputLease` | the lock/lookup/lease/log sequence |
| `HasValidOutput`, `GetPublishedOutputGeneration`, `GetLastComposeResult` | direct field reads |

The four publication fields and `NextOutputResourceGeneration` are private.
Named operations, not setters: `MarkFatal()` says what happened,
`SetLastComposeResult(Fatal)` would only say where it was stored.

### Two things deliberately not hidden

`Output` stays readable. DX12 assembles one fourteen-entry UAV table out of
raster resources and slot resources together, and Vulkan's rasterizer set-0
write binds slot 0's structured input. Hiding it behind accessors would
describe a boundary the descriptor contract does not have. Reading is fine;
every mutation goes through an operation.

`ReleaseOutput` rewinds descriptor **contents** and never **heaps**. The ring
cursors and cached table bases described one resource set, so they go with it;
the heaps are sized from the root-signature layout and outlive every
resolution. `ShutdownDescriptors()` is still the heap-lifetime operation. These
are two lifetimes, and collapsing them would have destroyed a heap on every
scale change.

### One real bug found on the way

DX12's create read `Output = make_shared<>(); if (!Output->Create(...))` --
publishing the pointer first and initializing through it. On the failure path
that left a half-initialized set reachable as the active `Output`, which both
the presenter and the compose path read without asking whether it had finished
being built. Both backends now build a candidate and adopt it only on success,
which is what Vulkan already did.

### The lease invariant, unchanged

`RendererOutputLease` still captures the `shared_ptr`. A presenter reading an
old resource set across a resolution change keeps it alive until it releases.
`ReleaseOutput` detaches; it never destroys under a lease.

### Ratchet

`audit-melonprime-srp-performance.ps1` now hard-fails on assignment to any
publication field, to `Output`, on `Output.reset()`, on a direct `make_shared`
of either `Output` type, and on any reappearance of
`NextOutputResourceGeneration` in a renderer -- and it requires the compositors
to still offer all eight operations, so the check cannot be satisfied by
deleting the call sites. Reads of `Gpu2D.Output` stay legal, because forbidding
them would forbid the shared descriptor table.

It was proved to fire rather than assumed to: each forbidden shape was injected
into `GPU3D_Vulkan.cpp` in turn and rejected, and the restored tree passes. An
audit nobody has watched fail is a comment.

That first version had two holes, found by a push review, and the way it failed
is worth keeping. Both let a future regression through while the audit stayed
green:

- **Multiline assignment.** The patterns ended in `[^=]`, which needs one more
  character on the same line. `Gpu2D.Output =` followed by `candidate;` on the
  next line matched nothing -- not an obfuscation, just how a formatter breaks a
  long line. Now `(?!=)`, which matches an `=` at end of line and still rejects
  `==` and `!=`.
- **Comment-only presence.** "The compositor must still offer these operations"
  was a search for the name anywhere in the header, so a comment mentioning
  `RecreateOutput` satisfied it while the declaration was gone. Now it matches
  declaration shape, and the generation counter is checked as a member.

The second hole is the sharper lesson: the check was written *to prevent*
exactly the substitution that satisfied it. Watching an audit fail is necessary
but not sufficient -- it has to fail for the reason you think. The negative
suite is now 20 cases, including both multiline bypasses, both comment-only
bypasses per header, and two equality reads that must stay legal.

### Evidence

| | Vulkan | DX12 |
|---|---|---|
| scale sweep 1x/4x/5x/8x/9x, savestate + 60 SemanticOnly frames | 5/5 | 5/5 |
| raster differential at 1x | 2129 frames, 0 mismatches | 2102 frames, 0 mismatches |
| GPU2D exact-stage validation | 13,766 records, 0 mismatches | 13,603 records, 0 mismatches |
| frames published through the native path | 2159 / 2159 | 2132 / 2132 |
| window actions + Vulkan/DX12/Software cycle | 21 transitions, 0 device losses | same run |

The DX12 admission regressions still pass unchanged: A explicit retry recovers,
B `Automatic` origin stays sticky, C Vulkan -> first DX12 activates, D
`HardUnsupported` attempts 0 device probes.

Live scale transitions were added afterwards, because a process per scale never
releases and recreates an output set inside a living renderer -- the one path
the ownership move actually changed. Eight live changes per cycle, through the
same slot the resolution combo reaches:

| Cycle | Vulkan | DX12 |
|---|---|---|
| 4 <-> 5 | PASS, generations 1..9 | PASS, generations 1..9 |
| 8 <-> 9 | PASS, generations 1..9 | PASS, generations 1..9 |
| 1 <-> 4 | PASS | - |

with no device loss, fallback or runtime failure. The generation check is per
composer instance: the counter lives on the compositor, so a renderer torn down
and rebuilt starts a fresh one at 1, and everything that had cached descriptors
against the old sets went away with it. The first version of that check compared
across instances and flagged a legitimate restart.

That test then failed its own review, in the way worth recording. It counted
scheduled steps rather than applied ones, so cycling `4,4` from a 4x start armed
the driver, logged eight steps, applied nothing, and passed -- a live-scale test
that never changed scale. The step log was written before the change was
attempted, `ApplyScale` returned void, and a single `resourceGeneration=1`
satisfied the generation check.

`ApplyScale` now returns Applied / NoOp / Failed, logs the outcome after the
fact, and the cycle ends with a tally the harness reads as the authority:

    [scale-stress] complete: requested=8 applied=7 noop=1 failed=0

Real runs now reconcile rather than being asserted: eight requests, one leading
no-op because the first element equals the starting scale, seven applied, and
generations 1..9 -- the initial creation plus seven changes plus the restore.
The verdict itself is now a function rather than inline code, pinned by
`tools/testing/scale-stress-verdict-tests.py` -- 12 synthetic logs covering both
directions, including the ones that must still pass: a leading no-op, and a
mid-run renderer restart whose generations legitimately begin again at 1. That
test was checked against a deliberate regression rather than assumed to work.

Being untestable without a GPU is what let the first version ship believing
request lines were proof of a change.

One note on the scale sweep: 5x and 9x first failed at 15 seconds with window
actions, and passed at 40 seconds without them. Those scales compile more
pipeline variants from a cold cache, and the savestate injection had not landed
before the run ended. That is the harness's clock, not the renderer -- worth
recording because "a scale that fails" and "a scale that needs longer" look
identical in a PASS/FAIL line.

### Platform CI

Re-confirmed on the ownership-closure tree: Ubuntu (x86_64, aarch64), macOS
(x86_64, arm64, universal) and BSD (FreeBSD, NetBSD, OpenBSD) all green,
first attempt. Green again on the tree that hardens the ownership ratchet, and
again on the one that reworks the scale-cycle test and pins its verdict.

### This is the end of splitting

No further Vulkan/DX12 SRP split happens without one of: an independent reason
to change, a different lifetime, a real ownership conflict, or an actual
regression or testability problem. Not file size, not member count, not what
another API's class diagram looks like.

## What is left

### The rest

Phase 2 is complete on both backends: the output publisher, capture bridge,
GPU2D compositor and pipeline repository are all separate modules, and what
remains in each `Renderer3D` is the 3D rasterizer plus the sub-components it
composes.

Two refinements are deliberately not done, and neither is a responsibility
problem:

- **The compositor's slot record still mixes two roles.**
  `<Backend>Gpu2DOutput::Slot` carries both the presentation record (the
  composed and direct resources, the frame descriptor) and the upload record
  (structured staging, content generation); `ComposeWorkSlot` likewise mixes
  compose work with capture scratch. Splitting those is a pure data reshuffle
  inside one module now, not a class boundary move, and it should be done with
  the golden hashes confirmed unchanged.
- **The descriptor strategy.** DX12 binds one fourteen-slot UAV table whose
  slots are overloaded by dispatch kind, and Vulkan keeps the compositor's
  pipelines in the rasterizer's shader-step array. Both are deliberate: the
  audit's §11.3 rules out folding a descriptor or pipeline-construction change
  into a responsibility move, and each module header records the constraint so
  the next reader does not mistake it for an oversight.

Phase 3 items the audit rated LOW remain open by its own reckoning:
`VulkanDevice` budget and diagnostics (VK-SRP-002), and a
`DX12SurfacePresenter` swapchain/layer split (DX-SRP-004), which is not needed
yet.

## Rules for anyone continuing this

- `GPU2DFramePolicy.h`, `RendererOutputRing.h`, `CaptureProvenanceState.h` and
  `StructuredUploadPlan.h` must never gain a native GPU handle. Their tests are
  what keep the decision tree, the publication protocol, the capture acceptance
  rules and the upload layout honest; extend them rather than replacing them
  when the rules change.
- When a backend-neutral extraction suddenly needs a large link closure to
  test, the boundary is wrong. That is how the sidecar tracker was found to
  belong on the renderer rather than in `CaptureProvenanceState`.
- `DX12LowLatencyController` must stay the only owner of the vendor sessions.
  If a new caller needs a marker, add a frame phase — do not re-export a vendor
  method from a renderer or a panel.
- Low-level DX12 modules must not include `DX12Context.h`.
- A component borrows through its context struct, never through a pointer back
  to the renderer. `DX12Gpu2DComposeContext` / `VulkanGpu2DComposeContext` are
  rebuilt per call for that reason; a cached one could outlive the resources it
  names.
- Callbacks into the renderer stay plain function pointers. These run once per
  DS frame on the emulation thread; `std::function` would put an allocation and
  an indirect call in that path.
- Structure and behaviour do not change in the same commit.
- A verdict the test harness cannot express is a verdict that will not be
  checked. `--expect-recovery` and `--expect-unsupported` exist because
  "degrade then come back" and "never probe at all" had no shape in the
  harness, and a property with no shape is one nobody notices is untested.
- When a run seems to confirm something, check that it actually exercised it.
  The switch-stress driver skipped requests whose target was already the
  configured renderer, which is exactly the case a post-failure retry is, and
  that silence was read as evidence.
- The unit that declares a responsibility creates, destroys, resets and mutates
  its own state. A declared owner whose state someone else writes is half an
  owner, and the compositors were that for three commits before anyone looked.
- An audit nobody has watched fail is a comment. Inject each forbidden shape
  and confirm it is rejected before claiming a ratchet exists -- and check it
  fails for the reason you think. The ownership ratchet's operation-presence
  check passed on a comment that merely named the operation, which is the exact
  substitution it was written to prevent.
- A textual ratchet must survive ordinary formatting. `[^=]` after an assignment
  needs another character on the same line, so a wrapped line defeats it; `(?!=)`
  does not. Write the negative test with the line already broken.
- A test proves what it counts, not what it is named after. The scale cycle
  counted scheduled steps and called them applied changes, so a run that
  changed nothing passed. Count the outcome the operation reports, and make the
  operation report one.
- Before claiming something is blocked, quote the rule that blocks it and check
  that it says what you remember. Several "blockers" in this document's history
  were mis-readings, the last of them the one that supposedly ruled out this
  whole compositor extraction.
