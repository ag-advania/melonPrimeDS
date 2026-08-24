# Vulkan Reflex On/On+Boost — generation-owned sleep preissue A/B

- Date: 2026-08-24
- Branch: `develop_hud`
- Instruction document: `.codex/MelonPrimeDS_develop_hud_Vulkan_Reflex_ON_FPS低下_根本原因監査_修正指示書_2026-08-24.md`
- Baseline commit named by that document: `b8a5d35e1d0b9e0f41827a63d631abeecd11d0d9` (ancestor of `HEAD`)
- Working tree at measurement time: `cc6a0d2c8d439e157e3082ef45784c5c7d675be2` + local changes

## Verdict

The document's P0 hypothesis (§6.3) is **disproven by measurement**, and the
proposed fix (§7) was **A/B tested and not adopted**.

Per §21 Phase 6 this is **Case C**: `vkLatencySleepNV` itself blocks. It is not
Case A (late request), and not Case B (semaphore withheld until a deadline).

The cost is nevertheless real and Vulkan-specific: the same sleep costs 1.2 us
on DX12 under an identical workload, with DX12's Reflex verified engaged. It is
not, however, reachable from this repository's scheduling -- the time is spent
inside the driver call. See "The DX12 reference" below.

## Phase 1 — per-API split (developer build, `MELONPRIME_PERF=1`)

Build `build/release-mingw-x86_64` (developer features + renderer perf telemetry
ON). Same F7 conditions as Phase 4. One report per second; the row below is the
last report of the measured window.

| Config | `reflex_latency_sleep_us` (worker) | `reflex_sleep_wait_us` (worker) | `reflex_sleep_join_us` (hot path) | n/s |
|---|---:|---:|---:|---:|
| Vulkan Off | p50 1798, p95 1946, max 2013 | p50 2.0, max 7.7 | **p50 191**, p95 311, max 485 | 534 |
| Vulkan Reflex On | p50 1247, p95 1769, max 1936 | p50 2.0, max 8.3 | **p50 1240**, p95 1740, max 1933 | 344 |
| Vulkan Reflex On+Boost | p50 1258, p95 1882, max 2008 | p50 2.0, max 8.3 | **p50 1249**, p95 1880, max 2004 | 336 |

Three conclusions follow directly:

1. **The semaphore wait was never the cost.** `vkWaitSemaphores` is p50 2 µs in
   every mode. §11's decision tree resolves to case C, and the document's
   framing of the problem as "sleep issued then immediately joined" is
   describing the wrong API.
2. **The preissue path really executed.** `reflex_sleep_join_us` carries
   344 samples/s under Reflex On, matching the frame rate, so the null result is
   not a silent fallback to the inline path.
3. **The overlap window is the limiting factor, not the request time.** Off
   overlaps its sleep with an entire emulated frame (input, simulation, render,
   submit ≈ 1.8 ms) and its hot-path join collapses to 191 µs. On/On+Boost must
   complete the wait *before* input sampling, so the only overlappable work is
   the presenter cleanup between present and the next `BeginFrame` — tens of
   microseconds. Preissuing moved p50 1247 µs to p50 1240 µs.

The Reflex On frame is therefore `sleep(1.25 ms) + work(1.72 ms) ≈ 2.97 ms`,
serialised by the latency contract itself. §7 assumed the ~1 ms was fixed call
overhead that could mature in the background; with pacing enabled it is a
deliberate driver block, and a block cannot be overlapped by work that is
required to happen after it.

## Phase 4 — Shipping F7 matrix

Build: `tools/build/windows/build-mingw-release.bat --jobs 1`,
`build/release-mingw-shipping-x86_64`, developer features / renderer telemetry /
Vulkan latency capture all OFF. Executable SHA-256 `9c13549e…` (the baseline run
used for comparison was `fc8bf47d…`).

Harness: `tools/testing/renderer-physical-ab.ps1`, F7 savestate slot 7, Scale 2,
`-NoVSync -NoFrameLimit`, action `savestate-load`, HUD On, warmup 5 s, measured
10 s, 20 window-title samples at 500 ms. Three runs per cell.

Measured **with the preissue applied to On/On+Boost** (the variant since reverted):

| Renderer | Low Latency | median FPS | run medians | document baseline |
|---|---|---:|---|---:|
| OpenGL Compute | Off | 537 | 537 / 537 / 537 | 537 |
| DX12 | Reflex On | 533 | 533 / 533 / 532 | 534 |
| DX12 | Reflex On+Boost | 533 | 533 / 533 / 532 | 533 |
| Vulkan | Off | 534 | 534 / 534 / 534 | 533 |
| Vulkan | Reflex On | 332 | 332 / 332 / 333 | 337 |
| Vulkan | Reflex On+Boost | 333 | 335 / 333 / 331 | 342.5 |

Every cell the change was not supposed to affect reproduces the document's
baseline, which establishes that the rig and conditions match the ones the
baseline figures came from. Off is not regressed. On/On+Boost did not improve;
the §20 target of ≥507 FPS is not met and is not approached.

## Phase 4 (confirm) — Shipping F7 after the revert

Rebuilt Shipping with On/On+Boost restored to the inline path. Same conditions,
three runs per cell.

| Renderer | Low Latency | preissue variant | **shipped (reverted)** | document baseline |
|---|---|---:|---:|---:|
| Vulkan | Off | 534 | **534 / 534 / 533** | 533 |
| Vulkan | Reflex On | 332 | **337 / 338 / 339** | 337 |
| Vulkan | Reflex On+Boost | 333 | **337 / 340 / 340** | 342.5 |

The revert restores the baseline exactly and is also the faster of the two
variants: the preissue's extra worker hop and condition-variable join cost about
5 FPS in On/On+Boost. The A/B is therefore negative in both directions —
preissuing neither helps latency-side nor throughput-side.

## Phase 5 — 1x exact validation

Shipping build, Vulkan, Scale 1, `-ExactGPU2DValidation`, action
`savestate-load`, 8 window captures per run. F1, F2, F6 (savestate containing
video), F7, each under both Low Latency Off and Reflex On.

| Run | mismatches | fallback frames | fallback lines | bad markers | unexpected blanks | result |
|---|---:|---:|---:|---:|---:|---|
| exact2-f1-off | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f2-off | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f6-off | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f7-off | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f1-reflex | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f2-reflex | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f6-reflex | 0 | 0 | 0 | 0 | 0 | PASS |
| exact2-f7-reflex | 0 | 0 | 0 | 0 | 0 | PASS |

All eight exit 0 with config and layer settings restored byte-for-byte. §19.1's
requirement (mismatch = 0, fallback = 0, bad marker = 0) is met.

A first attempt at this phase exited 1 on every run without passing
`-CaptureFrames`: the harness refuses a Vulkan run that produced no capture
evidence. That was a missing precondition on the invocation, not an exactness
failure, and the runs above supply the evidence the gate asks for.

## What was kept, and what was reverted

**Reverted** — On/On+Boost keep the inline `vkLatencySleepNV` + wait in
`BeginFrame`, on the presenting thread, as before. A vendor pacing integration
should not be moved onto a worker thread for a measured gain of 7 µs out of
1247 µs, and in shipping conditions the move actually cost about 5 FPS. The A/B
was run, it came back negative, and the change was dropped.

**Kept** — the hardening the A/B required, which stands on its own:

- Pacing-sleep **generation ownership**
  (`VulkanReflexSleepIsOwnedByFrame`): the sleep issued after present N belongs
  to frame N+1, and `FinishFrame()` joins only a sleep the current frame owns.
  This rule already governed the Off path implicitly via a `bool`; it is now
  explicit, unit-tested (`TestVulkanReflexSleepGenerationOwnership`) and
  CI-ratcheted. Getting it wrong regressed Off to ~330 FPS once before (§3.4).
- The **`reflex_sleep_join_us`** metric, which separates the presenting thread's
  join from the worker's driver wait. It is the measurement that produced the
  table above and is what made the null result interpretable rather than
  ambiguous. Compiled out of Shipping with the rest of `VulkanPerf`.
- Mode-neutral naming for the worker (`AsyncSleep*`), since Off is no longer the
  only mode whose policy is expressed against it.

## The DX12 reference: suspected inert, measured healthy

An earlier revision of this document argued that DX12 should not be trusted as
the reference, on the grounds that `src/DX12NvidiaReflex.cpp` had no counterpart
to Vulkan's `QueryTimings()` and therefore no evidence its Reflex was engaged.
That read-back has since been implemented (`NvAPI_D3D_GetLatency`, id
`0x1A587F9C`) and **the suspicion was wrong**. See
`docs/audit/dx12_reflex_latency_verification_2026-08-24.md`.

DX12's Reflex is fully engaged: the driver returns complete frame reports whose
`frameID` matches the logical frame ids the markers carried, with correctly
ordered input/sim/submit/present stamps. It costs nothing because there is
nothing for it to do — `gpuActiveRenderTimeUs` is 33-35 us against a
`gpuFrameTimeUs` of ~1804 us, so the GPU is busy about 2% of the frame, no
render queue accumulates, and the computed sleep is correctly zero.

That makes DX12 a *valid* reference, and it sharpens the Vulkan result rather
than excusing it. Under the same workload and the same GPU load:

| Backend | sleep call, p50 |
|---|---:|
| DX12 Reflex On (`NvAPI_D3D_Sleep`) | **1.2 us** |
| DX12 Reflex On+Boost | **1.2 us** |
| DX12 Off | 1.8 us |
| Vulkan Reflex On (`vkLatencySleepNV`) | **1247 us** |
| Vulkan Off | **1798 us** |

Roughly a thousandfold difference for the same job on the same GPU. The
instruction document's central claim -- that the cost is Vulkan-specific and not
inherent to Reflex -- is therefore **correct**, and this document's earlier
conclusion that the ~1.25 ms was legitimate pacing "working as designed" is
withdrawn.

What the measurement does not support is the document's *explanation* or its
proposed fix. The cost is not a late sleep request joined too eagerly (§6.3), and
it is not reachable by moving the request earlier (§7): it is time spent inside
`vkLatencySleepNV` itself. Note that Vulkan blocks ~1798 us even with
`lowLatencyMode = VK_FALSE`, where no pacing is requested at all -- so the block
tracks the swapchain's frame cadence rather than any latency target. That is
consistent with §3.2's finding that deleting the sleep merely relocated ~1 ms
into `vkAcquireNextImageKHR`: on this driver the low-latency sleep is where
Vulkan's WSI backpressure is absorbed.

Off can hide that behind a whole frame. On/On+Boost cannot, because the wait has
to finish before input. That is the Vulkan-specific defect, it lives in the
driver's `VK_NV_low_latency2` sleep rather than in this repository's scheduling,
and per §21 Phase 6 Case C it belongs to a separate investigation.

## Measurement caveat

The runs in this document used `-Action savestate-load`, which does not fix
window focus. A later focus-controlled re-measurement
(`build/verification/vk-focus-controlled-20260824/`) reproduces the FPS and
sleep figures tightly (Off 534/534/534 with sleep 1798.6/1798.4/1798.8 us; On
348/344/344 with sleep 1234.7/1232.7/1230.1 us), so the conclusions here hold.
Latency-side claims elsewhere in this investigation do not — see
`docs/audit/reflex_investigation_handoff_2026-08-24.md` section 6.

## Reproduction

```
build\verification\reflex-preissue-20260824\run-matrix.ps1 -Repeats 3
build\verification\reflex-preissue-20260824\run-telemetry.ps1
```

Artifacts: `build/verification/reflex-preissue-20260824/{matrix,telemetry}/`.
FPS is parsed from the `window_capture ... title=[NNN/60 ...]` rows in each
run's `.harness.log`; telemetry from the `[VulkanPerf] cpu ... name=...` rows in
each run's `.err.log`.
