# Vulkan retire-frame post-implementation re-audit — 2026-08-19

This record covers
`.codex/MelonPrimeDS_Vulkan_production-retire-mapping再監査_追加修正・CI検証指示_2026-08-19.md`.
The source baseline is `5efbad93c76501e6025a5f729689a91ce8a2b60f`
(`fix: correct Vulkan OOM retirement frame tagging`). The implementation
commits are `e774e054b` (`fix: preserve Vulkan last successful submission
number`) and `f4c84c9e3` (`test: cover one-slot Vulkan retirement failure
states`). The CI reachability adjustment is `179e2d110`
(`ci: run Vulkan retire check before macOS packaging`). Ubuntu target-step
isolation is `d454c1446` (`ci: always run Ubuntu Vulkan retire check`), and BSD
target-step isolation is `8b7ad7f59` (`ci: isolate BSD Vulkan retire check`).
Environment setup timeouts are `dbce40fde` (`ci: bound Ubuntu and BSD
environment setup`).
The instruction file is intentionally not modified or staged.

The production application used for the physical evidence was built from
`1a71113c8725da30e77e3eb990d316bc28552b54` (`docs: record exact-head Vulkan
retire mapping evidence`). The physical runner records that source head in
every artifact; it does not change production renderer code.

The latest hosted verification dispatch for this audit was run at exact
source/workflow SHA `dbce40fde913353701371720d38f4fa8fcaf507b` (`ci: bound
Ubuntu and BSD environment setup`). The report commits after that dispatch are
documentation only; the production renderer source used for the physical
evidence is unchanged.

A supplemental BSD-only rerun at documentation-only successor SHA
`b8dc9648c` (run [`32229161939`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32229161939))
reproduced the FreeBSD VM-retry failure and encountered NetBSD VM setup failure;
OpenBSD again reached and passed its retire target. The primary `dbce40fde`
run remains the exact matrix evidence for NetBSD's PASS; the rerun demonstrates
that the BSD VM failures are not a source-test assertion failure.

The FreeBSD retry log records the AnyVM action timing out at 20 minutes, retry
setup ending with `sudo` exit 100, and the later target shell reporting
`freebsd: command not found` because the VM was never established.

A final documentation-only exact-head re-dispatch at `7b253bd0279c4627d47ce771d61cefc2bb4d5e8f`
produced Windows run [`32231255914`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231255914),
Ubuntu run [`32231258079`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231258079),
BSD run [`32231260692`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231260692),
and macOS run [`32231263380`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231263380).
Windows completed PASS, including its production retire target. Ubuntu Audits
and the aarch64 retire target passed; x86_64 dependency installation timed out
against the hosted apt mirror after 20 minutes, so its target could not build,
and the artifact aggregator then failed because the expected artifacts were not
created. BSD VM startup/retry failed externally for NetBSD, FreeBSD, and
OpenBSD before their targets. macOS reached PASS for both retire targets, while
both build jobs failed later in the existing Classic On-Screen Edit geometry
test. These results supersede neither the prior exact `dbce40fde` target PASS
evidence nor the separately documented FreeBSD VM gap; they are the final
revision's hosted status.

The result separates source/build/model evidence, hosted CI evidence, and
physical GPU evidence. A successful local build or static audit is not
promoted to a cross-platform driver or latency result.

## Result summary

| Gate | Status | Evidence |
| --- | --- | --- |
| Existing P2 Vulkan retire-frame and bounded OOM contract | PASS | The production `FrameRing` policy selects current-recording, last-submitted, or completed frame as appropriate; Vulkan texcache retirement and the bounded fake-OOM retry model remain intact. |
| P3 independent last-successful-submit semantics | PASS by source/build/model | `FrameRing` stores `LastSubmittedFrameNumber` independently from the one-slot `FrameContext::SubmittedFrame`, resets it on create/destroy, and updates it only after successful `EndCommandBuffer` and `QueueSubmit`. |
| P3 production `FrameRing` one-slot mapping direct test | PASS by source/build/model | `VulkanFrameRingTestAccess` seeds first-recording, same-slot recording reuse, and same-slot submit-failure states without a Vulkan device, while the test calls production `FrameRing::GetResourceRetireFrame()`, `GetLastSubmittedFrameNumber()`, and `GetCurrentRecordingFrameNumber()`. |
| Telemetry/layout configuration parity | PASS | The frame-retire test target now receives the same `MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY` definition as `core`, preventing a measurement-build class-layout/ODR mismatch. Both telemetry-ON and telemetry-OFF Windows builds pass the direct test. |
| Windows/Ubuntu/BSD/macOS CI target wiring | PASS by workflow/YAML audit | Each workflow explicitly invokes `melonprime_vulkan_frame_retire_check`; workflow YAML parses successfully. |
| Hosted Windows CI at `dbce40fd` | PASS | Run [`32227415394`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32227415394) checked out the exact source/workflow SHA and completed successfully. The production retire target, Vulkan-disabled build, release-binary harness absence check, and artifact upload all passed. |
| Hosted macOS CI at `dbce40fd` | TARGET PASS / JOB FAILURE | Run [`32227422312`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32227422312) ran the dedicated retire step successfully on both x86_64 and arm64. Both jobs later failed in the pre-existing Classic On-Screen Edit geometry check, unrelated to this Vulkan change. |
| Hosted Ubuntu CI at `dbce40fd` | TARGET PASS / JOB FAILURE | Run [`32227417529`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32227417529) had the Audits job PASS and the independent retire target PASS on both x86_64 and aarch64. Both full Builds failed in the existing non-retire build surface, so the workflow job remained red. |
| Hosted BSD CI at `dbce40fd` | TARGET PARTIAL / JOB FAILURE | Run [`32227419672`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32227419672) had NetBSD and OpenBSD loader/presenter/retire targets PASS. FreeBSD's VM retry failed at the bounded setup step, so its retire target could not run; the workflow remained red. Supplemental retry [`32229161939`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32229161939) reproduced the FreeBSD failure and had a separate NetBSD VM setup failure while OpenBSD's target passed again. |
| Final exact-head Windows CI at `7b253bd0` | PASS | Run [`32231255914`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231255914) completed the build job and production Vulkan retire target successfully. |
| Final exact-head Ubuntu CI at `7b253bd0` | TARGET PARTIAL / HOST FAILURE | Run [`32231258079`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231258079) had Audits PASS and the aarch64 retire target PASS. x86_64 dependency installation timed out after 20 minutes against the apt mirror; its independent retire step then reported no build directory. The artifact aggregator failed only because those artifacts did not exist. |
| Final exact-head BSD CI at `7b253bd0` | TARGET UNAVAILABLE / VM FAILURE | Run [`32231260692`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231260692) could not establish NetBSD, FreeBSD, or OpenBSD VMs: startup timed out, retries failed in `sudo`, and the fallback target shells reported `<os>: command not found`. No BSD source assertion or retire target result was obtained on this final revision. |
| Final exact-head macOS CI at `7b253bd0` | TARGET PASS / JOB FAILURE | Run [`32231263380`](https://github.com/ag-advania/melonPrimeDS/actions/runs/32231263380) passed the dedicated retire target on x86_64 and arm64. Both build jobs later failed in the existing Classic On-Screen Edit geometry test, including narrow-window/large-font cases; this is unrelated to the Vulkan retire mapping. |
| Physical measurement runner and artifact restoration | PASS | `tools/testing/renderer-physical-ab.ps1` passed PowerShell parsing; it records renderer/scale/VSync/low-latency/HUD/action seed, restores config and layer settings byte-for-byte, requires the state marker, and rejects device/VUID/SYNC failure markers. |
| Physical Vulkan validation-layer lifecycle | PASS | Current-head Debug build completed resize x40, minimize/restore x20, and fullscreen x8 with 69 swapchain rebuilds, device lost 0, Sync hazards 0, clean validation, and config/layer restoration PASS. |
| Physical DX12/Vulkan/OpenGL Compute A/B and hardware acceptance | OPEN / PARTIAL | Vulkan and DX12 completed three-seed warmed action-all baselines plus scale/pacing representatives. OpenGL Compute passed steady-state and individual actions but reproducibly crashed on the Reset action (`0xC0000005`), so no cross-backend hardware acceptance is claimed. |

## Implementation

`FrameRing::BuildResourceRetireFrameState()` is the single production
extraction point used by `GetResourceRetireFrame()`. The model test has a
test-only friend access seam in the test translation unit; it does not add a
shipping API or production symbol. The direct assertions therefore exercise
the production mapping rather than only duplicating its pure policy helper.

The one-slot ring now keeps `LastSubmittedFrameNumber` independently from the
slot's mutable `SubmittedFrame`. `BeginFrameInternal()` changes the slot's
recording number but does not change the last-successful-submit number;
`SubmitFrame()` updates the independent number only after both command-buffer
end and queue submission succeed. The test seam mirrors production's
`AbsoluteFrame` rule: current recording frame, otherwise last successful frame
plus one, otherwise frame one.

The test target uses
`melonprime_apply_renderer_perf_telemetry_definition()` so the `FrameRing`
layout seen by the test matches the linked `core` library in measurement
builds. This was required because the first telemetry-enabled build exposed
the mismatch: the source compiled, but the injected test state did not reach
the production fields. After the CMake correction, both configured Windows
variants pass.

The following workflow steps now explicitly exercise the target:

- `.github/workflows/build-windows.yml`
- `.github/workflows/build-ubuntu.yml`
- `.github/workflows/build-bsd.yml`
- `.github/workflows/build-macos.yml`

The physical runner is intentionally an explicit-renderer, Windows-only
diagnostic harness. `-ActionSeed` applies a reproducible Fisher-Yates order to
the eight action scenarios when `-Action all` is selected; `-GraceSeconds`
allows final renderer telemetry to flush before shutdown. HUD OFF runs require
the developer `customHudForcedOff=1` marker in addition to the savestate marker.
The harness demonstrates injected actions and runtime safety markers; it does
not claim that every game-side action produced a particular gameplay result.

## Validation evidence

| Check | Status | Evidence |
| --- | --- | --- |
| Production late-fence/retire contract audit | PASS | `python tools/ci/audits/audit-explicit-renderer-late-fence.py` |
| Independent last-submit and one-slot failure-state audit | PASS | The explicit late-fence audit checks the independent member, create/destroy resets, successful-submit-only update ordering, production seam formula, and first-recording/same-slot/submit-failure test cases. |
| GPU memory production-overhead audit | PASS | `python tools/ci/audits/audit-renderer-memory-production-overhead.py` |
| Renderer telemetry zero-overhead audit | PASS | `python tools/ci/audits/audit-renderer-perf-zero-overhead.py` |
| Structured composition contract audit | PASS | `python tools/ci/audits/audit-structured-composition-contract.py` |
| Software raster parity audit | PASS | `python tools/ci/audits/audit-raster-software-parity.py` |
| Low-latency contract audit | PASS | `python tools/ci/audits/audit-low-latency-contract.py` |
| Linux Vulkan presenter-retire audit | PASS | `python tools/ci/audits/audit-vulkan-linux-presenter-retire-contract.py` |
| Workflow YAML syntax | PASS | Python `yaml.safe_load` over all four modified workflows |
| Whitespace/error check | PASS | `git diff --check`; only expected LF/CRLF conversion warnings |
| Measurement Windows build | PASS | `build/rebuild-mingw-x86_64`; developer features, renderer telemetry, and Vulkan latency capture enabled; full build and `vulkan-frame-retire-tests: PASS` |
| Shipping Windows build | PASS | `build/release-mingw-shipping-x86_64`; developer features and renderer telemetry disabled; full build and `vulkan-frame-retire-tests: PASS` |
| Physical runner parser and whitespace check | PASS | PowerShell AST parser and `git diff --check -- tools/testing/renderer-physical-ab.ps1` |
| Physical measurement build | PASS | `build/rebuild-mingw-x86_64`; Vulkan/DX12/OpenGL Compute runs used the NVIDIA GeForce RTX 5070 Ti, driver 610.74, with process exit/config restore/state marker/bad-marker results retained per run. |
| Physical validation-layer run | PASS | `build/physical-ab-20260819/vk-20260819-current-all-sync.{out,err}.log`; validation banner confirmed Sync Validation, hazards 0, device lost 0, and validation clean. |

The source/model/build rows remain distinct from the physical rows. The
physical rows are Windows/NVIDIA evidence only and do not establish hosted
cross-platform coverage or AMD/Intel vendor behavior.

## Physical GPU evidence

The fixed input was the Metroid Prime: Hunters USA Rev 1 ROM with matching
`ml1` savestate, using the same `build/rebuild-mingw-x86_64/melonPrimeDS.exe`
measurement binary. Artifacts are retained under
`build/physical-ab-20260819/`; the instruction file and ROM/save/state files
were not modified.

| Backend | Warmed randomized action-all baseline | Scale/pacing representatives | Result |
| --- | --- | --- | --- |
| Vulkan | Seeds 11, 29, 47; scale 4; VSync OFF; low-latency OFF; all three exited 0 with state marker 1 and bad markers 0 | Scale 1/OFF, 4/ON, and 16/OFF all exited 0; Reflex run was active and clean | PASS for the exercised NVIDIA cases |
| DX12 | Seeds 11, 29, 47; scale 4; VSync OFF; low-latency OFF; all three exited 0 with state marker 1 and bad markers 0 | Scale 1/OFF, 4/ON, and 16/OFF all exited 0; Reflex and ReflexBoost were active and clean | PASS for the exercised NVIDIA cases |
| OpenGL Compute | Seed 11 action-all reached all logged actions but exited `0xC0000005`; Reset alone reproduced the same crash | Scale 1/OFF, 4/ON, and 16/OFF steady-state runs exited 0; individual non-Reset actions exited 0 | OPEN: Reset crash prevents full action-all acceptance |

For the three-seed baseline's final four one-second telemetry windows, the
frame-time summaries were:

| Backend | p50 (ms) | p95 (ms) | p99 (ms) | max (ms) |
| --- | ---: | ---: | ---: | ---: |
| Vulkan | 16.678 | 18.553 | 19.561 | 19.891 |
| DX12 | 16.581 | 18.498 | 19.004 | 19.703 |

Representative scale-4 renderer telemetry aggregated over the same four
windows (median of per-window p50/p95/p99/max, microseconds) was:

| Backend | raster prepare | reuse wait | record/submit | soft2d total | structured pack | present wait |
| --- | --- | --- | --- | --- | --- | --- |
| Vulkan | 41.150/81.450/92.610/93.750 | 1.550/3.150/3.870/4.050 | 44.150/85.000/97.680/102.900 | 2749.950/4193.350/5858.430/6318.750 | 77.800/133.700/146.440/152.500 | acquire 2.200/4.000/5.310/5.850; slot 9.225/16.950/21.930/23.900 |
| DX12 | 40.600/55.980/82.895/97.850 | n/a | n/a | 2754.725/4076.120/5126.265/5542.200 | n/a | slot 0.900/1.205/1.730/2.100 |

The Vulkan first action window also recorded `texture_materialize_count=9`,
`texture_materialize_pre_fence_fail_count=0`,
`texture_materialize_retry_after_retire_count=0`,
`texture_materialize_retry_success_count=0`, and
`texture_materialize_retry_fail_count=0`, with 15 pending-storage growth
events. DX12's corresponding first action window recorded materialize count 9
and all four materialization failure/retry counters at zero. OpenGL Compute has
no corresponding detailed Vulkan/DX12 renderer telemetry stream in the current
source; its generic frame and input-to-present telemetry was captured instead.

The Vulkan action-all seed-11 run produced a non-empty display capture at
`build/physical-ab-20260819/20260819_current_Vulkan_state1_baseline_seed11.display.png`.
The HUD OFF smoke also passed with `custom_hud_off_marker=1`.

Low-latency capability evidence is vendor-qualified: NVIDIA Reflex was active
for Vulkan (`VK_NV_low_latency2`) and DX12 (`vendor_pacing_authority=1`, modes
On and On+Boost). On this NVIDIA adapter Anti-Lag 2 reported unsupported as
AMD-only, and XeLL reported unsupported as requiring a supported Intel Arc GPU;
those runs exited cleanly but are not vendor PASS results.

The Debug validation run is separate from latency measurement. Its exact
artifacts are `vk-20260819-current-all-sync.out.log` and
`vk-20260819-current-all-sync.err.log`: the validation banner confirmed core
and Synchronization Validation, `sync hazards=0`, `device lost=0`, and
`validation=clean` after resize/minimize/fullscreen cycling.

### OpenGL Compute Reset triage

The Reset-only physical run remains a real open crash, not a harness timeout:
the measurement build exited with Windows exception `0xC0000005`, and the
Windows Application Error record identifies `melonPrimeDS.exe`. Repeating the
same action with the Debug executable reproduced `0xC0000005`; the debug PE
fault address `0x140b802b4` resolves with `addr2line` to
`MainWindow::makeCurrentGL()` at `src/frontend/qt_sdl/Window.cpp:1434`.
The corresponding instruction reads `hasOGL` from `this+0x79` with an invalid
`RCX`, so the failure is a stale/invalid `MainWindow` object dereference
rather than a Vulkan retire assertion.

The GDB run separately confirmed the Reset call path
`EmuThread::handleMessages()` -> `EmuInstance::reset()` and showed a valid
`mainWindow`, `hasOGL`, and `panel` on the first post-Reset
`makeCurrentGL()` call. Breakpoints on `MainWindow::closeEvent()`,
`MainWindow::~MainWindow()`, and `EmuInstance::deleteWindow()` did not fire
during the instrumented Reset sequence. This narrows the issue to a timing-
dependent lifetime or memory-corruption path but does not prove which one;
therefore no speculative null-check or unrelated OpenGL change is claimed as
the fix. The investigation configuration was restored after the run.

## Remaining acceptance gates

The following remain explicitly open:

- Retain the final exact-head hosted status above. Windows and macOS reached
  their retire targets; Ubuntu aarch64 reached its target while x86_64 was
  blocked by the hosted apt mirror timeout; and the final BSD dispatch could
  not establish any VM. The earlier exact `dbce40fde` run remains the source /
  workflow evidence for Ubuntu x86_64, Ubuntu aarch64, NetBSD, and OpenBSD
  target PASS; FreeBSD remains unavailable in both the primary and retry
  evidence.
- The reproducible OpenGL Compute Reset-path `0xC0000005` is separately
  triaged above but remains unresolved. It is not part of the Vulkan
  retire-frame patch and is not promoted to cross-backend acceptance.
- Obtain AMD/Intel hardware for Anti-Lag 2/XeLL coverage; the current NVIDIA
  host can only provide explicit unsupported capability evidence.
- Retain visual parity, no-crash/device-reset/savestate checks, validation
  layer lifetime errors of zero, and capture evidence for future physical runs.

Accordingly, the requested production test hardening is implemented, the
Windows/NVIDIA physical evidence is now reproducible and partially complete,
and the validation-layer lifecycle is clean. OpenGL Reset remains an
explicitly triaged but unresolved crash, the BSD VM coverage remains
environment-blocked on the final dispatch, and non-NVIDIA vendor coverage is
unavailable; none is promoted to PASS.
