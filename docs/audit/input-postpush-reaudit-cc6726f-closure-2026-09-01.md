# Input Post-Push Re-Audit Closure — cc6726f — 2026-09-01

Audited baseline: `cc6726f869e7cb96a0e047de04ea8326b142d8f3`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_ReAudit_cc6726f_LowCycle_LowOverhead_SRP_HighPerformance_2026-09-01.md`

This ledger records the source disposition of the one new P1, two P2 findings,
one measurement-gated P3, and the re-audit Definition of Done. Compile, static
models and code-shape audits do not prove physical-device behavior, latency,
mutex contention, another operating system, or remote CI.

## Finding closure

| Finding | Disposition | Permanent proof |
|---|---|---|
| P1-001 controller commands freeze while paused | Fixed: physical acquisition, global-command projection and gameplay projection are separate helpers. A running late sample feeds both consumers once; a paused outer cycle samples and updates command state only. Controller release/re-press therefore reaches Pause edge detection without modifying the gameplay previous mask, baseline or Qt edge mailbox. Disconnect/reconnect resets both consumers and establishes a no-phantom baseline. | post-cc6726f state model; SRP Rule AC |
| P2-001 packed GUI policy read repeatedly | Fixed: `ReadGuiInputPolicyForEmu` performs one acquire and returns `GuiInputPolicySnapshot`; normal and reentrant input resolve pass this immutable snapshot to focus/capture/panel decisions. The three individual Emu accessors were removed. | source contract; SRP Rule AC |
| P2-002 fixed controller scratch full zeroing | Fixed: the physical snapshot scratch has no value initializer. The sampler writes `[0, sourceCount)`, every fanout index is asserted and release-guarded below `sourceCount`, and button/hat/axis equivalence is covered by a state model. | mapping equivalence model; SRP Rules AA/AC |
| P3-001 lifecycle cadence duplicates SDL update | Closed by scheduling shape without a performance claim: active running devices are attachment-checked by the required late physical sample, so the cadence path no longer performs another update in that outer cycle. The lifecycle helper is an absent-device probe only. Absent-device probes remain throttled in both scheduling states; paused active devices receive one command refresh per low-rate outer cycle. A successful paused reconnect probe may perform the exceptional probe plus first sample, and a Pause command that resumes in the same outer cycle is followed by the resumed guest frame's required late sample. | source contract; SRP Rule AC |

## Definition-of-Done disposition

| Requirement | Evidence / boundary |
|---|---|
| controller-only Pause, release, re-press, resume | deterministic command/gameplay state model passes; physical controller runtime not run |
| paused release/re-press is live | paused command projection model and scheduling source contract pass |
| paused refresh does not consume gameplay state | model proves previous gameplay mask unchanged; Rule AC bans gameplay baseline/mailbox access from command refresh |
| running physical sample is normally once per guest frame | one late call reaches the sole sampler; active lifecycle check reuses attachment result; Rule AC passes |
| getters scale with unique source count | cold unique-source compile plus mapping equivalence model and Rule AA pass |
| no maximum scratch zeroing | source contract and Rule AC pass |
| one GUI policy acquire per decision | snapshot API contains one acquire; split accessors absent; Rule AC passes |
| nested FrameAdvance edge contract | existing model plus Rule Q remain passing |
| reconnect has no phantom edge | command and gameplay baseline model passes |
| Qt sub-frame tap retained | existing event mailbox model plus Rule W remain passing |
| high-rate raw path has no normal locked RMW | prior closure Rule U remains passing; no raw path changed here |
| two-window authority retained | prior closure Rule Z remains passing; no surface authority code changed here |
| audit contract updated | state models and SRP Rule AC added |

## Validation boundary

The final local validation after implementation produced these results:

| Command | Result |
|---|---|
| `tools\\build\\windows\\build-mingw-existing.bat --jobs 1` | PASS: Release build succeeded; all 20 invoked test executables passed |
| `python tools/testing/test_input_postpush_full_contract.py` | PASS |
| `python tools/testing/test_mouse_input_savestate_contract.py` | PASS |
| `pwsh -File tools/ci/audits/audit-melonprime-srp-performance.ps1` | PASS |
| `pwsh -File tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict` | PASS: 0 findings |
| `pwsh -File tools/ci/audits/audit-melonprime-instance-state.ps1 -Strict` | PASS: 12 baseline findings in 4 files |
| `python tools/maintenance/check-doc-links.py` | PASS: 748 local links |
| `git diff --check` | PASS |

The following evidence requires hardware, another host, or a remote run and is
not represented as completed by local static checks:

- physical controller Pause/reconnect/held-through-resume and keyboard mixing;
- 2/4 concurrently active instances with measured `joyMutex` wait/hold time;
- Windows 1000/8000 Hz mouse and multi-window runtime;
- Linux build plus X11/XWayland/Wayland/controller runtime;
- macOS build plus GCMouse/IOHID/QCursor handoff runtime;
- remote CI results for this exact change were not available during local validation.

No measured CPU, latency or contention improvement is claimed. The P3 change is
reported only as a verified scheduling/code-shape reduction.
