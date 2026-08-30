# Aim and sensitivity settings

## Scope

This page documents the sensitivity-related controls in the MelonPrime
Settings tab. It separates host input scaling from guest sensitivity writes and
from the optional static aim-smoothing patch. The parent
[MelonPrime Settings reference](../melonprime-settings.md) is the compact
inventory; this page is the implementation-level reference.

## Controls and defaults

| Control | Key | Default | Range or choices |
| --- | --- | ---: | --- |
| MPH sensitivity | Metroid.Sensitivity.Mph | -3 | -5.008 to 155.225 |
| Aim spin/sensitivity | Metroid.Sensitivity.Aim | 63 | 0 to 99999 |
| Aim Y scale | Metroid.Sensitivity.AimYAxisScale | 1.5147 | 0.000001 to 2 |
| Aim adjust | Metroid.Aim.Adjust | 0.01 | 0 to 100 |
| Zoom sensitivity scale enabled | Metroid.Aim.ZoomScale.Enable | false | checkbox |
| Zoom sensitivity scale | Metroid.Aim.ZoomScale.Percent | 75 | 10 to 300 percent |
| Disable MPH aim smoothing | Metroid.Aim.Disable.MphAimSmoothing | false | guarded ROM patch |
| Aim accumulator | Metroid.Aim.Enable.Accumulator | false | runtime input path |
| MoonLike normal step | Metroid.Aim.MoonLikeAimNormalStepQ12 | 0x0165 | Q12 integer |
| MoonLike fast step | Metroid.Aim.MoonLikeAimFastStepQ12 | 0x058F | Q12 integer |
| MoonLike fast threshold | Metroid.Aim.MoonLikeAimFastThresholdQ12 | 0x042E | Q12 integer |

The old Metroid.Sensitivity.MorphBoostMouse value remains a legacy
compatibility setting. Morph Boost's current input path is documented in
[Morph Ball Boost](../morph-ball-boost.md), including the swipe-distance and
custom-threshold controls.

## Reset sensitivity values

The **Reset sensitivity values** button is a UI operation rather than a
configuration key. It walks the widgets currently descended from the
Sensitivity section and restores each widget from the corresponding compiled
default. The typed reset covers checkboxes, inverted checkboxes, integer and
floating-point spin boxes, and combo-box values. The dynamically inserted
Low-Latency Aim combo is reset separately because its save path uses the combo
item's data value.

Signals are blocked while the widgets are reset, so clicking the button does
not write the local configuration immediately. The normal dialog Save/OK path
is still required. Afterward, the dependent enabled states are recalculated;
the button does not reset settings in other sections or undo an already
committed global renderer/save-data operation.

## Host-side aim transformation

The normal aim path obtains the input delta, applies the configured spin
factor, applies the Y-axis scale where applicable, and then applies the
configured adjustment policy. These values are not addresses in the ROM and
do not by themselves change guest memory.

The numeric ranges are intentionally broad for experimentation. In
particular:

- MPH sensitivity is not a conventional 0-to-100 slider. A value of 0 is not
  “zero sensitivity”; it is a point in the conversion range.
- Aim spin 63 is the shipped default. Values close to zero can make aim appear
  unresponsive, while very large values can amplify one-frame input noise.
- Aim Y scale near 1.0 is neutral in the ordinary interpretation. The default
  1.5147 is retained for compatibility with the current tuning.
- Aim adjust is a host-side correction term, not the same thing as the ROM's
  sensitivity byte.

The UI must preserve the stored floating-point precision. A display-rounded
value is not sufficient evidence that the persisted value has the same
behavior.

## Guest sensitivity writes

Metroid.Sensitivity.Mph is consumed during the game-settings reconciliation
path. The implementation writes the configured value to the game sensitivity
field and to the corresponding in-game sensitivity field when the applicable
game initialization path runs. This is a guest-data write, not a persistent
save patch by itself.

The write must preserve the ROM/version table selected by the current guest.
Do not carry an address from JP1.0 into US/EU/KR. The exact table and the
reconciliation lifecycle are maintained in MelonPrimeGameSettings.cpp and
MelonPrimeGameRomAddrTable.h.

## Zoom scaling

When Metroid.Aim.ZoomScale.Enable is false, the configured zoom percentage is
not applied. When enabled, the zoomed aim path multiplies the applicable
input contribution by Metroid.Aim.ZoomScale.Percent, whose UI range is 10 to
300 percent. This is distinct from
Metroid.Input.ZoomMethod, which selects the guest zoom activation behavior.

Use this distinction when testing:

1. hold the same physical mouse movement while unzoomed;
2. repeat it while zoomed with ZoomScale disabled;
3. enable ZoomScale and repeat; and
4. compare the zoom activation method separately.

The result should not be attributed to the zoom method unless the activation
path itself changed.

## Optional MPH smoothing patch

When Metroid.Aim.Disable.MphAimSmoothing is enabled, the patch replaces the
ROM's X and Y smoothing instruction pairs with a branch-over sequence. Each
axis is guarded and restored independently.

| ROM | X pair | Y pair |
| --- | --- | --- |
| JP1.0 / JP1.1 | 0x02029FE0 | 0x0202A008 |
| US1.0 / US1.1 / EU1.1 | 0x0202A004 | 0x0202A02C |
| EU1.0 | 0x02029FFC | 0x0202A024 |
| KR1.0 | 0x02028020 | 0x02028048 |

The exact original words are version-specific and live in the ROM address
table. The replacement is not a blind two-word write:

- the first word must match its version-specific original;
- the second word must match its version-specific original;
- the pair is changed only when both words pass the guard; and
- restore writes only the pair that this patch owns.

This avoids restoring an unrelated modification over one axis. The patch is
match-scoped and participates in the normal configuration-change lifecycle.

## Stylus and native input interactions

Stylus mode is not merely another sensitivity multiplier. In stylus mode the
native aim controls are intentionally bypassed or disabled, including the
ordinary sensitivity/Y/adjust/accumulator/zoom-scaling path and the optional
native aim hooks. Direct stylus transformation remains active. See
[Input compatibility](input-compatibility.md) for the complete interaction
matrix.

Consequently, changing Aim Y scale while stylus mode is active may produce no
visible change. That is expected behavior, not proof that the value failed to
save.

## Verification checklist

- Verify defaults and numeric boundaries in the settings UI.
- Test MPH sensitivity at -3, 0, and a positive value while observing both
  guest sensitivity fields.
- Test aim spin and Y scale independently.
- Test zoom scaling with both zoom methods that are supported by the build.
- Enable smoothing disable and confirm the two guarded pairs change; leave the
  match and confirm the original words are restored.
- Test in stylus mode and confirm which controls are intentionally bypassed.
- Record ROM revision, renderer, input method, and all relevant values when
  comparing results.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimePatchAimSmoothing.cpp
- MelonPrimeInputConfig.cpp
- MelonPrimeDef.h
- MelonPrimeGameRomAddrTable.h

Existing deeper docs:

- docs/features/zoom-aim-sensitivity.md
- docs/features/input/zoom-input-methods.md
- docs/architecture/input/aim-input.md

Supporting reverse-engineering material is maintained separately and is
referenced rather than copied:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\AimHook\current\summary\MelonPrimeDS-AIM-Hook-Implementation-Checklist-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_Commons\current\Widescreen.md
