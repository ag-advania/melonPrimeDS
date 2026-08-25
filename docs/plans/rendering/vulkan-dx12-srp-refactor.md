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
| VK-SRP-004 | `VulkanRenderer3D` monolith | HIGH | **In progress** — pipeline cache, output ring, capture provenance, upload plan, capture bridge extracted |
| DX-SRP-003 | `DX12Renderer3D` monolith | HIGH | **In progress** — pipeline repository, output ring, capture provenance, upload plan, capture bridge extracted |
| DX-SRP-001 | `DX12Context` split | MEDIUM-HIGH | **Done** |
| VK-SRP-003 | `VulkanRenderer::VBlank()` publication policy | MEDIUM | **Done** |
| BOTH-SRP-002 | `GPU_Vulkan` / `GPU_DX12` policy duplication | MEDIUM | **Done** |
| BOTH-SRP-003 | DX12 / backend-neutral test seams | — | **Partly done** |
| Phase 3 #8 | Pipeline repository, both backends | — | **Done** |
| VK-SRP-005 | `VulkanPresenter` latency controller | MEDIUM-LOW | **Not started** |
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

**Not verified.** Everything below is still open and must not be reported as
covered:

- Runtime behaviour on real hardware. No ROM session was run: nothing in the
  audit's §14 matrix (renderer switching, 1x–16x, GPU2D native/structured/
  backpressure paths, display capture, presentation, DX12 low-latency markers)
  has been exercised.
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

## Phase 2 — not started, and why

What remains of VK-SRP-004 and DX-SRP-003 after the extractions above:
`Gpu2DComposer` and `Rasterizer3D`.

### The physical `CaptureBridge` and its shared command context

This one is worth stating precisely, because three earlier "blockers" in this
document turned out to be avoidable and this one is not the same shape: it is a
real constraint on *how* the split may be done, not on whether.

`DX12Renderer3D::CaptureCommands` is not a capture-only command context. It is
the *demand-driven readback* context, used by two different responsibilities:

1. `RecordNativeResolveAndReadback()` — resolving `FinalFB` into the readback
   buffer for `GetLine()`, which is a rasterizer concern;
2. `ReadNativeCapture()` — copying native VRAM capture blocks out, which is the
   capture concern.

They are not merely co-located. They serialize *against each other* through it:
`ReadNativeCapture()` retires the pending 3D readback before reusing the
allocator (`if (NativeReadbackSubmitted && !FrameReadbackValid)
EnsureFrameReadback();`), and both wait on the same
`CaptureCommands.WaitForSubmittedValue()`. Vulkan has the same arrangement
around `CaptureFrames`.

So a `CaptureBridge` that owns the context makes the rasterizer's `GetLine()`
path reach into the capture bridge — a sibling cross-reference the audit
explicitly rules out (§5, "resource ownerを循環させない"). A `CaptureBridge`
that does *not* own it cannot perform its own readback, which is the only
operation that would make it a bridge. Giving each responsibility its own
command context removes the mutual serialization, which is a GPU submission
behaviour change — and §11.3 forbids folding queue/submission changes into an
SRP refactor.

#### The decision, and how it was resolved

**Taken: the demand-driven readback context is its own concern, owned by the
renderer facade and used by both paths.** Separate contexts are ruled out —
that removes the mutual serialization, which is a GPU submission behaviour
change and §11.3 forbids folding one of those into a responsibility refactor.
Under §12's dependency direction the context is low-level infrastructure shared
by two feature components, so it belongs to the facade above them rather than
to either one.

That makes the resolution a naming and contract change rather than a
restructuring, and the naming was the actual defect: `CaptureCommands` claimed
sole ownership by the capture path, which is why the code had to carry a
comment correcting itself at the point of use. The members are now
`DemandReadbackCommands` / `DemandReadbackDescriptors` on DX12 and
`DemandReadbackFrames` on Vulkan, with the dual use and the serialization
contract stated where they are declared.

What this leaves for a future `CaptureBridge`: it owns the capture *resources*
(`CaptureSidecarBuffer`, `NativeCaptureReadback`) and receives the readback
context, rather than owning it. No sibling reaches into another, and the
submission ordering is untouched.

The audit sequences `OutputPublisher` first, on the grounds that output leases,
resource generation and slot identity are relatively independent of the raster
algorithm. The *protocol* half of that is done (above). The *resource* half is
where the remaining work is blocked, and it blocks the composer and capture
splits too: both backends' `OutputState` is a single struct whose `Slot` and
`ComposeWorkSlot` bundle, in one allocation:

- the presentation slot ring, the lease refcount and the frame identity
  (output publication),
- the structured upload staging and its content generation (2D composition),
- the native staging/input buffers, the semantic line cache and the
  native/diagnostic readbacks (capture),
- and, on DX12, a per-slot `DX12CommandContext` and `DX12DescriptorRing`
  (synchronization infrastructure).

So the composer and the capture bridge are entangled at the level of
individual struct members: neither can be cut without deciding who owns each
buffer in those two structs. What is left of Phase 2 is therefore not three
independent steps but one restructuring of the resource model in both
backends, inside files of ~5,000 lines each.

The audit is explicit that a bad version of this is worse than not doing it —
splitting into five files that reference each other by raw pointer is not an
improvement, and ownership must stay one-way from the facade down.

The blocking constraint is verification, not effort. The audit's own Definition
of Done requires that current frame identity, capture provenance, the native
GPU2D exact path and Software parity are all preserved, across the §14 matrix,
on real hardware. Those are runtime claims. This tree also has known-open
Vulkan issues in capture-background scenes, and the established verification
method here is per-frame comparison at 120 fps — averaging or spot-checking
hides alternating-frame bugs.

### What Phase 2 should do when it is picked up

1. **Split the resource model before splitting the class.** Separate
   `OutputState::Slot` into a presentation record (the composed/direct
   resource and the frame descriptor — the lease bookkeeping is already out)
   and a composer upload record (structured staging, content generation).
   Separate `ComposeWorkSlot` into a compose work record and a capture record.
   Do this as a pure data reshuffle, with no class boundaries moved, and
   confirm the golden hashes are unchanged.
2. `Gpu2DComposer`, with an explicit read-only contract on the 3D `FinalFB`.
3. `Rasterizer3D` is whatever remains.

Do one backend at a time, and do not mix in queue configuration,
frames-in-flight, descriptor strategy, shader or barrier changes — the audit's
§11.3 rule.

### VK-SRP-005, deferred

Extracting `VulkanPresentationLatencyController` out of `VulkanPresenter` looks
symmetric with the DX12 work above, but is not. The Vulkan vendor objects have
~100 call sites, and `PresentPacer` in particular participates in swapchain
*construction* — surface capability queries, `VkSwapchainCreateInfoKHR` flags,
NV low-latency optimized present-mode selection. Splitting it means first
deciding whether present-mode selection belongs to the swapchain or to the
pacing controller, which is a design question, not a move.

The audit rates this MEDIUM-LOW and notes the Vulkan placement is already
*better* than DX12's was. It is not worth spending the risk budget before
Phase 2.

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
- Structure and behaviour do not change in the same commit.
