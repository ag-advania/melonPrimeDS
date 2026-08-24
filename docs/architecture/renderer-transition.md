# Runtime renderer transition

What happens when the 3D renderer changes at runtime — from the Video settings
dialog, from the developer switch-stress driver, or from a renderer runtime
fallback — and where the time goes.

A transition is not one operation. It is split across two threads, and neither
side can see the other's cost, which is why "switching renderers is slow" used
to be unattributable.

## Sequence

`MainWindow::onUpdateVideoSettings(glchange = true)` drives the whole thing on
the GUI thread:

1. `emuThread->emuPause()`.
2. Destroy every screen panel (`destroyScreenPanel`), releasing the
   presenter's references to the old backend.
3. `prepareVideoBackendTransition()` — a message to the emulation thread, which
   installs the **Software** renderer as a placeholder. This is what actually
   destroys the outgoing 3D renderer, while its backend is still alive.
4. On Windows, `VulkanDevice::ReleaseRetainedDeviceForBackendTransition()` —
   the retained Vulkan device must not outlive this point, or a following
   D3D12 adapter enumeration can misbehave.
5. Create the new screen panel, which acquires the new backend's context and
   creates its device and swapchain.
6. `emuThread->updateVideoSettings()` + `emuUnpause()`. The **target** renderer
   is constructed on the emulation thread, on the first frame after the
   unpause, followed by `SetRenderSettings()`.

Steps 3 and 6 are the only places the 3D renderer object changes; the GUI thread
can only pause, swap panels, and unpause.

## Phase profile

`MelonPrime::RendererTransitionProfile` (`MelonPrimeRendererTransitionProfile.h`)
logs one `[RendererTransition] phase=… ms=…` line per phase and a `total_ms`
line at the end, always on — a switch is a cold, user-initiated event, so a
handful of Info lines per switch costs nothing and makes a slow switch
self-describing in any user's log.

`Begin()` opens the window on the GUI thread; `Mark()` is called from both
threads; `End()` runs on the emulation thread once the new renderer is live, so
`total_ms` covers the whole user-visible hitch rather than just the GUI half.

Two deeper profiles are developer-build only and gated on
`MELONPRIME_RENDERER_STARTUP_PROFILE=1`:

- `[RendererStartup] stage=…` — per-stage renderer construction
  (`pipeline_cache`, `fixed_resources`, `scale_resources`, `pipeline`).
- `[RendererStartup] stage=shutdown_…` — per-stage renderer teardown, including
  `shutdown_device_release` / `shutdown_device_destroy`.

The developer switch-stress driver drives real transitions on a timer:

```bash
MELONPRIME_RENDERER_SWITCH_STRESS=3,4 MELONPRIME_RENDERER_SWITCH_STRESS_ITERATIONS=3 MELONPRIME_RENDERER_SWITCH_STRESS_INTERVAL_MS=3000 MELONPRIME_RENDERER_STARTUP_PROFILE=1 ./melonPrimeDS <rom>
```

## Measured cost

Developer build, RTX 5070 Ti, `3D.GL.ScaleFactor = 8`, six Vulkan <-> DX12
switches per run. Steady-state totals, excluding the first switch of a session
(cold caches):

| Direction | Before | After |
| --- | ---: | ---: |
| DX12 -> Vulkan | ~400 ms | ~246 ms |
| Vulkan -> DX12 | ~425 ms | ~291 ms |

Where the remaining time goes, per direction:

| Phase | DX12 -> Vulkan | Vulkan -> DX12 |
| --- | ---: | ---: |
| `gui-pause` | ~10 ms | ~8 ms |
| `gui-destroy-panels` | ~3 ms | ~16 ms |
| `emu-construct-renderer` (placeholder swap) | ~55 ms | ~27 ms |
| `vk-release-retained-device` | — | ~33 ms |
| `gui-create-panels` | ~160 ms | ~140 ms |
| ... of which native device creation | ~29 ms (`vk-device-create`) | ~113 ms (`dx12-context-acquire`) |
| `emu-construct-renderer` (target) | ~10 ms | ~21 ms |
| `emu-render-settings` | ~9 ms | ~33-70 ms (DX12 scale resources) |

Caveats: these are developer-build numbers, so `dx12-context-acquire` includes
the D3D12 debug layer, which a shipping build does not enable. Absolute values
also scale with `ScaleFactor` — the DX12 `scale_resources` stage allocates
buffers proportional to the internal resolution.

## What the cost is made of

The floor is one native device destruction plus one native device creation per
switch. That is deliberate: the outgoing backend's device must be gone before
the incoming one creates its own (see step 4 above), so the two cannot overlap.
Everything above that floor is what optimization can reach.

Fixed in the 2026-08-25 pass:

- **Paused-thread message latency (~120 ms/switch).** The paused emulation-thread
  loop slept a flat 75 ms per iteration and only checked for messages between
  sleeps. A switch sends it two blocking messages, so the GUI thread waited out
  that sleep twice. It now waits on `msgQueueNotEmpty` with the same 75 ms idle
  period and wakes immediately when `sendMessage()` enqueues. Measured
  `emu-transition-handoff` 60-73 ms -> 0.01 ms and `gui-unpause` 52-66 ms ->
  0.1 ms.
- **Redundant D3D12 adapter probe (~43 ms/switch).** `PickAdapter` validated
  each adapter with `D3D12CreateDevice(..., nullptr)` and then created the
  device on the accepted one, initializing the driver twice. Creation is now
  the probe (`PickAdapterAndCreateDevice`). Measured `dx12-context-acquire`
  156 ms -> 113 ms.
- **Vulkan loader thrash.** `VulkanContext::DestroyInstance()` unloaded
  `vulkan-1.dll` whenever the instance count reached zero, so every switch back
  to Vulkan repeated ICD discovery. The module now stays resident; with no
  instance alive it owns no device, queue or adapter, so this is independent of
  the device-conflict rule in step 4. Only the instance-creation failure path
  still unloads, which is what makes "install a driver, retry" work.
- **Redundant cache writes.** The Vulkan pipeline cache (~1 MB) was rewritten on
  every teardown; it is now skipped when the driver's payload size still matches
  what was loaded. DX12 stopped re-serializing every PSO through
  `GetCachedBlob()` when the PSO was built from the cached blob it already
  holds.

Not addressed, and the largest remaining items:

- `dx12-context-acquire` ~113 ms is `D3D12CreateDevice` itself.
- `gui-create-panels` has ~120 ms beyond device creation on the Vulkan side
  (native surface realization, swapchain, present pipelines) that is not yet
  broken down.
- DX12 `scale_resources` (~33-70 ms at 8x) is committed-resource allocation
  proportional to the internal resolution.
- The placeholder-swap phase includes `GPU::SetRenderer`'s
  `SyncAllVRAMCaptures()`, which reads display-capture VRAM back from the
  outgoing GPU renderer. It is correctness work, not overhead.

## Rules

- Do not retain a native device across step 4 to make switching faster. The
  release point exists because a live Vulkan device changed what D3D12 saw when
  it enumerated adapters.
- Keep `End()` on the emulation thread. Ending the transition when the GUI
  thread finishes would hide the renderer construction that follows it.
- Any new work added to the transition path should show up as its own `Mark()`;
  an unattributed phase is how this got slow in the first place.
