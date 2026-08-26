# MelonPrime SRP / Performance Contract

Contract for the MelonPrime SRP refactor v3 immediate plan. Audited by
`tools/ci/audits/audit-melonprime-srp-performance.ps1` on Windows and Ubuntu CI.

## SRP boundaries (Immediate Plan)

| Unit | Owns | Must not own |
|---|---|---|
| `RuntimeConfigSnapshot` | Config read, clamp, developer gate, derived bools | Core member mutation, pending clears, `RecalcAimEffectiveFixedScale` |
| `AimConfigSnapshot` (Phase 10) | Aim sensitivity/Y-scale/adjust read | Core member mutation, `RecalcAimFixedPoint` |
| `InputProjection` | Hotkey → down/press projection | Aim pipeline, SnapTap policy |
| `ScreenCursorPolicy` | Platform cursor clip/warp/capture | Mouse event routing, HUD editor bridge |
| `HudEditorFormBuilder` | Shared HUD editor helpers | `QColorDialog` direct calls, `Config::Save` |
| `PatchLifecycleGateway` | Lifecycle patch apply/restore | RunFrameHook per-frame patches, Custom HUD patch state |

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

## QColorDialog rule

`QColorDialog` usage stays confined to `MelonPrimeColorDialogPrefs.cpp` (enforced by
`audit-color-dialog-prefs.ps1`). HUD editor code calls `ColorDialogPrefs::getColor()`.

## Public API rule (initial PRs)

Do not add MelonPrimeCore public getters for runtime config fields. Prefer
`ApplyRuntimeConfigSnapshot(const RuntimeConfigSnapshot&)` as a private apply path.

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

```bash
rg "std::function|virtual|dynamic_cast|QMetaObject|Config::Table|GetBool|GetInt|GetDouble|QString|std::string" \
  src/frontend/qt_sdl/MelonPrime.cpp \
  src/frontend/qt_sdl/MelonPrimeGameInput.cpp \
  src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp
```
