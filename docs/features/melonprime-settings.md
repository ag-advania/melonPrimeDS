# MelonPrime Settings tab

This page is the coverage map for the **MelonPrime Settings** tab
(`tabMetroid`). It intentionally stays at the overview level. Configuration
keys, defaults, ROM-specific addresses, patch words, guard rules, lifecycle
details, and verification procedures are maintained in the linked detailed
references under [`docs/features/melonprime-settings/`](melonprime-settings/README.md).

Keeping the address and patch tables in one detailed page per setting avoids
copying the same reverse-engineering data into this index. When the current
melonPrimeDS source changes, update the owning detailed page first, then update
this page only if the coverage map or link changes.

## Scope and evidence boundary

The covered UI page is the page titled **MelonPrime Settings**. It is distinct
from the controller-binding pages and the Custom HUD pages:

| UI page | Scope |
| --- | --- |
| Controls (`tabAddonsMetroid`) | Controller bindings; not covered here |
| Controls 2 (`tabAddonsMetroid2`) | Additional bindings; not covered here |
| MelonPrime Settings (`tabMetroid`) | Covered by this document and the detailed reference directory |
| Custom HUD (tabCrosshair) | HUD editor; see the [Custom HUD settings guide](hud/custom-hud-settings.md) |
| Custom HUD Input/Output (tabCustomHudCode) | HUD import/export; see the [Custom HUD settings guide](hud/custom-hud-settings.md#toml-inputoutput) |

`MPSettingsDialog` is the multiplayer settings dialog and is not this tab.
“Confirmed” below means confirmed by the current source inventory; it does not
claim physical-hardware, long-duration, remote-CI, or full runtime acceptance.

The sibling `mphCodex` checkout is used as reverse-engineering and patch-history
context. Its documents are referenced from the detailed pages rather than
copied. If an older analysis and the current source disagree, the current
melonPrimeDS source wins and the discrepancy should be recorded explicitly.

## Complete coverage map

Every setting currently inserted into `tabMetroid` is covered by one of the
references below. Closely coupled
controls share a page only when they use the same implementation contract or
patch lifecycle.

| UI area | Functions covered | Detailed reference |
| --- | --- | --- |
| Dialog-level | Menu language, system/default language selection, responsive wrapping and scroll behavior | [menu-language.md](melonprime-settings/menu-language.md), [Settings UI and edit mode](../development/ui/settings-and-edit-mode.md#settings-tab-organization-and-responsive-presentation) |
| Section headers | Expand/collapse state for all MelonPrime Settings sections | [section-disclosure.md](melonprime-settings/section-disclosure.md) |
| Sensitivity | MPH sensitivity, aim sensitivity, Y scale, aim adjustment, accumulator, smoothing, zoom scale, reset button | [sensitivity.md](melonprime-settings/sensitivity.md) |
| Aim follow | MPH-native, Immediate Sync, MoonLike Aim, Instant Aim Follow migration alias, FPS Camera Lock | [aim-follow.md](melonprime-settings/aim-follow.md) |
| Morph Ball boost | Swipe boost enable/disable, raw-threshold mode, required movement | [morph-ball-boost.md](melonprime-settings/morph-ball-boost.md) plus [existing feature reference](morph-ball-boost.md) |
| Input compatibility | Joy2Key, Stylus Mode, top-screen touch, touch-screen aim only, temporary Standard-transform exception, in-game cursor hiding, top-screen confinement, idle center hold | [input-compatibility.md](melonprime-settings/input-compatibility.md), [touch-screen-aim-only.md](melonprime-settings/touch-screen-aim-only.md), [stylus-cursor-policy.md](melonprime-settings/stylus-cursor-policy.md) |
| Bug Fixes | Wi-Fi active friend/rival bitset, Wi-Fi reconnect Error 52200, Shadow Freeze, Noxus Blade persistence | [wifi-bitset.md](melonprime-settings/wifi-bitset.md), [existing reconnect reference](gameplay/wifi-reconnect-52200.md), [shadow-freeze.md](melonprime-settings/shadow-freeze.md), [noxus-blade-persistence.md](melonprime-settings/noxus-blade-persistence.md) |
| Game Feature Improvements | DS firmware language, online HEADSHOT, online enemy target information, base/extra stage matrix | [firmware-language.md](melonprime-settings/firmware-language.md), [online-headshot.md](melonprime-settings/online-headshot.md), [online-enemy-hp.md](melonprime-settings/online-enemy-hp.md), [stage-matrix.md](melonprime-settings/stage-matrix.md) |
| Disable Features | Disable Double Damage Multiplier, Damage Notify Purple | [damage-multiplier.md](melonprime-settings/damage-multiplier.md), [damage-notify-purple.md](melonprime-settings/damage-notify-purple.md) |
| Power-Up Pickup Effects | Parent switch plus Double Damage, Cloak, and Deathalt child switches | [powerup-pickup.md](melonprime-settings/powerup-pickup.md) |
| Gameplay Toggles | SnapTap, unlock save data, headphone audio, DS name | [snaptap.md](melonprime-settings/snaptap.md), [save-data-unlock.md](melonprime-settings/save-data-unlock.md), [headphone-audio.md](melonprime-settings/headphone-audio.md), [ds-name.md](melonprime-settings/ds-name.md) |
| Video Quality | Low, High, High2, and conditional Metal presets | [video-presets.md](melonprime-settings/video-presets.md) |
| Volume | SFX and music apply switches and levels | [volume.md](melonprime-settings/volume.md) |
| License Apply | Hunter and rank-color selection/apply switches | [hunter-license.md](melonprime-settings/hunter-license.md) |
| Input Method | Standard/New/New 2 weapon and transform selectors, developer Biped fire, and Standard/developer-native zoom selectors | [input-methods.md](melonprime-settings/input-methods.md) |
| Screen Sync | Off, `glFinish`, and Windows `DwmFlush` modes | [screen-sync.md](melonprime-settings/screen-sync.md) |
| Cursor Clip | Bottom-screen cursor clipping outside gameplay and stylus cursor presentation/confinement | [cursor-layout.md](melonprime-settings/cursor-layout.md), [stylus-cursor-policy.md](melonprime-settings/stylus-cursor-policy.md) |
| In-Game Apply | In-game top-screen-only layout policy | [cursor-layout.md](melonprime-settings/cursor-layout.md) |
| In-Game Aspect Ratio | Auto and manual 5:3, 16:10, 16:9, and 21:9 guest scale patch | [aspect-ratio.md](melonprime-settings/aspect-ratio.md) |
| Low HP Warning | Disabled/vanilla, fixed, per-damage, and auto-scale thresholds | [low-hp-warning.md](melonprime-settings/low-hp-warning.md) |
| Developer Only | Native aim register/PostFold hooks, immediate edge overlay, developer input methods | [developer-hooks.md](melonprime-settings/developer-hooks.md) |

## Configuration and patch lifecycle

The normal dialog Save/OK path persists per-instance settings. Several controls
also have intentional immediate side effects, so Cancel is not a universal
rollback for already-committed global or save-data operations:

- video preset buttons write global renderer configuration immediately;
- SnapTap, unlock, headphone audio, and DS-name controls use legacy immediate
  global-config/save-data paths; and
- hooks, guest patches, menu language, cursor policy, and in-game layout use
  their dedicated reconciliation or match-scoped lifecycle.

The relevant implementation paths are `MelonPrimeInputConfigConfig.cpp`,
`MelonPrimeInputConfig.cpp`, `MelonPrimeGameSettings.cpp`, and
`MelonPrimePatchRegistry.cpp`. The detailed pages identify the exact event
boundary for each setting.

At a high level:

| Lifecycle | Typical settings |
| --- | --- |
| Game join | In-game aspect ratio |
| Battle runtime/config reload | Low HP, FPS Camera Lock, online notifications, Double Damage, no-pickup, touch aim |
| Menu/out-of-game reconciliation | Wi-Fi fixes, firmware language, stage matrix, hunter/license/audio/save-data settings |
| Match-scoped ARM9 hook plan | Native aim, weapon/zoom/transform/fire paths, Shadow Freeze, Noxus persistence, immediate input edges |
| Host presentation | Screen Sync, cursor clipping, in-game layout, video presets |

Static guest patches are absolute ARM9 MainRAM writes and are guarded against
foreign words. Runtime hooks and host-only options do not necessarily have a
guest address. A source-level guard or successful hook registration is not
runtime acceptance; the detailed verification lists preserve that boundary.

## ROM groups

The detailed address tables use these seven supported ROM groups:

| Group | Meaning |
| --- | --- |
| JP1.0 | Japanese revision 1.0 |
| JP1.1 | Japanese revision 1.1 |
| US1.0 | North American revision 1.0 |
| US1.1 | North American revision 1.1 |
| EU1.0 | European revision 1.0 |
| EU1.1 | European revision 1.1 |
| KR1.0 | Korean revision 1.0 |

Addresses in those pages are guest ARM9 MainRAM addresses, not host pointers.
Where the source uses `0xFFFFFFFF`, the page records that ROM as unsupported or
not applicable for that patch.

## Existing deeper references

The following older documents remain useful for broader design history and
feature context. The per-setting directory links them only where they are the
canonical deeper treatment; it does not duplicate their prose or tables:

- [Morph Ball Boost](morph-ball-boost.md)
- [Wi-Fi reconnect fix (Error 52200)](gameplay/wifi-reconnect-52200.md)
- [Zoom aim sensitivity](zoom-aim-sensitivity.md)
- [Zoom input methods](input/zoom-input-methods.md)
- [Aim input](../architecture/input/aim-input.md)
- [Settings UI and edit mode](../development/ui/settings-and-edit-mode.md)
- [Patch system](../architecture/gameplay/patch-system.md)
- [Immediate input edge overlay](../architecture/gameplay/patches/immediate-input-edge-overlay.md)
- [Native aim delta register injection](../architecture/gameplay/patches/native-aim-delta-register-injection.md)
- [No-pickup patch](../architecture/gameplay/patches/no-picking-up-specific-items.md)
- [Shadow Freeze runtime-hook architecture note](../architecture/gameplay/patches/shadow-freeze-runtime-hook.md)

## Source and research ownership

Current source of truth:

- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp`
- `src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp`
- `src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc`
- `src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp`
- `src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameSettings.cpp`
- `src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h`
- `src/frontend/qt_sdl/MelonPrimePatchCommon.h`
- `src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp`
- the feature-specific `src/frontend/qt_sdl/MelonPrimePatch*.cpp` and hook
  include files named in each detailed reference

Supporting reverse-engineering material is intentionally kept in the sibling
`mphCodex` repository. For example, the aspect-ratio page points to:

`C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_Commons\current\Widescreen.md`

That keeps the analysis single-sourced while still making the exact research
trail discoverable to maintainers.
