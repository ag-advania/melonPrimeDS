# Input Post-Push Audit Closure — ca5c1d24 — 2026-09-01

Audited baseline: `ca5c1d24ee725092d889b609d5b03004d370c9b9`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_Audit_ca5c1d24_LowCycle_LowOverhead_SRP_HighPerformance_2026-09-01.md`

This ledger records the source-level disposition of the audit. Compilation,
deterministic state models and code-shape audits do not prove physical-device
behavior, cross-platform runtime behavior, data-race freedom under TSan, or a
measured latency/CPU improvement.

## P1 finding closure

| Finding | Disposition | Permanent proof |
|---|---|---|
| P1-001 Qt key normalization and mapping domains | Fixed: MelonPrime press paths use `getEventKeyVal`; DS keypad projection reads `keyMapping`, while emulator/gameplay hotkeys read `hkKeyMapping`. Release uses the same normalized identity and also clears a modified non-modifier binding by physical base key when modifiers were released first. Left/right modifier encoding, autorepeat rejection, focus neutralization and stylus-hotkey routing remain explicit. | post-push source contract; SRP Rule AD |
| P1-002 global Qt sub-poll tap loss | Fixed: global emulator command presses and guest-frame gameplay presses have separate atomic mailboxes. `inputProcess` claims the global mailbox once; normal guest-frame projection claims the gameplay mailbox once; reentrant projection does not claim it. | mailbox state model; SRP Rules W/AD |
| P1-003 sticky Linux XI2 query failure | Fixed: `QueryAxisModes` reports failure without setting `known`; the next raw source event retries. A fail-then-success model pins recovery to absolute-axis mode. | XI2 recovery state model; SRP Rule AD |
| P1-004 controller program/device publication | Fixed at the source ownership boundary: config compiles a local fixed-capacity program, publishes pending under `joyMutex`, and release-publishes a generation. EmuThread activates a private immutable copy under the mutex before sampling and performs numeric fanout outside it. `joystickPresent` is release/acquire published; every SDL handle dereference remains mutex guarded. The obsolete unlocked analogue helper was removed. | publication model and source contract; SRP Rule AD; Windows compile |

## Related hardening and deferred measurement

| Finding | Disposition |
|---|---|
| P2-001 Linux capability lifecycle | Fixed in source: subscribe to `XI_HierarchyChanged` and `XI_DeviceChanged`; cold events clear source capability/baseline cache. Linux runtime was not available locally. |
| P2-002 wheel impulse versus held level | Fixed: wheel no longer mutates `keyHotkeyMask`. Global command edges, one-cycle non-gameplay down state, and gameplay wheel generation are separate mailboxes/routes. Mouse-button held state retains all hotkey domains, including Guitar Grip. |
| P2-003 global-only gameplay publication | Fixed: keyboard and mouse button producers mask global-command and gameplay bits before publishing to their respective mailbox. Accessory bits remain held-level only. |
| P2-004 running controller command age | Intentionally unchanged. Moving command sampling earlier or adding another sample requires latency measurement and must not violate the one-running-sample contract. |
| P2-005 config reload neutral boundary | Hardened: `inputLoadConfig` neutralizes old Qt levels/edges before publishing the new mapping/program generation. Physical controller state is rebaselined by the existing generation activation/reset lifecycle. |
| P2-006 process-global SDL sampling | Measurement only. No new coordinator or shared sampler was introduced without 2/4-instance contention evidence. |
| P2-007 native Wayland relative input | Deferred platform work. X11/XWayland XI2 source correctness was hardened; no unsupported native Wayland backend was invented. |
| P2-008 surface authority/input generation unification | Deferred architectural change. Existing ownership and generation contracts remain pinned. |
| P3 items | No speculative micro-optimization was applied. Existing changed-only atomics, Windows Raw Input and macOS input paths remain unchanged except for shared Qt normalization call sites. |

## Definition-of-Done disposition

| Requirement | Evidence / boundary |
|---|---|
| normalized modifier/right-mod identity and DS/hotkey domains | source contract and Windows compile pass; physical Windows/macOS/Linux keyboard matrix not run |
| global Qt short tap exactly once | deterministic separate-mailbox model passes; interactive runtime not run |
| XIQuery transient recovery and lifecycle invalidation | fail/recover plus lifecycle model and source audit pass; Linux runtime/fault injection not run |
| no shared controller binding-program race by construction | pending/active generation ownership model and Rule AD pass; TSan/config stress not run |
| no plain joystick-presence pointer read in MP hot paths | atomic presence source contract passes; handle use remains inside `joyMutex` |
| no new allocation/config lookup/expanded projection lock in hot path | fixed POD tables/mailboxes and SRP audit pass; no cycle or contention measurement claimed |
| one normal running physical controller sample | existing one-sample source/model contract remains passing |
| input sample ordering before `RunFrameHook` | low-latency contract audit remains passing |
| focus/config reset is neutral | `keyReleaseAll` clears held and pending Qt state before reload; source contract passes |

## Local validation

The final local validation after implementation produced these results:

| Command | Result |
|---|---|
| `tools\build\windows\build-mingw-existing.bat --jobs 1 --tail 100` | PASS: Release build succeeded; all 20 invoked test executables passed |
| `python tools/testing/test_input_postpush_full_contract.py` | PASS |
| `python tools/testing/test_mouse_input_savestate_contract.py` | PASS |
| `pwsh -File tools/ci/audits/audit-melonprime-srp-performance.ps1` | PASS |
| `python tools/ci/audits/audit-low-latency-contract.py` | PASS |
| `pwsh -File tools/ci/audits/audit-melonprime-thread-boundary.ps1 -Strict` | PASS: 0 findings |
| `pwsh -File tools/ci/audits/audit-melonprime-instance-state.ps1 -Strict` | PASS: 12 baseline findings in 4 files |
| `python tools/maintenance/check-doc-links.py` | PASS: 748 local links |
| `git diff --check` | PASS |

The following acceptance evidence needs hardware, another host, instrumentation,
or a remote run and is not represented as completed by the local checks:

- physical modifier/right-mod keyboard testing on Windows, Linux and macOS;
- physical controller hotplug, config-reload race stress and TSan;
- 2/4 concurrently active instances with measured `joyMutex` wait/hold time;
- Windows 1000/8000 Hz mouse and multi-window runtime;
- Linux build and X11/XWayland/absolute-pointer/native-Wayland runtime;
- macOS build and GCMouse/IOHID runtime;
- measured p50/p95/p99 input-section CPU, latency and contention deltas;
- remote CI for this uncommitted working-tree change.

No CPU-cycle, latency or contention improvement is claimed. The source-level P1
ownership/correctness gaps and the safely adjacent P2 domain/lifecycle gaps are
closed; measurement-gated and platform-runtime items remain explicitly outside
the local proof boundary.
