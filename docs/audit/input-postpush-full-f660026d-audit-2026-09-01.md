# Input Post-Push Full Audit Closure — f660026d — 2026-09-01

Audited baseline: `f660026de30b97996dc486b82c4255d3ab4f8fac`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_Full_Audit_f660026d_LowCycle_LowOverhead_SRP_HighPerformance_2026-09-01.md`

This ledger records the current source disposition of all five P1 findings,
ten P2 findings, and nine P3 candidates. A local Windows build or static model
does not prove macOS/Linux compilation, device runtime, latency, or remote CI.

## P1 correctness and ownership

| Finding | Disposition | Permanent proof |
|---|---|---|
| P1-001 sub-frame Qt tap loss | Fixed: GUI press events OR fixed hotkey bits into `qtGameplayPressPending`; only a normal guest frame claims it, reentrant frames leave it pending, autorepeat is rejected, and wheel remains Bridge-only | post-f660026d state model; SRP Rule W |
| P1-002 panel reset snapshot race | Fixed: both consumer read and discard paths retry until the reset generation is stable around the cumulative total | interleaving state model; SRP Rule X |
| P1-003 GCMouse/IOHID overlap | Fixed: connect claims GC ownership before handler installation; disconnect removes the handler and drains its serial queue before clearing the queue-local producer gate and ownership bit | source contract; SRP Rule Y |
| P1-004 multi-window surface authority | Fixed by an explicit primary-window policy: only the primary `MainWindow` and its `ScreenPanel` publish/clear focus, capture, panel, center and HWND state or consume cursor requests; state remains per `EmuInstance` | source contract; SRP Rule Z |
| P1-005 macOS move-path locked RMW | Fixed: lost-release recovery loads current masks, computes actually stale bits, and performs correcting RMW only when nonzero | source contract; SRP Rule U |

## P2 hot-path and ownership reductions

| Finding | Disposition |
|---|---|
| P2-001 duplicate process-owner read | Fixed: the result of `PlatformInputOwnerService::Update` is passed into Aim source resolution |
| P2-002 split Linux X/Y totals | Fixed: one lock-free packed `atomic<uint64_t>` is published once per nonzero event and acquired once per frame; arithmetic is modulo 32-bit |
| P2-003 logical controller sampling | Fixed: cold config builds unique physical button/hat/axis sources plus fixed fanout rules; SDL getters run once per unique source and mask assembly remains outside the mutex |
| P2-004 mouse press/release rescans | Fixed: both event directions reuse the five cold-precomputed input/hotkey masks |
| P2-005 repeated stylus publication | Fixed: identical packed stylus coordinates return before release-store |
| P2-006 presentation-driven GUI queue | Fixed: Emu publications advance `m_guiWorkRevision`; steady draw calls return after a revision load and do not attempt CAS/queued invocation. QCursor recenter creates a new revision after its previous pending request is consumed |
| P2-007 split GUI policy loads | Fixed: focused, capture-wanted and panel-available are one changed-only packed GUI policy word |
| P2-008 repeated raw-active resolution | Fixed: `ResolvedAimInput` carries source/raw-active/warp policy from acquisition; UI publication reuses `m_rawAimActiveThisFrame` |
| P2-009 Qt autorepeat RMW/edges | Fixed: MelonPrime key press and release producers reject `isAutoRepeat()` before mask publication (the MainWindow guard remains too) |
| P2-010 recursive joystick locking | Fixed: the public wrapper locks once and both it and `inputLoadConfig` call `setJoystickLocked` |

Rules W-AB in `audit-melonprime-srp-performance.ps1` ratchet these
source-level contracts. `test_input_postpush_full_contract.py` covers sub-frame
tap retention, nested non-consumption, stable reset cases and packed wrap.

## P3 disposition

| Candidate | Disposition |
|---|---|
| P3-001 process controller sampler | Measurement-gated; unchanged until 1/2/4-instance mutex wait and latency profiles justify a process sampler |
| P3-002 frame-count lifecycle cadence | Intentionally retained; a counter is cheaper than a new per-frame clock read unless an existing elapsed-time or SDL dirty signal can be reused |
| P3-003 IOHID metadata cache | Measurement-gated; unchanged pending macOS high-rate callback profiles and lifecycle evidence |
| P3-004 Linux raw state bit packing | Deferred: packed motion removed the larger load/publication cost; availability/session-bit packing needs Linux measurement |
| P3-005 wheel dual publication | Compatibility-retained: Bridge is gameplay authority while the Qt pulse preserves global/non-gameplay hotkeys; domain splitting needs binding compatibility tests |
| P3-006 Qt keyboard cold compile | Human-rate scan retained; event edge publication is fixed-size and allocation-free, so a table is not added without evidence |
| P3-007 Core member layout | Measurement-gated; no padding or layout churn without `offsetof` and cache profile evidence |
| P3-008 sensitivity release timing | Retained as a global command-domain release; it is not a gameplay simulation press edge |
| P3-009 exact platform runtime/CI | NOT RUN / NOT VERIFIED on this host; remains an explicit evidence gap |

## Validation boundary

Local source verification covers the Windows MinGW build and linked tests,
post-f660026d input state model, Savestate input contract, strict GUI/EmuThread
boundary audit, instance-state audit, SRP/performance audit, documentation links,
and diff hygiene.

Still not run on this Windows host:

- macOS compilation and GCMouse/IOHID/QCursor physical handoff;
- Linux compilation and X11/XWayland/Wayland/absolute-tablet runtime;
- physical controller reconnect during nested FrameAdvance;
- 1000 Hz / 8000 Hz input and locked-RMW telemetry;
- two-window and multi-instance physical runtime;
- remote CI for this result.

Windows Raw Input algorithms, Aim Q14/residual arithmetic, generic DS mapping
compatibility, and the visible `RunFrameHook` order were not changed.
