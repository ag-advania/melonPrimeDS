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

## Runtime boundary

The following remain open because they require an interactive real-game
session, a known ROM/savestate, and controlled physical input hardware:

- `cc6726f86` vs `a574c83a` first A/B;
- first-bad commit isolation through `985ca1c8` and the remaining bisect points;
- 1000 Hz and 8000 Hz mouse runs, including controller disconnected/connected
  matrices;
- frame/input p95/p99 capture and subjective smoothness correlation;
- TSan, remote CI, and any claim that current runtime smoothness equals or
  exceeds `cc6726f86`.

No runtime smoothness improvement is inferred from the source cleanup alone.
