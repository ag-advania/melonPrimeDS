# Input Post-Push Performance Audit — 2026-08-31

Baseline: `876d1eef84a2e753ea126f8350ea6935058b2354`

This ledger resolves the findings in the post-push audit without changing the
fixed-point Aim algorithm, `FrameInputState`, Windows batched Raw Input,
specialized native delivery, or `RunFrameHook` ordering.

## Finding disposition

| Finding | Source disposition | Evidence | Runtime status |
|---|---|---|---|
| P1-001 macOS raw per-frame recenter | Fixed: capture-wanted-only request removed; source-resolved warp policy remains | Rule L2 bans `CaptureWantedForEmu` in `ProcessAimInputMouse` and requires `PlatformInput_ShouldWarpCursorAfterAim` | macOS GCMouse/IOHID/QCursor hardware matrix NOT RUN |
| P1-002 Linux unconditional `absBaseInvalid.exchange` | Fixed: relaxed load gates the exchange claim | Rule L2 verifies load precedes exchange | Linux X11 hardware matrix NOT RUN |
| P1-003 Linux accumulator `fetch_add` | Fixed: sole filter writer uses relaxed load plus release store | writer grep, Rule L2 fetch-add ban and load/store requirement | Linux 125/1000/8000 Hz NOT RUN |
| P2-001 repeated `receivedMotion` store | Fixed: release store occurs only after false-edge load | Rule L2 verifies load-before-store | Linux runtime NOT RUN |
| P2-002 disabled overlay guest read | Fixed: both-disabled return precedes pointer address/read | Rule L2 orders gate before `hookLocalPlayerPtrGlobal` | Windows Release build passed; gameplay toggle smoke NOT RUN |
| P2-003 shared overlay baseline owner | Fixed: renamed `m_postPollOverlayLocalPlayerPtr`; coordinator reset is unique | Rule L2 rejects feature reset ownership and legacy name | static ownership PASS |
| P2-004 config reload exchange | Fixed: relaxed false load before rare exchange | Rule L2; producer racing false remains pending for next frame | live reload smoke NOT RUN |
| P2-005 cursor command exchange | Fixed: `-1` sentinel load before rare exchange | Rule L2; replacement command may consume next frame | cursor-mode transition matrix NOT RUN |
| P2-006 wheel mailbox exchange | Fixed after invariant audit: zero alone is empty; generation-only values remain nonzero claims | Rule L2 plus mailbox comments and existing generation contract test | wheel race/burst hardware NOT RUN |
| P2-007 Windows wheel `HK_MAX` scan | Fixed: input config projects up/down masks used by raw and Qt wheel paths | Rule L2 rejects the hot scan | MinGW compile PASS; wheel binding smoke NOT RUN |
| P2-008 Linux source-id hash lookup | Measurement-gated, unchanged | instruction requires evidence before cache/array work | OPEN pending Linux profiling |
| P3 bridge packing / Core physical split | Intentionally unchanged | no profiling evidence; logical SRP and locality retained | N/A |

## Concurrency and memory-order proof

- Linux `accX/accY` producer: the XInput filter thread running `ThreadMain` is
  the only writer. `fetchMouseDelta` and `resetAll` only acquire-load values and
  advance per-subscription cursors. A relaxed writer load followed by a release
  store therefore cannot lose another writer's update.
- `absBaseInvalid` producers (`resetAll`, `NotifyCursorWarp`) release-store true.
  RawMotion's relaxed load is only a cheap hint; a true observation still uses
  `exchange(false, acq_rel)`, so no load/store clear race is introduced.
- config and cursor commands coalesce to the latest value. A GUI publication
  after an emulation-thread empty load survives and is consumed on the next
  normal frame; it is never overwritten by the consumer.
- the wheel mailbox is packed as generation in the upper 32 bits and signed
  steps in the lower 32 bits. `0` alone is empty. A generation-only value is
  nonzero, reaches exchange, and preserves the registration boundary.

## Hot-path cost delta

| Cost | Delta |
|---|---:|
| heap allocation | +0 |
| virtual / `std::function` dispatch | +0 |
| mutex | +0 |
| atomic objects | +0 |
| steady Linux RawMotion locked RMW | -1 `absBaseInvalid.exchange`; -1 per changed accumulator axis |
| repeated Linux motion release store | removed after first motion |
| disabled post-poll guest reads | -1 pointer read per call when both features are off |
| steady config/cursor/wheel exchange | removed on empty sentinel |
| wheel `HK_MAX` scans | removed from both raw and Qt pulse paths |
| guest writes / `FrameInputState` copies | +0 |
| platform syscalls | +0; macOS raw frame recenter request removed |

No before/after performance improvement is claimed without clean-provenance
platform measurements.

## Validation

Completed locally on Windows:

```text
tools/build/windows/build-mingw-existing.bat --jobs 1: PASS (77/77)
audit-melonprime-srp-performance.ps1: PASS (includes Rule L2)
renderer-physical-ab.ps1 Software + savestate-load: PASS
  process exit 0; startup/action markers 1/1; 1016 frame rows;
  bad markers 0; config/layer restore PASS
```

The existing configured build compiled the current dirty sources, but its
embedded build SHA came from its earlier configure step. It proves compilation
and linked tests, not clean-source binary provenance. The Software smoke used
`-AllowUnverifiedBinary` for the same reason; it proves a bounded Windows launch,
savestate reconciliation and clean exit, not input-device behavior or a clean
performance measurement.

Still required for hardware acceptance:

- macOS GCMouse, IOHID and QCursor fallback warp counts and transitions;
- Linux X11 relative mouse at 125/1000/8000 Hz, absolute-pointer recovery,
  XWayland/Wayland fallback and multi-instance;
- Windows wheel binding/burst and overlay toggle gameplay smoke;
- clean before/after latency and CPU measurements.

GitHub Actions for the uncommitted implementation is not applicable and was not
run. The source-id cache remains deliberately open until Linux profiling shows
the `unordered_map` lookup is material.
