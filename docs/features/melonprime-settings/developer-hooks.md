# Developer-only aim and input hooks

## Availability and safety boundary

The Native Aim Hook Mode and Immediate Input Edge Overlay controls are hidden
or sanitized unless MELONPRIME_ENABLE_DEVELOPER_FEATURES is enabled. Release
builds must not expose these experimental hooks as ordinary compatibility
features.

| Control | Key | Default | Values |
| --- | --- | --- | --- |
| Native aim hook mode | Metroid.Aim.NativeHookMode | 0 | 0 off, 1 register injection, 2 PostFold |
| Immediate input edge overlay | Metroid.Input.Enable.ImmediateInputEdgeOverlay | false | developer diagnostic |
| Low-latency aim mode | Metroid.Aim.LowLatencyMode | 0 | 0 off, 1 Immediate Sync, 2 MoonLike Aim, 3 legacy alias |

FPS Camera Lock is public and is documented separately in
[Instant aim follow and camera lock](aim-follow.md); it is not a developer
hook merely because it patches ARM9 code.

## Shared player and input memory

The native hook modules use the same guest player/input layout. These are
offsets from the current `CPlayer` pointer unless an absolute ROM address is
shown.

| Object | Offset | Meaning |
| --- | ---: | --- |
| Player input struct | CPlayer + 0x464, size 0x48 | Current/previous/pressed/held/released/quick-repress state |
| Input-state fields | +0x00, +0x02, +0x04, +0x06, +0x08, +0x0A | Six 16-bit state fields in that order |
| Movement bindings | +0x368, +0x36C, +0x370, +0x374 | Four directional binding slots |
| Fire/jump/morph bindings | +0x398, +0x39C, +0x3A0 | Physical binding masks |
| Boost/zoom bindings | +0x3B4, +0x3E0 | Physical binding masks |
| Hunter ID | +0x400 | ID 4 identifies Noxus |
| Double Damage timer | +0x4B0 | Player-side timer used by the damage path |
| Scope zoom state | +0x850 | Native zoom state |
| Weapon state | +0x858, weapon +0x08 | Current weapon and native zoom guard |
| Noxus alternate-attack timer | +0x704, u16 | Cleared by the death-persistence hook |

The ROM-specific global address of the local-player pointer is:

| ROM | Local-player pointer global |
| --- | ---: |
| JP1.0 | 0x020BE790 |
| JP1.1 | 0x020BE750 |
| US1.0 | 0x020BCA70 |
| US1.1 | 0x020BD2D0 |
| EU1.0 | 0x020BD2F0 |
| EU1.1 | 0x020BD370 |
| KR1.0 | 0x020B6240 |

Binding masks use their low 16 bits for the physical input mask. The upper
selector bits are not copied into the guest input-state fields. A hook that
uses these addresses must validate the player pointer and the relevant range
before reading or writing it.

## Native aim hook modes

Mode 0 leaves the native aim hook uninstalled. Mode 1 injects the host-computed
aim delta into guest registers at the normal and alternate-form sites. Mode 2
uses a single post-fold write path after TouchInputProcessor and writes the
folded delta into the guest input structure.

The two modes are mutually exclusive in the UI. A configuration reload must
not leave both dispatch masks active.

### Register injection sites

| ROM | Normal form | Alternate form |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x02024318 | 0x020220F8 |
| US1.0 / US1.1 | 0x0202433C | 0x0202211C |
| EU1.0 | 0x02024334 | 0x02022114 |
| EU1.1 | 0x0202433C | 0x0202211C |
| KR1.0 | 0x0200D208 | 0x0200FBA0 |

When the VSteer/spec108 path is active, the additional spec108=0 site is:

| ROM | Additional VSteer site |
| --- | ---: |
| JP1.0 / JP1.1 | 0x02021DAC |
| US1.0 / US1.1 | 0x02021DD0 |
| EU1.0 | 0x02021DC8 |
| EU1.1 | 0x02021DD0 |
| KR1.0 | 0x0200F884 |

The registration hook dispatches by execution PC and ROM group. It must not
run the same logical update twice when an instruction site is reached through
an alternate-form path.

### PostFold sites

Mode 2 installs one post-fold site:

| ROM | PostFold site |
| --- | ---: |
| JP1.0 / JP1.1 | 0x0202A030 |
| US1.0 / US1.1 | 0x0202A054 |
| EU1.0 | 0x0202A04C |
| EU1.1 | 0x0202A054 |
| KR1.0 | 0x02028070 |

At that site the hook writes the folded X/Y result to the player input
structure at offsets +0x2A and +0x2C. The path runs after TouchInputProcessor,
covers all alternate forms including spec108=0, and is not the Dual-control
path. The source contract is one hook after fold, not a collection of
per-form patches.

## Immediate input edge overlay

The overlay is a developer diagnostic around the shared post-poll Action
Consumer. It reads the player input structure at +0x464 and exposes same-frame
edges such as fire, jump, zoom, movement, and other action transitions for
debugging.

It is not a replacement for the native weapon, biped-fire, or zoom methods.
Those consumers must still preserve the guest's own cooldown, ammo, validity,
and transition rules. When the overlay and native biped-fire dispatch share a
PC, the dispatcher must invoke the shared post-poll update once.

The detailed event-field and validation contract is maintained in
docs/architecture/gameplay/patches/immediate-input-edge-overlay.md.

## Low-latency aim modes

Low-latency aim mode is public but experimental:

| Value | Name | Behavior |
| ---: | --- | --- |
| 0 | MPH native | Game's ordinary aim-follow path |
| 1 | Immediate Sync | Sync currentAim to targetAim at the ARM9 hook |
| 2 | MoonLike Aim | Apply small moves immediately; chase large jumps by a max step |
| 3 | InstantAimFollow | Legacy compatibility alias; do not reinterpret as Immediate Sync |

MoonLike tuning values are Q12 integers:

| Key | Default |
| --- | ---: |
| Metroid.Aim.MoonLikeAimNormalStepQ12 | 0x0165 |
| Metroid.Aim.MoonLikeAimFastStepQ12 | 0x058F |
| Metroid.Aim.MoonLikeAimFastThresholdQ12 | 0x042E |

The low-latency hook's ROM-specific exit sites are:

| ROM | Exit A | Exit B |
| --- | ---: | ---: |
| JP1.0 / JP1.1 | 0x020282C8 | 0x02028544 |
| US1.0 / US1.1 | 0x020282EC | 0x02028568 |
| EU1.0 | 0x020282E4 | 0x02028560 |
| EU1.1 | 0x020282EC | 0x02028568 |
| KR1.0 | 0x0200B454 | 0x0200B6B0 |

These addresses describe hook registration sites, not proof of a successful
low-latency runtime path. Hardware, renderer, speed state, and input source
must be recorded for timing tests.

## Lifecycle and verification

Native hooks are installed through ARM9Hook_Install after
NotifyConfigChanged. The selected ROM group and expected original instruction
must validate before registration. Disabling a mode, leaving, or stopping the
match removes the hook and restores the original execution path.

Verification should include:

- mode 0 as the no-hook control;
- mutual exclusion of modes 1 and 2;
- normal and alternate forms;
- spec108=0;
- Touch versus Dual control paths;
- immediate edge overlay with native fire enabled;
- config reload, leave, stop, and rejoin; and
- guard failure with no partial registration.

Do not treat a local hook-install log as physical latency acceptance. Runtime
measurements and hardware/backend gates remain separate evidence.

## Evidence and related material

Current source:

- MelonPrimeArm9Hook.cpp
- MelonPrimePatchNativeAimDeltaHookRegisterInjectionVersion.inc
- MelonPrimePatchNativeAimDeltaHookPostFoldWriteVersion.inc
- MelonPrimePatchImmediateInputEdgeOverlay.inc
- MelonPrimePatchLowLatencyAimHook.inc
- MelonPrimeInputConfig.cpp

Deep existing docs:

- docs/architecture/gameplay/patches/native-aim-delta-register-injection.md
- docs/architecture/gameplay/patches/immediate-input-edge-overlay.md
- docs/architecture/input/aim-input.md

Supporting research is maintained in mphCodex and is intentionally referenced
instead of copied:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\AimHook\README.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md
