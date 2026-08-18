# Vulkan Reflex / DX12 difference audit — 2026-08-18

This is the maintained audit record for the Vulkan low-latency work requested by
the untracked instruction document
`.codex/MelonPrimeDS_Vulkan_Reflex_DX12差分監査_修正指示書_develop_hud_baf22d7_2026-08-18.md`.
The instruction document is intentionally not modified or committed.

The audit separates source/build evidence from physical-runtime evidence. A
clean build or a historical smoke log is not promoted to a current hardware
latency result.

## Phase commits

| Phase | Commit | Scope | Current result |
| --- | --- | --- | --- |
| P1 | `eff83775488db640d7eded9a9aaf231f5c00b2ef` | One logical frame ID allocated by `EmuThread` and passed to Vulkan and DX12 Reflex paths | Source audit PASS; Debug Vulkan/telemetry build PASS |
| P2 | `63b4c54270593342245391e66442f8f14b4be044` | Low-overhead presenter-depth gauges and latency-capture frame context | Source audit PASS; normal and capture-enabled Debug builds PASS |
| P3 | This document | Requirement/status ledger and remaining runtime gates | Recorded below; no runtime gate is silently promoted |

The branch is intentionally not pushed by this task. The only remaining
untracked file is the user-provided instruction document.

## P1 source contract

The logical frame ID is owned by the emulation frame boundary. It is incremented
once per emulated frame and passed down through the existing renderer plumbing;
the Vulkan presenter, Vulkan Reflex, DX12 renderer, and DX12 Reflex code do not
allocate presenter-local IDs.

The following relationships are now enforced in source:

- Reflex `INPUT_SAMPLE`, simulation, render-submit, and present markers use the
  logical frame selected by the emulation thread.
- Vulkan `VkLatencySubmissionPresentIdNV.presentID` and the present-side ID use
  the same Reflex frame ID for an accepted frame.
- DX12 and Vulkan Reflex `BeginFrame()` reject zero or non-increasing external
  IDs, which makes accidental duplicate or presenter-callback allocation
  visible instead of silently producing a second ID stream.
- Vulkan marker closing is guarded by the marker-open state. A skipped or
  rejected presentation cannot invent a `PRESENT_END` marker for a present that
  did not occur.
- The generic presenter metadata fallback retains the preferred logical ID.

The existing synchronization exclusions remain intact: there is no
`ImagesInFlight` map, no post-Acquire host fence wait, no steady-state
`vkDeviceWaitIdle`, no fixed one-frame ring, and no busy-poll/sleep loop. The
normal blocking Acquire policy and the bounded low-latency Acquire policy remain
separate and cached.

## P2 telemetry contract

The optional `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` CSV now records the
following frame context in addition to the existing marker and pacing fields:

```text
logical_frame_id
reflex_present_id
present_id
swapchain_image_index
frame_slot
swapchain_image_count
unavailable_swapchain_images
cpu_logical_frames_ahead
presenter_logical_depth
gpu_submitted_frames_ahead
acquire_wait_us
```

`MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` remains off by default and the
capture methods compile to no-ops when the option is disabled. Acquire timing
is sampled only around the actual `vkAcquireNextImageKHR` call.

The always-available performance report adds the corresponding low-cost gauges:

- `VulkanPresenterCpuLogicalAhead`
- `VulkanPresenterGpuSubmittedAhead`
- `VulkanPresenterUnavailableSwapchainImages`

The presenter logical-depth value is calculated from the FrameRing's submitted
and completed frame numbers. It is a presenter/GPU-submit-ahead proxy, not a
direct query of the driver's internal WSI present queue. The unavailable-image
value is a diagnostic observation of distinct successfully acquired image
indices during the current swapchain generation; it is deliberately not an
image fence map and is never used to gate, wait, recycle, or recreate a
swapchain. Consequently, the instruction's literal WSI queue-depth requirement
remains an instrumentation limitation rather than a false PASS claim.

## Static and build evidence

| Check | Result | Evidence |
| --- | --- | --- |
| Low-latency source contract audit | PASS | `py -3 tools/ci/audits/audit-low-latency-contract.py` |
| P1 normal Debug build | PASS | `cmd /c tools\\build\\windows\\build-mingw-existing.bat --build-dir build\\debug-vulkan-telemetry --jobs 1` |
| P1/P2 integrated tests | PASS | Configured Debug Vulkan/telemetry build; Classic On-Screen Edit 82-language geometry and Vulkan timing/lifecycle/timeout/pacer/XeLL tests passed |
| P2 capture-enabled compile/link/test | PASS | `build/debug-vulkan-capture-p2`, Debug, Vulkan + DX12 + renderer telemetry + `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON`, Ninja `--parallel 1` |
| Whitespace check | PASS | `git diff --check` before each phase commit |

The capture-enabled build was configured as a separate ignored build tree so
the normal configured build remained untouched. Its final test set passed all
15 build/test steps, including the 82 registered-language layout cases. The
`-ldwmapi` cache setting was supplied to that fresh Windows configuration to
match the existing configured Debug tree; this was a build-environment detail,
not a source workaround.

## Runtime evidence and limits

The repository already contains physical Windows/NVIDIA Vulkan smoke and A/B
records in:

- [`vulkan_reflex_acquire_budget_2026-08-18.md`](vulkan_reflex_acquire_budget_2026-08-18.md)
- [`vulkan_reflex_acquire_budget_followup_2026-08-18.md`](vulkan_reflex_acquire_budget_followup_2026-08-18.md)
- [`vulkan-low-latency.md`](../development/performance/vulkan-low-latency.md)

Those records demonstrate useful historical behavior: validation-clean
Windows/NVIDIA smoke, 60/240/540 Hz runs on the available monitor modes, VSync
coverage, a two-image experiment, and Custom HUD/scoreboard activity. They were
produced before the P1/P2 commits above and therefore are retained as historical
baseline evidence, not as exact-current-SHA proof for the new frame-context
columns.

The following current-source gates remain explicit:

| Gate | Status | Reason |
| --- | --- | --- |
| Reflex OFF/ON source and unit behavior | PASS | Source audit and configured Debug tests |
| Reflex ON + Boost physical runtime | NOT RUN | No current controlled physical run in this task |
| VSync OFF/ON current-source physical smoke | NOT RUN | Existing records are historical baseline |
| Windowed/fullscreen current-source matrix | OPEN | Existing event runs did not prove the final fullscreen mode in every case |
| 60/240/native-refresh current-source matrix | OPEN | Existing physical records are not from the P1/P2 SHA |
| 1x/4x, HUD OFF/ON, scoreboard current-source matrix | OPEN | Existing records are historical baseline only |
| Real nonzero `VK_TIMEOUT`/`VK_NOT_READY` Acquire skip and recovery | OPEN | Prior controlled 0 ns/high-rate/two-image attempts produced zero driver skips; fake/contract tests cover the branch semantics |
| Validation-layer run from the P1/P2 executable | NOT RUN | The current turn performed source/build/test validation, not a new ROM-driven validation session |
| DX12 vs Vulkan same-scene input-to-present p50/p95/p99 | NOT RUN | Existing DX12 comparison is renderer-health/frame-pacing evidence, not Acquire-equivalent latency parity |
| AMD Anti-Lag 2 physical runtime | NOT RUN | AMD hardware unavailable |
| Linux WSI runtime | NOT RUN | Linux hardware/runtime unavailable |
| Click-to-photon / Reflex Analyzer result | NOT RUN | No external photodiode, camera, or Reflex Analyzer measurement |

The runtime rows are intentionally not changed to PASS by inference from a
successful compile. The runbook remains the authority for a future same-scene
matrix and requires at least p50/p95/p99, frame-pacing variance, skip rate, and
validation markers rather than average FPS alone.

## Defaults and experiments left unchanged

- The default swapchain image policy remains surface-authorized (normally three
  images); `MELONPRIME_VULKAN_SWAPCHAIN_IMAGE_COUNT=2` remains an experiment.
- The bounded low-latency Acquire default remains the existing finite budget;
  zero, 250 us, 500 us, and 1 ms remain A/B controls rather than an automatic
  default change.
- Frames-in-flight is not forced to one.
- No per-image CPU fence or `vkDeviceWaitIdle` was reintroduced.
- The telemetry does not influence present admission or pacing decisions.

## Final assessment

P1 and P2 source/build deliverables are complete and separated into their own
commits. P3 records the implementation and the remaining evidence honestly.
The implementation is not a claim that Vulkan and DX12 have already achieved
equal end-to-end latency on every refresh mode, GPU, WSI, or fullscreen path;
those claims require the physical matrix and external latency measurement listed
above.
