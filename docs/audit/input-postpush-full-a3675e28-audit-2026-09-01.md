# Input Post-Push Full Audit Closure — 2026-09-01

Audited baseline: `a3675e289ddbb4514edee7e04e44b887e18a887b`

Source audit:
`mphCodex/.codex/MelonPrimeDS_Input_PostPush_Full_Audit_a3675e28_LowCycle_LowOverhead_SRP_HighPerformance_2026-09-01.md`

This ledger records the source-level disposition of the five P1 findings, seven
P2 findings, and eight P3 measurement/hardening candidates. It does not claim
macOS/Linux compilation, physical-device runtime, remote CI, or latency gains.

## P1 correctness and ownership

| Finding | Disposition | Permanent proof |
|---|---|---|
| Reentrant controller edge commit | Fixed: nested FrameAdvance refreshes held state but passes `commitGameplayEdges=false`; it neither advances `previousLateJoystickHotkeyMask` nor consumes the reconnect baseline | post-push state-model test; SRP Rule Q |
| Panel aim reset multiwriter | Fixed: GUI alone publishes reset boundary+generation; EmuThread alone writes cursor+seen generation | SRP Rule S; thread-boundary audit |
| Controller derived-state race | Fixed: `closeJoystick` owns only physical handles/capabilities and publishes `joystickGameplayResetPending`; EmuThread alone resets/updates gameplay masks | SRP Rule P; thread-boundary audit |
| macOS availability lost update | Fixed: `BackendGc`/`BackendHid` share one atomic bitset updated with `fetch_or`/`fetch_and` | SRP Rule R; post-push source contract |
| Early Qt gameplay press | Fixed: normal `UpdateInputStateImpl` owns a guest-frame-late Qt baseline; reentrant updates never commit it; wheel bits are excluded and projected only from the generation mailbox | SRP Rule Q; post-push state-model test |

## P2 late-path cost

| Finding | Disposition |
|---|---|
| RTC/shader work after Input Sample | Moved before the limiter; late critical sequence is input sample, controller refresh, `RunFrameHook`, `SetKeyMask`, `RunFrame` |
| macOS lost-release mapping scans | Five button input/hotkey masks are rebuilt on config load and combined with fixed work in the event path |
| Linux repeated first-motion atomic load | Filter-thread `receivedMotionPublished` gates the one release publication |
| Non-Windows Aim source reread | Acquisition caches `m_warpCursorAfterAimThisFrame`; Aim consumes the scalar |
| Repeated GUI level stores | panel availability, packed center, and native window handle skip unchanged stores |
| Repeated input-generation bridge call | Core publishes only when its cached generation changes |
| QCursor recenter locked OR | Recenter uses a dedicated SPSC level mailbox; other GUI commands retain their bitset |

Rules T-U-V in `audit-melonprime-srp-performance.ps1` pin frame ordering,
fixed-mask release recovery, and changed-only publications.

## P3 disposition

| Candidate | Disposition |
|---|---|
| Frame-count controller lifecycle cadence | Intentionally retained: the counter has lower steady overhead than adding a per-frame clock read; late running detach remains per guest frame. Change requires reconnect-latency evidence or an SDL dirty event |
| IOHID worker exits after initial GCMouse | Fixed: the cold worker waits while GC is active and opens IOHID after the last GC mouse disconnects |
| IOHID element metadata lookup | Measurement-gated; unchanged pending a macOS trackpad profile because registration caches add API lifecycle complexity |
| GUI keyboard `HK_MAX` scan | Intentionally retained on human-rate press/release events; the high-rate mouse-move scan was removed |
| Unconsumed late joystick release state | Removed after repository-wide reference search found writes but no reader; global emulator release remains owned by `inputProcess` |
| Standard DS input mask in MelonPrime build | Retained: removal crosses keyboard/controller/menu compatibility and needs all-platform runtime evidence |
| ThreadBridge cacheline grouping | Measurement-gated; padding is not added without contention evidence because it increases every instance footprint |
| Exact remote CI/platform runtime | Not locally satisfiable; remains separately NOT RUN / NOT VERIFIED until matching platform jobs/hardware tests exist |

## Validation boundary

Local source verification covers the Windows MinGW build and linked tests,
strict GUI/Emu ownership audit, instance-state audit, SRP/performance audit,
post-push input state model, Savestate input contract, and diff hygiene.

Still not run on this Windows host:

- macOS GCMouse, IOHID, QCursor and GC-to-trackpad transition;
- Linux X11/XWayland/Wayland relative and absolute-pointer paths;
- physical controller reconnect during nested frame advance;
- 1000 Hz / 8000 Hz input soak and before/after latency profiling;
- multi-instance physical-input runtime;
- remote CI for this result.

Those are runtime/platform evidence gaps, not source findings silently reported
as passed. No Windows Raw Input algorithm, Aim fixed-point arithmetic,
`RunFrameHook` internal order, or generic action bus was changed.
