# Vulkan Reflex / DX12 difference audit — 2026-08-18

This is the maintained audit record for the Vulkan low-latency work requested by
the untracked instruction document
`.codex/MelonPrimeDS_Vulkan_Reflex_DX12差分_実装後再監査_追加修正指示書_develop_hud_45fed5d_2026-08-18.md`.
The instruction document is intentionally not modified or committed.

The audit separates source/build evidence from physical-runtime evidence. A
clean build or a historical smoke log is not promoted to a current hardware
latency result.

## Phase commits

| Phase | Commit | Scope | Current result |
| --- | --- | --- | --- |
| P1 | `d8697c091` | Query and select NVIDIA low-latency optimized Vulkan present modes while preserving VSync policy | Source audit PASS; Debug fake-dispatch tests PASS |
| P2 | `e11788514` | Gate present admission using effective active Reflex/Anti-Lag authority | Source audit PASS; Debug authority matrix PASS |
| P3 | `0e97dc948` | Make latency-capture and performance telemetry names/semantics explicit | Source audit PASS; Debug telemetry tests PASS |
| P4 | `09102be0b` | Treat Reflex sleep-wait timeout/failure as runtime failure and close the frame | Source audit PASS; Debug timeout tests PASS |
| P5 | This commit | Final static audit, Debug capture build, Release/RelWithDebInfo builds, and evidence update | PASS for source/build gates; physical runtime remains OPEN/NOT RUN |

Foundational work retained in the history includes the logical frame-ID
plumbing (`eff83775488db640d7eded9a9aaf231f5c00b2ef`), the original telemetry
surface (`63b4c54270593342245391e66442f8f14b4be044`), and the initial audit
record (`45fed5dad4fe8f21a5bbf694e664b481ec181d73`). The phase commits above
were intentionally kept separate at the user's request. The branch is not
pushed by this task; the user-provided instruction document remains the only
untracked file.

## P1 — NVIDIA low-latency optimized present modes

When `VK_NV_low_latency2` is enabled, swapchain recreation queries
`VkLatencySurfaceCapabilitiesNV` through the
`VkSurfaceCapabilities2KHR::pNext` chain. The query is a two-pass count/array
query, accepts `VK_INCOMPLETE`, and is performed at swapchain recreation rather
than once per frame.

The selected mode is the intersection of the driver-reported optimized modes
and the surface's available modes. The selector preserves the existing VSync
policy: VSync ON remains FIFO-safe and does not force tearing; a latest-ready
FIFO mode is only accepted when the existing policy/capability gate permits it.
VSync OFF is restricted to IMMEDIATE/MAILBOX semantics and does not select FIFO
merely because the optimized list contains it. If there is no valid optimized
intersection, the generic present-mode policy remains in force.

The presenter emits the following low-cost selection telemetry:

- `present_mode_selected`
- `nv_low_latency_optimized_mode_count`
- `present_mode_is_nv_low_latency_optimized`

The fake-dispatch tests cover the two-pass query, `VK_INCOMPLETE`, intersection,
VSync ON/OFF filtering, and generic fallback paths.

## P2 — effective low-latency authority

The configured-only `GPU_Vulkan::ShouldBypassPresentWait()` path was removed.
Present admission now uses the effective presenter authority
`Reflex.IsActive() || AntiLag.IsActive()`, exposed through a constexpr policy
helper and a presenter method. This distinguishes a requested setting from an
extension/device/runtime state that is actually active. The hot path does not
introduce a mutex for this decision.

The timing tests cover configuration OFF/effective OFF, requested-but-
unsupported extension/effective OFF, active Reflex, runtime-failure fallback,
and active Anti-Lag authority.

## P3 — telemetry semantics

The optional `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` CSV now uses explicit
names:

```text
logical_frame_id
reflex_present_id
present_id
swapchain_image_index
frame_slot
swapchain_image_count
distinct_swapchain_images_acquired_since_recreate
logical_frames_since_last_accepted_present
unretired_frame_ring_submission_depth
acquire_wait_us
```

`distinct_swapchain_images_acquired_since_recreate` is a diagnostic count of
distinct successfully acquired image indices in the current swapchain
generation. It is not an `ImagesInFlight` map and never gates, waits, recycles,
or recreates a swapchain.

`logical_frames_since_last_accepted_present` measures logical emulation frames
since the last accepted present. `unretired_frame_ring_submission_depth` is
calculated from FrameRing submitted/completed frame numbers and is explicitly a
FrameRing retirement proxy, not the driver's internal WSI queue depth. The
former duplicated `gpu_submitted_frames_ahead`/`presenter_logical_depth`
representation was removed in favor of this single semantic counter.

The always-available performance report uses the matching counters:

- `VulkanPresenterDistinctSwapchainImagesAcquiredSinceRecreate`
- `VulkanPresenterLogicalFramesSinceLastAcceptedPresent`
- `VulkanPresenterUnretiredFrameRingSubmissionDepth`

Capture remains optional and compiles to no-ops when disabled. Acquire timing is
sampled only around the actual `vkAcquireNextImageKHR` call.

## P4 — Reflex sleep-wait failure handling

`VulkanNvidiaReflex::BeginFrame()` retains the one-second
`vkWaitSemaphoresKHR` watchdog. A result of `VK_SUCCESS` continues the frame;
every other result, including `VK_TIMEOUT` and `VK_NOT_READY`, disables Reflex
for the runtime failure, marks the frame as presented/closed, and returns
without entering a busy loop. The timeout/failure classifier is directly tested
for success, timeout, and not-ready results.

## P5 — static and build evidence

| Check | Result | Evidence |
| --- | --- | --- |
| Low-latency source contract audit | PASS | `python tools/ci/audits/audit-low-latency-contract.py` |
| Phase Debug gates | PASS | Phase target sets completed 28/28, 25/25, 24/24, and 18/18 during P1–P4 |
| Current capture-enabled Debug build | PASS | `build/debug-vulkan-capture-p2`, Vulkan + DX12 + renderer telemetry + `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE=ON`; 32/32 steps passed |
| Current Release build | PASS | `build/telemetry-on-release`, Vulkan + DX12 + renderer telemetry; 80/80 steps passed and build script reported success |
| Current RelWithDebInfo build | PASS | `build/vulkan-draw-crash-relwithdebinfo`, Vulkan + DX12; 272/272 steps passed and build script reported success |
| Whitespace check | PASS | `git diff --check` before the Phase 5 commit |

The capture-enabled Debug configuration was kept in a separate ignored build
tree. Its test set included the 82 registered-language Classic On-Screen Edit
geometry cases, Vulkan timing/lifecycle/timeout/pacer tests, the presenter
direct-surface tests, and the XeLL fake API state-machine tests. The fresh
Windows configuration used `-ldwmapi` to match the existing configured Debug
tree; this was a build-environment detail, not a source workaround.

The Release build emitted transient MSYS2 `sh.exe` shared-library messages
during LTO, but Ninja completed all 80 steps and the build wrapper reported
success. The RelWithDebInfo build regenerated its existing configuration and
completed all 272 steps, including its test executables.

## Runtime evidence and limits

The repository contains historical physical Windows/NVIDIA Vulkan smoke and A/B
records in:

- [`vulkan_reflex_acquire_budget_2026-08-18.md`](vulkan_reflex_acquire_budget_2026-08-18.md)
- [`vulkan_reflex_acquire_budget_followup_2026-08-18.md`](vulkan_reflex_acquire_budget_followup_2026-08-18.md)
- [`vulkan-low-latency.md`](../development/performance/vulkan-low-latency.md)

Those records are retained as historical baseline evidence. They predate the
P1–P4 commits and are not exact-current-SHA proof for optimized-mode selection,
authority, telemetry semantics, or Reflex timeout recovery. No ROM/state file
was available in the repository/workspace scope used for this task, so a new
physical Vulkan/DX12 run was not started.

| Gate | Status | Reason |
| --- | --- | --- |
| P1 optimized present-mode physical selection | NOT RUN | No current controlled physical run with a ROM/state and suitable NVIDIA Vulkan device |
| Reflex OFF/ON source and unit behavior | PASS | Static audit and configured Debug/Release/RelWithDebInfo tests |
| Reflex ON + Boost physical runtime | NOT RUN | No current controlled physical run in this task |
| VSync OFF/ON current-source physical smoke | NOT RUN | Existing records are historical baseline |
| Windowed/fullscreen current-source matrix | OPEN | No exact-current-SHA physical matrix was run |
| 60/240/native-refresh current-source matrix | OPEN | Existing physical records are not from the P1–P4 SHA |
| 1x/4x, HUD OFF/ON, scoreboard current-source matrix | OPEN | Existing records are historical baseline only |
| Real physical `VK_TIMEOUT`/`VK_NOT_READY` Reflex wait recovery | NOT RUN | Contract/fake tests cover the branch; no controlled physical timeout run was performed |
| Validation-layer run from the current executable | NOT RUN | This task performed source/build/test validation, not a new ROM-driven validation session |
| DX12 vs Vulkan same-scene input-to-present p50/p95/p99 | NOT RUN | Existing DX12 comparison is not Acquire-equivalent latency parity at this SHA |
| AMD Anti-Lag 2 physical runtime | NOT RUN | AMD hardware unavailable |
| Linux WSI runtime | NOT RUN | Linux hardware/runtime unavailable |
| GitHub CI status for the current SHA | NOT CHECKED | CI was not queried in this task |
| Click-to-photon / Reflex Analyzer result | NOT RUN | No external photodiode, camera, or Reflex Analyzer measurement |

These rows are intentionally not promoted to PASS by inference from a
successful compile. A future same-scene runbook remains responsible for p50,
p95, p99, frame-pacing variance, skip rate, and validation markers rather than
average FPS alone.

## Preserved synchronization and policy constraints

- No `ImagesInFlight` map or per-image host fence was introduced.
- Acquire does not perform a post-Acquire host-fence wait.
- No steady-state `vkDeviceWaitIdle`, fixed one-frame ring, or busy polling was
  introduced.
- Frames-in-flight is not forced to one, and the surface-authorized swapchain
  image policy remains in force; two-image mode remains an experiment.
- The bounded low-latency Acquire budget remains a finite policy control rather
  than an unbounded `UINT64_MAX` wait.
- Telemetry does not influence present admission or pacing decisions.

## Final assessment

P1 through P4 implementation deliverables are complete in separate commits, and
P5 records the final static/build validation in its own documentation commit.
Source-level behavior and configured test coverage are PASS. Physical Vulkan and
DX12 latency parity, refresh-rate behavior, WSI behavior, and external
click-to-photon equivalence remain OPEN/NOT RUN as recorded above; the audit does
not claim those results without matching hardware evidence.

## Post-audit remediation: DX12 XeLL FinishFrame marker semantics

The follow-up review identified and closed the P3 marker-semantics finding. The
production `DX12IntelXeLL::FinishFrame()` now performs cleanup only: it closes an
already-open Present, Render Submit, or Simulation span and resets frame state.
It does not synthesize `InputSample`, `PresentStart`, or `PresentEnd` for a frame
that never reached the corresponding production phase.

The Fake API state-machine coverage now includes:

- normal Present bracketing, with one `PresentStart` and one `PresentEnd`;
- a frame that reaches Render Submit but no actual Present, with no Present markers;
- `BeginFrame()` followed immediately by cleanup, with no input or Present marker;
- cleanup after `PresentStart`, proving that the existing open span closes once
  without a duplicate marker pair.

The low-latency source contract audit asserts that `FinishFrame()` cannot call
`MarkPresentStart()`, `MarkInputSample()`, or `EndRenderPhase()` and that the
regression cases remain present in the Fake API test. Physical Intel Arc XeLL
runtime validation and exact-current-SHA external latency measurements remain
OPEN/NOT RUN; this remediation closes the source/build P3 only.
