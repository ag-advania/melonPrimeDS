# Input Post-Push Re-Audit Closure — b2e3c311 — 2026-09-01

Audited baseline: `b2e3c3119d798a381f9dba0cba7e49f53a409652`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_ReAudit_b2e3c311_LowCycle_LowOverhead_SRP_HighPerformance_2026-09-01.md`

This ledger records the source-level disposition of the re-audit. Compilation,
deterministic state models and code-shape audits do not prove physical-device
behavior, cross-platform runtime behavior, data-race freedom under TSan, or a
measured latency/CPU improvement.

## Correctness closure

| Finding | Disposition | Permanent proof |
|---|---|---|
| P1-001 Linux XI2 UNKNOWN event | Fixed fail-closed. `QueryAxisModes` failure returns before valuator decode or accumulation, leaves capability unknown and retries the next event. Relative recovery emits normally; absolute recovery seeds a baseline before differencing. Hierarchy/device invalidation still clears capability and baseline state. | post-push state/source contract; SRP Rule AE |
| P1-002 wheel count collapse | Fixed. Qt angle deltas are accumulated in signed 120-unit detents, the generation-tagged bridge/Raw snapshot preserves signed counts, `FrameInputState::wheelSteps` carries the count, and count-sensitive weapon cycling resolves the final available-set target before calling `SwitchWeapon` once. | wheel count/wrap/Omega/generation/reentrant state model; SRP Rules AF/AG; Windows compile |

## Low-cycle and ownership closure

| Finding | Disposition |
|---|---|
| P2-001 steady wheel-mask loads | Fixed. Qt wheel impulses never enter the held/pending gameplay path, so no-wheel guest frames read `keyHotkeyMask` directly and perform zero wheel-up/down mask loads. Wheel masks are read only when `wheelSteps != 0`. |
| P2-002 duplicate Qt key normalization | Fixed. `MelonPrimeQtKeyBinding.h` is the pure canonical integer projection used by the binding editor, runtime press/release and stylus prime/release paths. Editor-only Escape/Backspace policy remains local. |
| P2-003 running controller command age | Measurement-gated and intentionally unchanged. No second SDL sample or earlier scheduling change was introduced without freshness data. |
| P2-004 native Wayland status | Source implementation is present behind its platform/configuration guards. Runtime acceptance remains pending; it is not described as unimplemented or as fabricated fallback work. |
| P3-001 release-event aggregation | Fixed. Release normalization is computed once; matched DS input bits and hotkey bits are accumulated and each atomic mask is updated at most once. |
| P3-002/P3-003/P3-004 | Measurement-gated and intentionally unchanged. No process-wide controller sampler, Linux raw-state packing change, cache-line padding or memory-order weakening was introduced. |

## Wheel semantic policy

- Weapon Next/Previous is count-sensitive. `+N`/`-N` detents advance the
  available weapon set by N positions, including wrap and Omega restriction,
  then issue one final switch request.
- Emulator commands and other edge actions remain per-frame coalesced bits.
- Qt `angleDelta().y()` uses 120 units per physical detent. Sub-120 values are
  retained in a GUI-thread-owned residual until a complete detent exists.
- Qt pixel-only trackpad deltas are not treated as detents because Qt exposes no
  portable physical-detent conversion for `pixelDelta`.
- Natural-scroll inversion is undone before detent accumulation so binding and
  gameplay direction follow physical wheel direction.

## Preserved contracts

- Raw Input ownership and Qt fallback remain mutually exclusive for gameplay
  wheel counts; the Raw owner drains/discards the Qt mailbox rather than adding
  both sources.
- Reentrant frame projection does not claim wheel or gameplay edge mailboxes.
- Input generation mismatch discards stale wheel counts.
- Global-command, gameplay and level-pulse mailboxes remain separate.
- Controller pending/active program ownership, one normal running sample,
  numeric projection outside `joyMutex`, paused command refresh and reconnect
  baselines are unchanged.
- Primary input surface authority remains the only keyboard, mouse, wheel,
  focus/capture, centre and native-handle publisher.
- Windows Raw Input event/batch/deferred-drain architecture is unchanged except
  for consuming the already-preserved signed wheel count semantically.

## Definition-of-Done disposition

| Requirement | Evidence / boundary |
|---|---|
| query failure emits no delta and remains UNKNOWN | source guard before decode plus fail/recover model |
| absolute success seeds; relative success resumes | lifecycle state model |
| signed `+2`/`-3` reaches semantic consumer | Qt/bridge/frame/weapon source checks and count model |
| final weapon target switches once | cycle function source ratchet and sparse/wrap/Omega model |
| reentrant/generation/Raw-Qt exclusivity | state model and SRP Rule AG |
| high-resolution Qt policy | source comments, residual model and this ledger |
| zero no-wheel wheel-mask loads | source ratchet in post-push contract and SRP Rule Q |
| one Qt key normalizer | source contract and SRP Rule AH |
| release event normalized once/aggregated | source contract and SRP Rule AH |
| no heap/runtime polymorphism added | fixed-size stack/POD helpers and SRP audit |
| existing controller/primary-surface regressions | full post-push contract, thread and instance audits |

## Local validation

The final local validation after implementation produced these results:

| Command | Result |
|---|---|
| `tools\build\windows\build-mingw-existing.bat --jobs 1` | PASS: Release build succeeded; all 20 invoked test executables passed |
| `python tools/testing/test_input_postpush_full_contract.py` | PASS, including Rules AE-AI state/source coverage |
| `python tools/testing/test_mouse_input_savestate_contract.py` | PASS |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-srp-performance.ps1` | PASS |
| `python tools/ci/audits/audit-low-latency-contract.py` | PASS |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict` | PASS: 0 findings |
| `pwsh -NoProfile -File tools/ci/audits/audit-melonprime-instance-state.ps1 -Strict` | PASS: unchanged 12 baseline findings in 4 files |
| `python tools/maintenance/check-doc-links.py` | PASS: 748 local links |
| `git diff --check` | PASS |

## Evidence boundary

The following acceptance evidence needs hardware, another host,
instrumentation, or a remote run and is not represented as completed by the
local checks:

- physical wheel/high-resolution device and trackpad behavior;
- Windows 1000/8000 Hz mouse and simultaneous wheel stress;
- Linux build plus X11/XWayland XIQuery fault injection, source-id reuse,
  VirtualBox absolute pointer and native-Wayland runtime;
- macOS build plus GCMouse/IOHID/fallback runtime;
- 2/4 concurrently active controller instances and measured `joyMutex`
  wait/hold time;
- TSan and physical keyboard/controller lifecycle testing;
- measured p50/p95/p99 input CPU, command age, latency and contention;
- remote CI for this uncommitted working-tree change.

No CPU-cycle, latency, contention or platform-runtime improvement is claimed.
The two new P1 correctness findings and safely adjacent P2/P3 source-contract
items are closed. Hardware/runtime and measurement-gated work remains explicit
acceptance evidence, not an unresolved source implementation claim.
