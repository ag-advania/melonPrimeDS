# Input Post-Push Re-Audit Closure — f0b0d9e1 — 2026-09-02

Audited base: `f0b0d9e1a8cd44f502e827d2bb7ffe0df1e24161`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_ReAudit_f0b0d9e1_vs_cc6726f86_Smoothness_LowCycle_LowOverhead_SRP_2026-09-02.md`

Smoothness reference: `cc6726f869e7cb96a0e047de04ea8326b142d8f3`

This ledger records the behavior-neutral Phase A cleanup requested by the
re-audit. Static checks and compilation do not prove physical-device
smoothness, latency, mutex contention, or a first-bad runtime commit.

## Finding closure

| Finding | Disposition | Permanent proof |
|---|---|---|
| Windows Raw recovery publication | Retained from f0: successful pure motion and wheel-only events do not request post-frame recovery; stateful events and Raw-read failures remain fail-safe. | `test_input_postpush_full_contract.py`, `raw-recovery-hint-tests`, SRP recovery rules |
| P2-001 Raw/Qt source-selection duplication | Fixed: one normal-snapshot source decision supplies both held and pressed masks. The default `m_qtFallbackGameplayMask == 0` path remains direct Raw; mixed bindings preserve disjoint Raw-owned and Qt-fallback masks. | source contract, SRP CI-03/CI-04 |
| P2-002 `rawActionReady` duplication | Fixed: Raw owner, baseline, and generation readiness is computed once and reused by wheel and gameplay projection. | source count contract, SRP CI-03 |

## Source Definition-of-Done

| Requirement | Evidence |
|---|---|
| f0 recovery classification retained | Full input contract and production Raw recovery test pass |
| default Raw source selection occurs once | `rawOnlyFastPath` source contract and SRP CI-04 pass |
| `rawActionReady` occurs once | SRP CI-03 and full input contract pass |
| no-wheel binding mask reads stay zero | Existing wheel projection contract remains passing |
| `FrameInputState` stays 64B | Existing input layout contract remains passing |
| no new heap/hot polymorphism | Hot-path SRP audit passes |
| RunFrame ordering unchanged | Thread-boundary and ordering audits pass |
| controller one physical sample unchanged | Existing controller ownership/source contract remains passing |

## Validation

| Command | Result |
|---|---|
| `python tools/testing/test_input_postpush_full_contract.py` | PASS |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-srp-performance.ps1` | PASS |
| `cmd /c tools\\build\\windows\\build-mingw.bat --jobs 1 --tail 60` | PASS: Release MinGW build; all 28 invoked targets passed |
| `melonprime_raw_recovery_hint_tests.exe` | PASS |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict` | PASS: 0 findings |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-instance-state.ps1 -Strict` | PASS: 12 documented baseline findings |
| `python tools/ci/audits/audit-low-latency-contract.py` | PASS |
| `python tools/testing/test_mouse_input_savestate_contract.py` | PASS |
| `python tools/maintenance/check-doc-links.py` | PASS: 748 local links |
| `git diff --check` | PASS; only configured LF-to-CRLF notices |
| GitHub Actions Windows run `#1548` / ID `33582155098` | PASS: all workflow steps completed successfully for `39cd0886c0f0095d596fed0a3e65de33adb1c0ee` |

## Runtime evidence (2026-09-02)

A dedicated Release MinGW measurement build was produced from the current
commit `27c875ecd1fd9c4a4e034e492ab2551e3212d939` with renderer and Raw-input
telemetry enabled. The current Vulkan run used the known ROM/savestate fixture,
RTX 5070 Ti, VSync off, frame limit on, Reflex low latency, a 3-second warmup,
and a 10-second measurement window. Process exit, startup savestate marker,
configuration restore, provenance, and native mismatch/fallback checks all
passed; the run selected 790 frame samples.

The same short steady-state condition was run against the smoothness reference
and the audit's suggested checkpoints:

| Run | Selected frames | Frame-time p95/p99 (recorded us) | Input-to-present p95/p99 (us) |
|---|---:|---:|---:|
| `cc6726f86` reference | 790 | 19.05 / 20.01 | 6997.00 / 7813.30 |
| `a574c83a6` | 790 | 19.09 / 19.97 | 6790.00 / 7539.60 |
| `985ca1c8` | 790 | 19.05 / 19.90 | 6631.50 / 7195.60 |
| `27c875ecd` current | 790 | 19.11 / 19.86 | 6800.19 / 7419.61 |

These four short runs show no clear first-bad runtime point or material
steady-state regression. They are not sufficient to claim a smoothness
improvement: the runner's injected key action did not produce physical
`WM_INPUT` hidden-window dispatch, so `hidden_dispatches=0`,
`recovery_scans=0`, and the Raw recovery path was not exercised. The required
physical 1000 Hz/8000 Hz mouse and controller disconnected/connected matrix
remain open. The `0daf3cdf9` checkpoint was also attempted, but its historical
binary crashed with a TOML serialization error before the startup savestate
marker; it is excluded from performance comparison.

## Runtime boundary

The short A/B above is runtime smoke/A-B evidence, but the following remain
open because they require longer controlled runs, an interactive real-game
session, or physical input hardware:

- first-bad commit isolation beyond the short `cc6726f86`/`a574c83a`/`985ca1c8`
  comparison and the remaining bisect points;
- 1000 Hz and 8000 Hz mouse runs, including controller disconnected/connected
  matrices;
- repeated frame/input p95/p99 capture and subjective smoothness correlation;
- TSan and any claim that current runtime smoothness equals or exceeds
  `cc6726f86`.

No runtime smoothness improvement is inferred from the source cleanup alone.
