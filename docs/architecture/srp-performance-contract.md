# MelonPrime SRP / Performance Contract

Contract for the MelonPrime SRP refactor v3 immediate plan. Audited by
`tools/ci/audits/audit-melonprime-srp-performance.ps1` on Windows and Ubuntu CI.

## SRP boundaries (Immediate Plan)

| Unit | Owns | Must not own |
|---|---|---|
| `RuntimeConfigSnapshot` | Config read, clamp, developer gate, derived bools | Core member mutation, pending clears, `RecalcAimEffectiveFixedScale` |
| `AimConfigSnapshot` (Phase 10) | Aim sensitivity/Y-scale/adjust read | Core member mutation, `RecalcAimFixedPoint` |
| `InputProjection` | Hotkey → down/press projection | Aim pipeline, SnapTap policy |
| `MelonPrimeGameInput.cpp` input owner | Frame projection consumption, Aim-derived state, specialized action latches, lifecycle reset profiles | Config lookup, generic command bus, platform-specific game semantics |
| `ScreenCursorPolicy` | Platform cursor clip/warp/capture | Mouse event routing, HUD editor bridge |
| `HudEditorFormBuilder` | Shared HUD editor helpers | `QColorDialog` direct calls, `Config::Save` |
| `PatchLifecycleGateway` | Lifecycle patch apply/restore | RunFrameHook per-frame patches, Custom HUD patch state |
| `MelonPrimeHudRender.h` | Custom HUD render entry, HUD font resolution | Editor, patch lifecycle, radar preprocessing, runtime queries, developer harness |
| `MelonPrimeHudConfigState.h` | Per-instance HUD state handle, config-cache epoch | Qt widget/event types |
| `MelonPrimeHudPresentationState.h` | Lightweight host presentation-state queries used by renderer front-ends | Editor event routing, drawing, patch lifecycle |
| `MelonPrimeHudRuntime.h` | Gameplay visibility, visual generation, match join | Drawing, editor, patching |
| `MelonPrimeHudRadar.h` | Radar colour-key source preparation | Radar drawing, HUD layout |
| `MelonPrimeHudPatchLifecycle.h` | Native HUD patch apply/restore/reset/reconcile | Rendering, editor |
| `MelonPrimeHudEdit.h` | On-screen editor session, mouse routing, selection | Rendering, patch internals |
| `MelonPrimeHudGoldenHarness.h` | Developer-only golden hash harness | Anything reachable in a release build |
| `MelonPrimeHudRuntimeSample.inc` | NDS RAM to bounded snapshot, game-mode semantics, match cache | Presentation text, Qt layout/font types, glyph outlines, ownership aggregation |
| `MelonPrimeHudPresentationText.inc` | Resolved runtime values to bounded display strings and text-cache updates | NDS RAM reads, painter/drawing policy |
| `MelonPrimeHudBattleOwnedState.inc` | Per-instance battle-state slot and restore-edge storage | RAM sampling, patch operations |
| `MelonPrimeHudFrameOwnedState.inc` | Single per-instance `frameState` aggregate and cache lifetime | Sampling policy, drawing policy, extra allocations |
| `MelonPrimeHudRenderPlan.inc` | Snapshot to draw-ready plan, layout/text/outline caches, painter transform | Emulated memory reads, game-mode meaning |
| `MelonPrimeHudRenderRuntime.inc` | Ordered unity wrapper for runtime child fragments | Feature implementation and additional state |
| `MelonPrimeHudRuntimeDraw.inc` | Runtime-sourced HUD draw helpers consuming `HudRuntimeState` | RAM sampling, visibility policy, patch lifecycle |
| `MelonPrimeHudRuntimePolicy.inc` | Custom HUD enablement, gameplay visibility, crosshair policy | Drawing, radar preprocessing, patch lifecycle |
| `MelonPrimeHudRadarRuntime.inc` | CPU radar color-key preprocessing and source-radius helper | HUD layout, native patch lifecycle |
| `MelonPrimeHudPatchRuntime.inc` | Native HUD patch apply/restore/reset/reconcile implementation | Drawing and radar processing |
| `MelonPrimeHudStateEpoch.inc` | Config-cache and visual-generation entry points | RAM sampling, drawing, patch operations |
| `MelonPrimePatchAimSmoothing` | Aim smoothing ARM9 instruction patch and its preconditions | Game setting RAM writes, patch scheduling |
| `MelonPrimeGameSettings` | MPH setting RAM writes | ARM9 instruction patching |

The field-level input ownership, cadence and physical-move risk map is maintained
in [input/input-srp-ownership.md](input/input-srp-ownership.md). Logical owners
use embedded Core storage where locality is load-bearing; SRP does not imply a
heap object or one class per responsibility.

## Rendering backend ownership

One rule decides these rows: **the unit that declares a responsibility is the
unit that creates, destroys, resets and mutates its state.** A declared owner
whose state someone else writes is not an owner, and the mismatch is invisible
until a reset or a lifetime bug makes it visible.

| Unit | Owns | Must not own |
|---|---|---|
| `DX12Renderer3D` | DS 3D raster semantics, geometry, raster orchestration, raster resources, subsystem coordination | GPU2D publication lifecycle |
| `VulkanRenderer3D` | DS 3D raster semantics, geometry, raster orchestration, raster resources, subsystem coordination | GPU2D publication lifecycle |
| `DX12Gpu2DComposer` | compose behaviour, output resource lifecycle, publication state, output lease source | raster resource ownership, renderer failure reason |
| `VulkanGpu2DComposer` | compose behaviour, output resource lifecycle, publication state, output lease source | raster resource ownership, renderer failure reason |
| `DX12Gpu2DOutput` | resolution-lifetime compositor resources | 3D raster policy |
| `VulkanGpu2DOutput` | resolution-lifetime compositor resources | 3D raster policy |
| `<Backend>CaptureBridge` | physical capture mechanism | capture validity semantics |
| `CaptureProvenanceState` | backend-neutral capture validity semantics | native GPU handles |
| `RendererOutputRing` | backend-neutral publication and lease protocol | native GPU handles |
| `<Backend>Presenter` | final surface and present | renderer resource lifetime |
| `DX12LowLatencyController` / `VulkanLatencyController` | vendor low-latency sessions and policy | present-surface ownership |

What that means in practice for the renderer:

```
Gpu2D.RecreateOutput(...)        not  Gpu2D.Output = make_shared<...>
Gpu2D.ReleaseOutput()            not  Gpu2D.Output.reset()
Gpu2D.ResetForRendererEpoch(...) not  walking Ring / Slots / WorkSlots
Gpu2D.MarkFatal()                not  Gpu2D.LastComposeResult = Fatal
Gpu2D.GetComposedOutput()        not  reading Output + the published slot
```

Two deliberate exceptions, both because the descriptor contract really is
shared and inventing a boundary would describe something that is not there:

- `Gpu2D.Output` stays readable. DX12 assembles one fourteen-entry UAV table
  out of raster resources and slot resources together, and Vulkan's
  rasterizer set-0 write binds slot 0's structured input. Reading is fine;
  every mutation goes through an operation above.
- A released output rewinds the compositor's descriptor *contents*
  (`Reset()`), never its heaps (`Shutdown()`). The contents describe one
  resource set; the heaps are sized from the root-signature layout and
  outlive every resolution.

Ratcheted by `tools/ci/audits/audit-melonprime-srp-performance.ps1`.

## Hot path (no new abstraction cost)

These functions must not gain virtual dispatch, `std::function`, heap allocation,
QString/std::string conversion, `Config::Table` lookup, `QMetaObject` invoke,
`dynamic_cast`, or new mutex/atomic usage:

```text
MelonPrimeCore::RunFrameHook
MelonPrimeCore::UpdateInputStateImpl
MelonPrimeCore::ProcessMoveAndButtonsFastImpl
MelonPrimeCore::ProcessAimInputMouse
ARM9Hook DispatcherCallback
CustomHud_Render
```

Existing atomics may remain; do not add new ones in hot paths.

## Platform ifdef taxonomy

Use explicit platform branches:

```text
_WIN32
__APPLE__
__linux__
other non-Windows (e.g. BSD)
```

Forbidden pattern:

```cpp
#elif !defined(_WIN32)
// Linux/macOS-only API here
#endif
```

## Screen.cpp dependency rule

`Screen.cpp` must not `#include` MelonPrime patch or ARM9 hook internals. Cursor and
input policy belong in dedicated units; patch lifecycle stays out of Screen.

## Custom HUD API and fragment boundaries

The Custom HUD API is split by responsibility across the headers listed above.
Consumers include only what they call: a renderer front-end must not pick up the
editor's Qt event types, and nothing that only draws may see native HUD patch
internals. `MelonPrimeHudRender.h` must not re-export the other headers.

This is an API boundary, not a link boundary. Every declaration is still defined
by the single `MelonPrimeHudRender.cpp` unity translation unit, and the unity
fragments (`MelonPrimeHudRenderPlan.inc`, `MelonPrimeHudRuntimeSample.inc`,
`MelonPrimeHudRenderRuntime.inc` and its children) are one logical split of one
TU. Include order is load-bearing: the plan fragment comes first; the sampling
fragment then defines the game-facing types and includes presentation text,
battle ownership, and frame ownership at the points where their dependent
types are complete. The runtime wrapper expands its children in the historical
draw, policy, radar, patch, and epoch order.

Enforced by `audit-melonprime-srp-performance.ps1` Rule B, per function rather
than per group, with the matching requirement that the owner header still
declares what it owns. Rule B2 keeps Vulkan and Metal presenters off the heavy
editor header, Rule E keeps presentation types out of the sampling fragment,
Rule F keeps raw RAM reads out of the drawing fragment, Rule A2 keeps
aim-smoothing wired at game join, and Rule C2 keeps the fast-forward writer in
`EmuThread.cpp`.

The per-instance HUD owner slots are typed `std::unique_ptr` members of
`CustomHudConfigState`. `MelonPrimeHudRender.cpp` constructs the three concrete
owners (battle, frame, and text-cache) in the cold config-state constructor;
editor fields remain directly owned by `CustomHudConfigState` until a future
phase introduces a concrete editor owner. Frame/text accessors only dereference
those already-live owners. The top-level `MelonPrimeCore::m_hudConfigState`
`std::shared_ptr` remains the instance lifetime boundary and is not part of the
frame path. Rule G hard-fails if an internal HUD fragment reintroduces erased
ownership, casts, or lazy owner construction.

HUD enablement is resolved once at the screen config-epoch boundary. The
screen-owned `m_hudEnabled` snapshot is passed through the overlay helper into
`CustomHud_Render`, which does not read live config. Visibility and native-HUD
restore decisions therefore use the same epoch-coherent value. Rule H
hard-fails if the render body regains a live `CustomHud_IsEnabled` or config
getter call.

ARM9 hook module activation follows the same cold-boundary rule. Runtime config
is resolved into `Arm9HookActivationPlan` by
`ApplyRuntimeConfigSnapshot`; `ARM9Hook_Install` consumes only that plan and
the ROM scope. The dispatcher stores only the ROM group in the per-Core
`MelonPrimeArm9HookState` and passes it to the stateless Shadow Freeze/Noxus
modules; their handlers do not own a process-global activation context. Rules I
and J hard-fail if the installer regains direct config/key interpretation or a
module-local config/ROM cache. The ARM9 dispatcher remains a cached,
address-gated fast path.

## QColorDialog rule

`QColorDialog` usage stays confined to `MelonPrimeColorDialogPrefs.cpp` (enforced by
`audit-color-dialog-prefs.ps1`). HUD editor code calls `ColorDialogPrefs::getColor()`.

## Public API rule (initial PRs)

Do not add MelonPrimeCore public getters for runtime config fields. Prefer
`ApplyRuntimeConfigSnapshot(const RuntimeConfigSnapshot&)` as a private apply path.

## MelonPrimeCore runtime state ownership

`isCursorMode`, `isStylusMode`, `m_snapTapMode`, `isFastForward` and
`screenSyncMode` are private. The emulation thread owns them:

```text
GUI read      -> MelonPrimeUiSnapshot via ThreadBridge().ReadForGui()
GUI write     -> the ThreadBridge mailbox
                 (RequestCursorModeFromGui / ConsumeCursorModeForEmu)
config write  -> ApplyRuntimeConfigSnapshot
EmuThread     -> SetFastForwardState(), the one narrow inline setter
```

A new external need is not answered with a public getter (see the rule above) or
a new atomic; it is answered through the ThreadBridge, which stays the single
GUI/Emu communication boundary. The five declarations must also stay where they
are in the member layout -- cache-line grouping is load-bearing, so only the
access specifier changed. Enforced by `audit-melonprime-srp-performance.ps1`
Rule C, which tracks the access specifier through the class body rather than
searching for the field name. Rule C2 separately checks that the narrow
`SetFastForwardState()` writer has exactly one call site, in `EmuThread.cpp`.

## Never mix (same PR)

```text
RuntimeConfigSnapshot + RunFrameHook
InputProjection + Aim pipeline
ScreenCursorPolicy + mouse router
HudEditorFormBuilder + HUD render unity split
PatchLifecycleGateway + ARM9 hook context
PlatformInputPolicy + raw filter ownership
State struct extraction + feature fix
```

## RunFrameHook order (PR 1–6)

Do not reorder without a dedicated review:

```text
1. reentrant path
2. config reload
3. m_isRunningHook = true
4. focused load
5. UpdateInputState
6. InputReset
7. clear BIT_BLOCK_STYLUS
8. HandleGlobalHotkeys
9. ROM detect
10. inGame flag update
11. game join init
12. battle runtime transition
13. HUD pre-frame clamp
14. DamageNotifyPurpleTick
15. focused in-game/out-of-game input
16. cursor/touch dispatch
17. focus transition reset
18. pending native request tick
19. m_isRunningHook = false
```

## Review grep (manual, not CI fail)

`audit-melonprime-srp-performance.ps1` Rule D now prints the same scan, scoped to
the hot-path function bodies themselves. It never hard-fails: grep cannot tell a
real per-frame `Config` lookup from a mention in a comment, so the output is for
a human to judge. The warning scan also calls out shared ownership/casts,
mutex locks, raw `new`, and `std::string` construction so new hot-path cost is
visible during review; Rules G, H, and I provide the hard failures for the HUD
ownership, render-snapshot, and ARM9 activation-plan contracts.

```bash
rg "std::function|virtual|dynamic_cast|QMetaObject|Config::Table|GetBool|GetInt|GetDouble|QString|std::string" \
  src/frontend/qt_sdl/MelonPrime.cpp \
  src/frontend/qt_sdl/MelonPrimeGameInput.cpp \
  src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp
```
