# Disable double-damage multiplier

## Purpose and dependency

Disable Double Damage Multiplier prevents the guest's double-damage multiplier
from being applied through the supported battle code path. It is a static
instruction patch and is independent from the purple damage notification.
Damage Notify Purple is enabled only when this option is also enabled, so the
two settings must be recorded together in test reports.

| Control | Key | Default |
| --- | --- | --- |
| Disable double-damage multiplier | Metroid.GameFeature.DisableDoubleDamageMultiplier | false |
| Damage notify purple | Metroid.GameFeature.DamageNotifyPurple | false |

## Patch shape

The patch replaces eight guarded words at scattered code locations. They are
not one contiguous eight-word span:

| ROM | Eight addresses in source order |
| --- | --- |
| JP1.0 / JP1.1 | 0x020173D4, 0x0200AFC8, 0x0200AFD4, 0x0202409C, 0x020240A8, 0x020414B8, 0x020414C4, 0x020414D0 |
| US1.0 | 0x020173F4, 0x0200AFC8, 0x0200AFD4, 0x020240C0, 0x020240CC, 0x02041370, 0x0204137C, 0x02041388 |
| US1.1 / EU1.1 | 0x020173F8, 0x0200AFC8, 0x0200AFD4, 0x020240C0, 0x020240CC, 0x020412A0, 0x020412AC, 0x020412B8 |
| EU1.0 | 0x020173EC, 0x0200AFCC, 0x0200AFD8, 0x020240B8, 0x020240C4, 0x02041298, 0x020412A4, 0x020412B0 |
| KR1.0 | 0x02019990, 0x0200F614, 0x0200F620, 0x02027774, 0x02027780, 0x0203A6C0, 0x0203A6CC, 0x0203A6D8 |

For JP/US/EU the first word changes from 0x11A00889 to 0x11A00809 and the
other seven words change from 0xE1A00080 to 0xE1A00000. KR uses
0x11A00885 to 0x11A00805 for its first word; its other seven words use the
same E1A00080 to E1A00000 transition. The apply and restore arrays are
maintained in MelonPrimePatchDisableDoubleDamageMultiplier.cpp. Do not infer
the complete array from the first word; every address participates in the
guard.

The patch uses a static-word-span contract:

- all eight words must match the expected original sequence before apply;
- an already-applied sequence is recognized as idempotent;
- a mismatched mixed state fails closed;
- apply writes the complete replacement sequence; and
- restore writes the complete original sequence only when the current span is
  owned by this patch.

This prevents a partial or stale patch from silently overwriting another
version's code.

## Lifecycle

The registry applies the patch for battle runtime/config reload and restores it
on leave and stop. A configuration toggle while no match is active may only
change the pending setting; acceptance requires observing the actual
match-scoped lifecycle.

A process restart or guest image reload can also remove the patch, but that is
not a substitute for testing the explicit registry restore path.

## Gameplay interpretation

The patch affects damage calculation in the guest code path. It does not
guarantee a particular HUD number, kill result, or opponent-side behavior.
Online observations must identify whether the measured value is local,
remote, predicted, or authoritative. Test both ordinary hits and any hunter
or weapon whose damage path was previously reported as exceptional.

Damage Notify Purple is a host-side notification based on observed damage
events. It is not proof that the multiplier patch changed the authoritative
server calculation.

## Verification checklist

- Confirm all eight original words for each ROM before enabling.
- Confirm the full eight-word replacement after enabling.
- Test a mixed/mismatched span and verify no partial write occurs.
- Verify idempotent re-apply does not rewrite an already-applied span.
- Verify leave, stop, and config reload behavior.
- Test with Damage Notify Purple disabled and enabled separately.
- Record local/remote/authoritative interpretation for online results.

## Evidence and related material

Current source:

- MelonPrimePatchDisableDoubleDamageMultiplier.cpp
- MelonPrimePatchRegistry.cpp
- MelonPrimeDamageNotifyPurple.cpp

Supporting reverse-engineering material:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\DoubleDamage\current\DoubleDamage-Integrated-AllVersions-v7\04_Double-Damage-Patch-Reference-AllVersions-v7.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\DoubleDamage\current\Player-Double-Damage-DeepDive-JP1_0.md

The current source table and guard are authoritative if the research report
uses an older address or an older ROM image.
