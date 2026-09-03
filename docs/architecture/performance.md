# MelonPrime Performance Rules

Use this as the default performance model for MelonPrime gameplay and HUD work.

## Update Cadence

Separate game-frame state updates from presentation-frame drawing:
- gameplay-derived state should update once per emulated game frame
- repeated 120/240Hz draws of the same `NDS::NumFrames` should consume cached state
- debounce timers, short transitions, random-effect phases, and aim/HUD smoothing should not advance per presentation frame unless the feature is explicitly visual-only

Prefer this ownership split:
- `Update...ForGameFrame(..., nds->NumFrames)` reads RAM and advances gameplay timers
- `Draw...()` consumes cached values and performs only drawing work
- caches are keyed by `NDS*`, `MainRAM`, `NDS::NumFrames`, player offset, and ROM group when those can affect the read result

## Hot-Path Shape

Keep common cases cheap:
- branch early for disabled features and common false states
- test the cheapest reliable byte/flag before reading wider or dependent structures
- avoid reading weapon/player substructures until an outer state proves they are needed
- cache stable facts by pointer or frame number instead of re-reading them per draw

Keep math out of inner loops:
- precompute percentages, fixed-point scales, colors with alpha, bboxes, and stamps when config changes
- move expensive painter setup, image generation, dilation, text rasterization, and lookup-table construction out of per-frame paths
- use dirty rects and actual drawn bboxes so unchanged pixels are not cleared, composited, or uploaded

## Input / Aim Latency Path

The input→`RunFrame` path is latency-bound, not throughput-bound. Keep it short, and
keep work that only the *next* frame can observe off it:
- do not issue a syscall on the pre-frame path when a later post-present site can do the
  same work. Stuck-key recovery (`clearStuck*`) and message draining run after `RunFrame` +
  `drawScreen` (`DeferredDrain` / `clearStuckPostFrame`), because their effect is only ever
  read by the next snapshot.
- Hidden-window Raw recovery is classified at decode time: successful pure motion and
  wheel-only messages do not publish the post-frame scan, while stateful events and
  Raw-read failures retain the fail-safe request.
- On Windows, a normal input snapshot computes Raw readiness once and makes one
  Raw/Qt source decision for both held and pressed gameplay state. The default
  Raw-only profile stays on the direct path; mixed bindings merge disjoint
  Raw-owned and Qt-fallback masks.
- gate optional per-frame syscalls on a cheap flag instead of running them unconditionally.
  LateLatch re-drains the raw-input buffer only when a `FrameAdvance` opened a wide window
  (`m_didFrameAdvanceSinceSnapshot`); normal frames skip the `GetRawInputBuffer` call.
- early-out the aim path on zero work: a zero mouse delta with zero residual returns before
  any IMUL / clamp / output.

Respect the Single-Writer atomic discipline (one writer thread per Raw Input mode):
- prefer relaxed load + release store over locked RMW (`fetch_or` / `exchange` / CAS).
- load a press/edge slot relaxed first and pay the lock-prefixed op only when it is nonzero;
  consumer-thread-only slots can use plain load + `store(0)`.

See [input/aim-input.md](input/aim-input.md) and
[../archive/investigations/input/click-handling.md](../archive/investigations/input/click-handling.md) for the full pipeline and the
P-44 / P-47 / P-48 rationale.

## Invalidations

Every cache needs a clear invalidation owner:
- config change invalidates config-derived caches
- ROM/player/weapon pointer change invalidates RAM-derived capability caches
- death, pause, third-person, transform, emulator stop, and HUD disabled paths must reset visual state that would otherwise replay later
- frame caches naturally expire when `NDS::NumFrames` changes

## Current Examples

- Custom HUD zoom amount, crosshair DS aim position, and `s_chDisplayZoom` are game-frame keyed.
- Custom HUD runtime state caches base/adventure/visible reads within a game
  frame, lazily caches optional element values such as ammo, owned weapons,
  bombs, match status, rank, and time only when those elements are drawn, and
  reuses the same frame cache for scoreboard and Enemy Target snapshots.
- `CustomHudConfigState` constructs its typed battle/frame/text owner slots once on the cold
  path; editor fields remain directly owned by the config state until a concrete editor owner is
  introduced. `HudFrameState()` and `HudBattleState()` only dereference those owners during
  rendering.
- `CustomHud_Render()` consumes the Screen-owned, config-epoch-coherent HUD enable snapshot;
  HUD enable interpretation stays outside the steady-state render path. ARM9 hook activation
  likewise consumes the resolved `Arm9HookActivationPlan`.
- Zoom status uses scope bit + cached CanZoom and intentionally avoids zoom FOV / HUD animation reads on the hot path.
- Aim sensitivity stores an effective scale and keeps floating-point percentage math out of `ProcessAimInputMouse()`.
- `ProcessAimInputMouse()` skips IMUL / clamp / output on zero-delta frames and returns immediately when delta and residuals are both zero (P-44).
- LateLatch's `GetRawInputBuffer` syscall is gated on `m_didFrameAdvanceSinceSnapshot`, so normal in-game frames skip it (P-47).
- `clearStuck*` and message draining run post-present in `DeferredDrain` / `clearStuckPostFrame`, off the input→`RunFrame` latency path (P-48b).

## Invalidation Ledger (V5 Phase 5)

Every row must have an explicit owner. Adding a cache without a ledger row is a review blocker.

| Cache | Location | Invalidation trigger | Owner |
|---|---|---|---|
| `CachedHudConfig` / `CustomHud_GetCacheEpoch()` | `MelonPrimeHudRenderConfig.inc` | Config reload, TOML import, edit-mode snapshot apply | `CustomHud_RefreshConfigIfNeeded` |
| `m_hudCfgEpoch` / `m_hudEnabled` | `Screen.h` + `MelonPrimeHudScreenOverlay.h` | `Metroid.Visual.CustomHUD` change | `MelonPrimeHud_RefreshHudEnabledIfNeeded` |
| `m_hudFontEpoch` / `overlayFont` | `Screen.h` + `MelonPrimeHudScreenOverlay.h` | HUD font property change | `MelonPrimeHud_RefreshOverlayFontIfNeeded` |
| `m_radarCfgEpoch` / radar GL fields | `Screen.h` + `MelonPrimeHudScreenOverlay.h` | Radar property change | `MelonPrimeHud_RefreshRadarConfigIfNeeded` |
| `m_hudTopMatrix` / layout scale | `Screen.h` + `MelonPrimeHudScreenIntegration.cpp` | `setupScreenLayout()` | `ScreenPanel::updateHudScreenLayoutCache` |
| `m_radarAnchorDsX/Y` | `Screen.h` + `MelonPrimeHudScreenOverlay.h` | Radar config epoch refresh | `MelonPrimeHud_RefreshRadarConfigIfNeeded` |
| Dirty rect / `s_drawnDirtyPx` | `MelonPrimeHudRenderAssets.inc` | Top of each `CustomHud_Render` | `CustomHud_Render` |
| OPT-DR3 upload hash | `MelonPrimeHudScreenCppOverlayOfGl.inc:36-40` | Resize / full reupload / content change | GL overlay path (same file) |
| `s_zoomAimCanZoomCache` | `MelonPrimeGameInput.cpp:51` | ROM detect / layout / scope state edge | `UpdateZoomAimEffectiveScale` |
| `m_aimEffectiveFixedScale*` | `MelonPrime.h` | Sensitivity / zoom-aim config reload | `RecalcAimFixedPoint` / zoom update |
| `m_aimResidualX/Y` | `MelonPrime.h` | Sensitivity, layout, aim block, focus (not `InputReset`) | `HandleAimEarlyReset`, explicit lifecycle |
| `m_cachedPanel` (P-3) | `MelonPrime.h` | `OnEmuStart`, `NotifyLayoutChange` | `MelonPrimeCore` lifecycle |
| Qt panel `m_panelAimTotal` / reset baseline / consumer cursor | `MelonPrimeThreadBridge.h` | reset captures the current total; panel→raw and layout/focus transitions request reset | GUI-thread producer + emulation-thread consumer |
| SDL active binding table / late edge baseline | `EmuInstance.h` | input config load, central device close, reconnect baseline | `EmuInstance` input owner |
| Linux raw axis baseline / residuals | `MelonPrimeRawInputLinuxFilter.cpp` | reset eventfd readiness at a bounded (64-event maximum) filter-loop boundary, device hierarchy invalidation | `LinuxRawInputFilter` |
| Linux raw availability + motion-seen | `MelonPrimeRawInputLinuxFilter.cpp` packed state byte | filter startup/first delivered motion, teardown | `LinuxRawInputFilter` |
| Mac raw `lastReadX/Y` | `MelonPrimeRawInputMacFilter.mm:46-47` | `resetAll`, filter stop | `MacRawInputFilter::resetAll` |
| Linux raw `lastReadX/Y` | `MelonPrimeRawInputLinuxFilter.cpp:54-55` | `resetAll` | `LinuxRawInputFilter::resetAll` |
| Win raw mouse snapshot | `MelonPrimeRawInputState.cpp:283-289` | `discardDeltas`, `resetAll` | `InputState::fetchMouseDelta` |
| `m_platformRawAimWasActive` | `MelonPrime.h:594` | N/A (edge detector); cleared on emu stop via filter reset | `UpdateInputStateImpl` |
| `BattleMatchState` | `MelonPrimeHudBattleOwnedState.inc` (nested under `MelonPrimeHudRuntimeSample.inc`) | Match join/leave, config epoch | `CustomHud_OnMatchJoin` / `CustomHud_ResetPatchState` |
| Typed HUD owner slots | `MelonPrimeHudRenderConfig.inc` / `MelonPrimeHudRender.cpp` | Config-state construction/destruction | `CustomHudConfigState` constructor/destructor |
| `Arm9HookActivationPlan` | `MelonPrime.h` / `MelonPrimeRuntimeConfig.cpp` | Runtime config reload and core snapshot apply | `LoadRuntimeConfigSnapshot` / `ApplyRuntimeConfigSnapshot` |
| Text/icon/radar-frame caches | `MelonPrimeHudRenderAssets.inc` | Signature change (size/color/text) | Per-cache prepare helpers |
| `shadersReady` | `MelonPrimeEmuThreadFrameState.inc` | `videoSettingsDirty` / renderer switch | `EmuThread.cpp` limiter block |

## Per-Frame Syscall Budget (V5 Phase 7 ratchet)

Review must not increase steady-state per-frame syscalls without Phase 0 before/after numbers.

| Platform | Path | Budget (post-V5 Phase 2) | Notes |
|---|---|---|---|
| Windows | Raw input | `GetRawInputBuffer` via Poll + DeferredDrain; LateLatch gated (P-47) | Unchanged do-not-touch path |
| Windows | Warp | `ClipCursor` on clip setup only | No per-frame SetCursorPos in aim |
| macOS | Raw aim active | **0** CGWarp per aim frame | Threshold warp in `Screen.cpp` >96px only |
| macOS | QCursor fallback | CGWarp per aim frame (Accessibility path) | Intentional |
| Linux raw | Aim | **0** XWarp per aim frame | Threshold containment >96px |
| Linux panel | Aim | 0 warps; atomic panel delta | Edge reset on panel→raw |
| All (dev) | Perf probe | 0 in release; gated in dev | S22 verified |

Increasing any row requires `MELONPRIME_PERF=1` before/after attached to the PR.

### Software presenter lock measurement

`EmuInstance::renderLock` is the renderer/NDS lifetime fence. The GUI-thread
Software presenter takes it before copying borrowed `RendererOutput` CPU
pointers; renderer and console replacement take it on their cold transition
paths. The established Software path still holds it through `QPainter` game
composition and Custom HUD composition. Do not narrow or remove it from source
shape alone.

Developer builds with `MELONPRIME_PERF=1` emit a one-second
`native_paint_us` window from `ScreenPanelNative`. It reports sample count,
p50, p95, p99, and max for `render_lock_wait`, `render_lock_hold`,
`framebuffer_copy`, `qpaint_game`, and `hud_software`. This collector is
panel-owned (multi-instance safe), uses fixed storage, and compiles to inline
no-ops when developer features are disabled. A lock-scope change requires
before/after output from the same Software/HUD/layout workload plus a renderer
transition smoke test; the measurement hook itself is not evidence that the
scope can safely shrink.

The 2026-09-04 Windows F7 (`.ml7`) audit used the same steady-state savestate,
4x scale, VSync/frame limit enabled, low-latency mode disabled, 5 s warmup,
12 s measurement, and 2 s grace for every renderer/HUD pair. The comparison
binary was compiled independently from a clean detached source worktree at
`7728669f9e8f9e241c809135bf04dff5422b1c78`; both binaries lack usable embedded
build-info JSON, so these results are an unverified local regression screen,
not release provenance or a controlled benchmark. The table reports each
run's shutdown frame histogram, including warmup and grace, under identical
conditions.

| Renderer / HUD | Detached HEAD p95 / p99 (ms) | Audited tree p95 / p99 (ms) | Audited minus HEAD (ms) |
|---|---:|---:|---:|
| Software / ON | 17.033 / 17.137 | 17.022 / 17.128 | -0.011 / -0.009 |
| Software / OFF | 16.890 / 17.036 | 16.887 / 17.004 | -0.003 / -0.032 |
| OpenGL / ON | 16.921 / 17.096 | 16.937 / 17.150 | +0.016 / +0.054 |
| OpenGL / OFF | 17.035 / 17.295 | 17.046 / 17.321 | +0.011 / +0.026 |
| Vulkan / ON | 16.907 / 17.655 | 16.888 / 17.820 | -0.019 / +0.165 |
| Vulkan / OFF | 16.968 / 17.613 | 16.960 / 17.573 | -0.008 / -0.040 |
| DX12 / ON | 16.962 / 17.663 | 16.954 / 17.441 | -0.008 / -0.222 |
| DX12 / OFF | 17.062 / 17.601 | 17.057 / 17.781 | -0.005 / +0.180 |

The audited Software path's median one-second-window p99 was 0.1 us for
`render_lock_wait` with either HUD state (worst window p99: 0.2 us ON, 0.1 us
OFF). `render_lock_hold` p99 was 727.0 us ON versus 32.2 us OFF, while
`hud_software` p99 was 696.7 us ON versus 1.7 us OFF. The wait cost is therefore
negligible in this workload and the hold delta is dominated by Custom HUD
composition. Keep the lifetime-fence scope intact unless a repeatable workload
shows material wait contention; optimize the measured HUD work instead of
weakening renderer/NDS pointer lifetime protection.

### GUI input and renderer-transition follow-up (2026-09-04)

The Custom HUD editor now has a GUI-owned `m_hudEditInputActive` latch. It is
updated before the early-return checks in `setHudEditModeActive()`, and the
ordinary `mouseMoveEvent()` path calls `handleHudMouseMove()` only under an
`Q_UNLIKELY` check of that latch. The helper still validates
`CustomHud_IsEditMode()` before its edit-only Config lookup, so a stale edge
cannot enter the editor. No atomic, mutex, allocation, or queued invocation was
added to the event path.

Developer builds with `MELONPRIME_PERF=1` also emit one aggregate
`screen_input` record per second from the panel-owned fixed collector. The
record contains `mouseMoveEvents`, `eventSamples`,
`eventDroppedOrOverwritten`, `eventHistogramSaturated`, `event_ns`
p50/p95/p99/max, and the
`hudEditFastRejected`, `hudEditHelperEntered`, `uiSnapshotRead`, and
`stylusPointerPublish` counters. Percentiles come from a fixed 4096-bucket,
one-microsecond histogram, so the one-Hz report does not sort a large sample
array inside a GUI event. The histogram covers the full report window;
`eventDroppedOrOverwritten` remains zero, while the saturated-tail count makes
out-of-range durations explicit. The Windows physical A/B harness can enable
the deterministic synthetic workload with `-Action steady-state
-SyntheticMouseRateHz 1000` or `8000`; its `.screen-input.json` artifact
records attempted and successfully submitted `SendInput` reports. This is a
repeatable OS-injection workload, not a claim about a physical 1k/8k device.

The Vulkan VBlank observer/capture path was removed after a repository-wide
read-site check found no consumer for its `frameTop`/`frameBottom` state. The
authoritative presentation source is `AcquireRendererOutputLease()` in
`drawScreenFrame()`, including the complete Software fallback and native
Vulkan-buffer paths. The process-global Vulkan panel registry remains only for
constructor/destructor publication and the cold renderer-transition snapshot;
no VBlank callback acquires its mutex or publishes a redundant frame pointer.

Top-screen touch keeps the exhaustive parity target in `ALL`: on the Windows
MinGW build used for this audit, the current 138,240-configuration executable
completed in 347 ms (the incremental CMake target completed in 325 ms). That
is below the materiality threshold used for this repository, so no fast/smoke
split was introduced and the full coverage remains the default build gate.
The explicit old/new mapping benchmark is:

```text
cmake --build build/release-mingw-x86_64 --target melonprime_top_screen_touch_benchmark
```

It reports `ns / map` and `maps / second` for fixed 1x/4x/16x, odd-scale,
rotation, and hybrid-like transforms without random branch noise. Both the old
reference kernel and the precomputed-transform kernel use the same `noinline`
call boundary; the line also records compiler ID/version, configured and
preprocessor build mode, architecture, CMake optimization flags, and source
git SHA so the speedup value is comparable only within an identical build
shape.

Vulkan and DX12 renderer-transition registry walks now snapshot matching panel
addresses under their process-global registry mutex and release it before
calling `prepareForRendererTransition()` (and therefore presenter `Quiesce()`).
The lifetime proof is the existing `prepareVideoBackendTransition()` barrier:
the GUI thread waits synchronously for the emulation-thread transition and
cannot unpublish/destroy a panel until that wait returns. The VBlank registry
walk no longer exists; the registry is cold-path-only for construction,
destruction, and renderer-transition lookup.

The transition path emits one cold-path `renderer_transition` sample per
transition for `registry_lock_wait`, `quiesce_duration`, and
`transition_total`. Samples are local fixed POD values, so concurrent
instances/backends cannot mix attribution or race a process-global reset; the
offline summarizer can aggregate the emitted lines. The registry snapshot uses
a fixed `kMaxWindows` array, so the registry mutex does not allocate. The
two-instance close/transition contract is executable without a renderer SDK;
it deterministically blocks a transition, queues close requests for both the
matching snapshot panel and another instance, then releases the barrier:

```text
cmake --build build/release-mingw-x86_64 --target melonprime_native_panel_registry_transition_check
```

### Input event / frame RMW budget

The steady-state contract is load-first for rare claims and single-writer
load/store for monotonic raw accumulators:

| Path | Empty / steady operation | Claim / publication |
|---|---|---|
| Linux packed availability/motion state | one acquire load by the frame resolver | release stores at startup, first motion, and teardown |
| Linux absolute baseline / residual reset | no per-event reset check | cold mailbox token consumed at the filter-loop/event-batch boundary |
| Linux packed cumulative total writer | relaxed load | release store by the sole XInput filter thread |
| Linux motion-seen state | filter-thread shadow, no event-hot atomic load | one release publication in the packed state byte |
| macOS GCMouse / IOHID cumulative totals | relaxed load by the backend's sole serialized writer | release store; frame reader advances a subscription cursor |
| Qt panel aim cumulative total | relaxed load by the GUI-thread sole writer | packed release store; reset publishes a separate boundary baseline |
| SDL wheel pulse | relaxed zero load | exchange only when a pulse is pending |
| Core config reload | relaxed false load | `exchange(false, acq_rel)` on a pending edge |
| cursor-mode command | relaxed `-1` load | `exchange(-1, acq_rel)` on a command |
| wheel mailbox | relaxed zero load | exchange for an event or nonzero generation-only boundary |
| GUI / persist request mailbox | relaxed empty load | exchange only when a request is pending |

A producer racing an empty load is not cleared: its value remains pending and
is consumed on the next normal frame. Config reload and cursor mode are
coalesced replacement commands, so this bounded delay is intentional. Wheel
keeps the packed generation/value invariant and never treats a generation-only
publication as empty.

The macOS load/store accumulator contract relies on explicit writer
serialization: all GCMouse value-change handlers use one serial handler queue,
while IOHID has one worker runloop. The two backends publish separate totals,
so they never become concurrent writers of the same atomic. Qt panel movement
is likewise serialized by GUI dispatch; its reset baseline is separate from the
producer-owned total and preserves movement that arrives after a reset request.

### V6 Measurement Gate (historical baseline rule)

V6 Phase 0 prepared the `MELONPRIME_PERF=1` baseline procedure and parser, but
the canonical 10-minute ROM soaks for macOS, Windows, and Linux were still the
gate for Phase 3 and Phase 5 performance changes at that snapshot. Under that
historical gate, HUD element caching, per-frame patch/write edge changes, and
input queue timing changes had to stay in the planning state or be committed
only as measurement harness work.

This is the retained V6 gate for new, unmeasured optimizations; it is not a claim that all later
HUD cache work remains only planned. The later HUD runtime, scoreboard, and Enemy Target caches and the 2026-08-27
SRP ownership closure are documented in [custom-hud-runtime.md](../development/hud/custom-hud-runtime.md)
and [srp-performance-contract.md](srp-performance-contract.md). Their runtime, hardware, and
remote-CI acceptance remains separately classified rather than inferred from source audits.
