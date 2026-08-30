# Input method selectors

## Controls

The Input Method section selects how weapon switching, biped firing, transform,
and zoom requests are generated:

| Control | Key | Default | Values |
| --- | --- | --- | --- |
| Weapon switch method | Metroid.Input.WeaponSwitchMethod | 0 | 0 legacy, 1 native |
| Biped fire method | Metroid.Input.BipedFireMethod | 0 | 0 legacy, 1 native |
| Transform method | Metroid.Input.Enable.DirectAltFormTransform | false | false legacy, true native |
| Zoom method | Metroid.Input.ZoomMethod | 0 | 0 legacy, 1 retired alias, 2 native |

The controls select paths; they do not change the keyboard bindings themselves.
The existing [Zoom input methods](../input/zoom-input-methods.md) page contains
the longer zoom design and test history.

## Shared action-consumer boundary

The native weapon, biped-fire, and zoom paths integrate at the guest's shared
Action Consumer. Its ROM-specific PC is:

| ROM | Action Consumer PC |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02024174 |
| US1.0 / US1.1 | 0x02024198 |
| EU1.0 / EU1.1 | 0x02024190 / 0x02024198 |
| KR1.0 | 0x0200F6DC |

The hook dispatcher also shares the post-poll player input update for
immediate edges. This shared boundary is why enabling multiple native
methods must be tested for duplicate dispatch. The source owns the
single-dispatch rule.

## Weapon switch

Value 0 retains the legacy touch/menu-driven path. Value 1 hooks the native
TryEquipWeapon path. The native path uses a trampoline at 0x02003EA0 and a
scratch area at 0x02003EE0.

| ROM | Hook site | TryEquipWeapon target |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x02026BFC | 0x0200C5FC |
| US1.0 | 0x02026C20 | 0x0200C5FC |
| US1.1 | 0x02026C20 | 0x0200C5FC |
| EU1.0 | 0x02026C18 | 0x0200C600 |
| EU1.1 | 0x02026C20 | 0x0200C5FC |
| KR1.0 | 0x0200C29C | 0x02025DBC |

The US revisions currently share both values shown above. The complete expected
BL words and continuation values remain in
MelonPrimePatchWeaponSwitchHook.inc. Apply validates the expected call before
installing the match-scoped hook.

Acceptance requires that a native switch produces one guest equip request,
does not bypass ammo/weapon validity, and restores the legacy instruction
after leave/stop.

## Biped fire

Value 0 uses the legacy input path. Value 1 consumes a native fire edge at the
shared action boundary. The helper sets the fire result true for the native
edge; guest cooldown, ammo, projectile creation, HUD, and sound remain owned
by the game.

This is not a “fire every frame” patch. Test press, hold, release, cooldown,
empty ammo, weapon changes, and pause/menu transitions.

## Transform

When Direct Alt-form Transform is false, transform continues through the
legacy simulated input path. When true, the native transform gate hooks the
guest TransformRequest condition/call pair.

| ROM | Compare site | Transform call site |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x02023B3C / 0x02023B74 | 0x02025F94 / 0x02025FCC |
| US1.0 / US1.1 | 0x02023B60 / 0x02023B98 | 0x02025FB8 / 0x02025FF0 |
| EU1.0 / EU1.1 | 0x02023B58 / 0x02023B90 | 0x02025FB0 / 0x02025FE8 |
| KR1.0 | 0x02011598 / 0x020115D0 | 0x0200EE54 / 0x0200EE8C |

The paired values in the source table are alternatives within the
version-specific control flow, not a license to patch both regions blindly.
Guard failure must leave the legacy path intact.

## Zoom

Value 0 is the legacy zoom path. Value 1 is a retired configuration value that
behaves as value 0. Value 2 uses the native SetPlayerScopeZoom path.

| ROM | SetPlayerScopeZoom site |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02015C98 |
| US1.0 | 0x02015CB8 |
| US1.1 | 0x02015CBC |
| EU1.0 | 0x02015CB0 |
| EU1.1 | 0x02015CBC |
| KR1.0 | 0x0201CEBC |

The native weapon-action path uses the shared action consumer for JP/US/EU
and 0x0200D07C for KR, with a trampoline at 0x02003F00 and scratch area at
0x02003F40. The activation edge, guest scope call, and release behavior must
be tested independently from zoom sensitivity scaling.

## Lifecycle and interactions

Native hooks are match-scoped. Configuration changes are consumed by
NotifyConfigChanged and reconciled by the ARM9 hook installer. A hook being
installed is not proof that the input edge reached the guest; inspect the
behavioral path as well.

Stylus mode can intentionally bypass native aim-related controls. Joy2Key and
SnapTap can change the host edge sequence before the native hook sees it.
Immediate Input Edge Overlay is a developer diagnostic that shares a post-poll
boundary and must not create duplicate fire/zoom/transform actions.

## Verification checklist

- Test each selector with the other selectors at their defaults.
- Test combinations of native weapon/zoom/fire and transform.
- Verify one action per physical edge, not one action per frame.
- Test cooldown, ammo, invalid weapon, pause, menu, morph, and respawn paths.
- Verify per-ROM guards and leave/stop restoration.
- Test with Joy2Key, SnapTap, and stylus settings recorded.
- For zoom, separate activation method from zoom sensitivity scale.

## Evidence and related material

Current source:

- MelonPrimePatchWeaponSwitchHook.inc
- MelonPrimePatchNativeBipedFireHook.inc
- MelonPrimePatchImmediateTransformGateHook.inc
- MelonPrimePatchNativeZoomToggleHook.inc
- MelonPrimeArm9Hook.cpp

Detailed existing docs:

- docs/features/input/zoom-input-methods.md
- docs/architecture/gameplay/patches/immediate-input-edge-overlay.md
- docs/architecture/input/aim-input.md

Supporting reverse-engineering material:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\CallingRawFunctions\changeWeapon\current\README.md
