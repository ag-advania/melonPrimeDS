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
| `m_hudCfgEpoch` / `m_hudEnabled` | `Screen.h` + `MelonPrimeHudScreenCppHelpers.inc:117-124` | `Metroid.Visual.CustomHUD` change | `MelonPrimeHud_RefreshHudEnabledIfNeeded` |
| `m_hudFontEpoch` / `overlayFont` | `Screen.h` + helpers | HUD font property change | `MelonPrimeHud_RefreshOverlayFontIfNeeded` |
| `m_radarCfgEpoch` / radar GL fields | `Screen.h` + helpers | Radar property change | `MelonPrimeHud_RefreshRadarConfigIfNeeded` |
| `m_hudTopMatrix` / layout scale | `Screen.h` + `MelonPrimeHudScreenCppLayout.inc:5-17` | `setupScreenLayout()` | `ScreenPanel::setupScreenLayout` |
| `m_radarAnchorDsX/Y` | `Screen.h` + helpers | Radar config epoch refresh | `MelonPrimeHud_RefreshRadarConfigIfNeeded` |
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
