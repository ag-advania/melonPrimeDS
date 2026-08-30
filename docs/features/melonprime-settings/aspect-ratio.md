# In-game aspect ratio

## Setting contract

UI section: In-Game Aspect Ratio.

| Item | Value |
| --- | --- |
| Enable key | Metroid.Visual.InGameAspectRatio |
| Enable default | schema default true |
| Mode key | Metroid.Visual.InGameAspectRatioMode |
| Mode default | 0, Auto |
| UI modes | 0 Auto, 1 5:3 (3DS), 2 16:10 (3DS), 3 16:9, 4 21:9 |
| Implementation | MelonPrimePatchAspectRatio.cpp |
| Patch kind | Two ARM9 words plus one 16-bit data value |

The setting changes the guest's in-game 3D scale. It does not change the
host window geometry or the bottom-screen layout. It is intended for displays
whose active top-screen aspect is wider than native 4:3.

## Mode resolution

The runtime resolves the selected UI mode at game-join time.

1. If Metroid.Visual.InGameAspectRatio is false, return without touching RAM.
2. If the combo is Auto, read the host window's ScreenAspectTop value.
3. Map ScreenAspectTop IDs as follows:

   | ScreenAspectTop ID | Meaning | Internal patch mode |
   | ---: | --- | ---: |
   | 0 | 4:3 native | no patch |
   | 1 | 16:9 | 2 |
   | 2 | 21:9 | 3 |
   | 3 | window | no patch |
   | 4 | 5:3 | 0 |

4. For a manual combo selection, use combo index minus one. Thus the internal
   patch modes are 0=5:3, 1=16:10, 2=16:9, and 3=21:9.
5. Any invalid internal mode is a no-op.

The 16:10 option is available as an explicit 3DS-style mode even though Auto
does not select it from the current ScreenAspectTop mapping.

## Guest patch operation

The unmodified code loads a scale-related word from two different base
registers. The current source treats these as independent guarded sites:

~~~text
original instruction 1: 0xE5991664
original instruction 2: 0xE59A1664
original scale value:   0x1555
~~~

The two instructions are ARM load-word forms that load the scale path's
constant into r1. The replacement is an ARM immediate move into r1:

| Internal mode | Replacement instruction | Meaning at the instruction level | Replacement 16-bit value |
| ---: | ---: | --- | ---: |
| 0, 5:3 | 0xE3A01099 | mov r1,#0x99 | 0x1AAB |
| 1, 16:10 | 0xE3A0109F | mov r1,#0x9F | 0x199A |
| 2, 16:9 | 0xE3A0108F | mov r1,#0x8F | 0x1C72 |
| 3, 21:9 | 0xE3A0106D | mov r1,#0x6D | 0x2555 |

The exact fixed-point interpretation belongs to the guest scale routine; the
patch deliberately changes both the immediate instruction and the associated
16-bit scale datum so they remain a matched pair.

## ROM address table

| ROM | Instruction 1 | Instruction 2 | 16-bit scale value |
| --- | --- | --- | --- |
| JP1.0 | 0x0211313C | 0x0211E7E8 | 0x02112960 |
| JP1.1 | 0x021130FC | 0x0211E7A8 | 0x02112920 |
| US1.0 | 0x02110FFC | 0x0211C638 | 0x02110820 |
| US1.1 | 0x02111ABC | 0x0211D168 | 0x021112E0 |
| EU1.0 | 0x02111ADC | 0x0211D114 | 0x02111300 |
| EU1.1 | 0x02111B5C | 0x0211D208 | 0x02111380 |
| KR1.0 | 0x02109B64 | 0x02114838 | 0x021091A4 |

## Guard and state behavior

ApplyScalingPatch checks each address for its own original value before
writing. A site that is already patched is not written a second time. The
current implementation sets aspectRatioApplied after the apply attempt, but
does not use a cross-site all-or-nothing transaction: if one site no longer
contains its original value, the other original sites may still be changed.

This differs from the common StaticWordPatch helper used by several other
features. The aspect-ratio implementation is a custom three-site patch and
must be audited as such.

The registry entry is:

| Event | Current behavior |
| --- | --- |
| Game join | Resolve mode and apply the three guarded writes |
| Match end | No registry restore callback is registered |
| Emulator stop | Guest RAM is reset as part of emulator teardown; the aspect entry itself still has no RAM restore callback |
| Config reload | Not a battle-runtime registry entry |

Therefore, the current source does not promise that changing the option or
aspect mode during the same guest session restores the three original values.
The effective patch lifetime and reapplication behavior must be treated as a
source-level limitation until a restore/reapply policy is implemented and
runtime-tested.

The derivation of the `E3A010XX` upper byte, the Q12 aspect formula, and the
relationship between projection, 3D-to-2D placement, and culling is maintained
in the sibling repository rather than copied here:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_Commons\current\Widescreen.md

That research file also lists ratios such as 64:27 and 43:18. They are not
current MelonPrime Settings UI modes and must not be treated as supported by
this setting until the source mode table and UI expose them.

## Verification checklist

Static checks should validate:

- all three addresses are in ARM9 MainRAM for all seven ROM groups;
- the original instruction/value triplet matches the source table;
- Auto returns no-op for 4:3 and window;
- manual modes map one-to-one to the four replacement pairs;
- a second apply does not rewrite already patched sites;
- a mixed original/patched triplet is observable and is not incorrectly
  reported as an atomic successful patch.

Runtime checks still required before claiming acceptance:

- start a match at 5:3, 16:10, 16:9, and 21:9;
- verify the guest 3D geometry rather than only host-window stretching;
- change aspect or config across two matches in one emulator session;
- stop/restart and confirm the next guest boot is clean.

## References

- src/frontend/qt_sdl/MelonPrimePatchAspectRatio.cpp
- src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h
- src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp
- ../melonprime-settings.md
