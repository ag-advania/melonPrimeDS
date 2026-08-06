# Vulkan compositor frame ownership

`MelonPrimeVulkanOutput` composes the structured 2D planes and the 3D color
target into one image per emulated frame, with several frames in flight. This
document records which fence protects which resource, because a resource whose
protecting fence is not unique is a data race that reproduces only when the GPU
actually overlaps two submissions.

## The rule

> Every resource written per frame lives in exactly one `FrameResource`, and
> that resource's `submitFence` is the only thing that has to be waited on
> before it is rewritten.

`acquireFrameForCpuWrite()` waits on the fence of the frame being acquired and
nothing else. That is a complete statement about safety only if the inputs the
emulation thread is about to write belong to that same frame.

## Ownership table

| Resource | Owner | Protected by | Notes |
| --- | --- | --- | --- |
| `image`, `imageView`, `imageMemory` | `FrameResource` | that frame's `submitFence` | Composition output; the presenter reads it on the same queue under the same context queue lock. |
| `commandBuffer` | `FrameResource` | that frame's `submitFence` | Reset in `beginFrameCommand()`, after the wait. |
| `descriptorSet` | `FrameResource` | that frame's `submitFence` | Rewritten per dispatch, so it must not be shared. |
| `timestampQueryPool` | `FrameResource` | that frame's `submitFence` | Results consumed only after completion. |
| `topPackedBuffer` / `bottomPackedBuffer` and their memory and mappings | `FrameResource` | that frame's `submitFence` | The structured 2D planes. See below. |
| `submitFence` | `FrameResource` | itself | One fence per slot; never shared. |
| `commandPool`, `compositorDescriptorPool` | object | `commandPoolLock` plus the fence wait in `destroyFrameResource()` | Allocation and freeing only at frame create/destroy. Never reset wholesale, so no per-frame mutation. |
| `compositorPipeline`, `compositorPipelineLayout`, `compositorDescriptorSetLayout` | object | immutable after `createCompositorResources()` | Read-only during rendering. |
| `timelineSemaphore` | object | monotonic | Signals only; carries no writable state. |
| `VulkanRenderer3D::ColorImage` | 3D renderer | pipeline barriers, not a fence | Written as a color attachment and read by this compositor as a storage image. The write-after-read dependency is pinned by `tools/ci/audits/audit-vulkan-compositor-colorimage-sync.py`. |

## Why the packed planes are per-frame

They were briefly a single object-level pair, mirroring `DX12Renderer3D`'s single
`CompositionInputBuffer`. That mirroring was wrong: DX12 serializes every
composition with `Commands.WaitIdle()`, so one buffer can only ever have one
reader. This path does not serialize, so the shared pair had no unique
protecting fence:

```
resource A   compositor dispatch, still reading the shared planes
resource B   acquireFrameForCpuWrite waits on B's fence -- says nothing about A
resource B   writes below/above/control/lineMeta into the same shared planes
```

A single dispatch could then read `below` and `lineMeta` from one emulated frame
and `above` and `control` from the next. Because the control word decides where
the 3D layer sits relative to the 2D planes, a mixed read draws the background
and 3D layer in front of the UI, alternating with correct frames as the overlap
comes and goes.

`lastComposedFrame` existed as a stand-in for "who read the shared buffer last",
but it was only ever assigned and cleared -- it was never used as a wait
condition, so nothing waited for the actual last reader. Per-frame ownership
makes the question meaningless, so the pointer is gone rather than repaired.

The cost is one extra pair of packed buffers per frame slot, a few megabytes
total, in exchange for the fence being a complete statement.

## Enforcement

- `tools/ci/audits/audit-vulkan-frame-resource-ownership.py` fails if the planes
  move back to object level, if `lastComposedFrame` returns, or if the write,
  build, dispatch or destroy paths stop going through the owning resource.
- `dispatchCompositor()` refuses inputs that name another frame's buffers and
  counts it in `packedBufferIdentityMismatch`, reported with the other ownership
  counters in the developer performance log. All of them must stay at zero.
- With renderer debug tools enabled, each composition logs `VulkanPackedFrame:`
  with its slot, generation and buffer handles. Two frames in flight must never
  report the same handles.
