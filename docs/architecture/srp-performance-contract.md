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
