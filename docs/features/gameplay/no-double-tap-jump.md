# Weapon-switch jump suppression

MelonPrimeDS advertises a fix that prevents an unintended jump when weapons
are switched rapidly. The implementation is narrower than a global jump
disable: it temporarily suppresses one guest input branch while the legacy
touch-based weapon-switch fallback advances the game, then restores the
original instruction.

This distinction matters when diagnosing a jump. A normal user jump outside a
legacy weapon-switch fallback is not supposed to be blocked by this feature.

## User-visible behavior

The fallback weapon-switch path writes the requested weapon state, advances
the emulated game around a synthetic or current touch input, and checks
whether the requested weapon became active. During those two frame-advance
windows, the patch suppresses the version-specific branch associated with the
0x90000 jump request route. This prevents the weapon-switch gesture from
being interpreted as a jump in the cases covered by the fallback.

The feature has no user-facing checkbox and no independent configuration key.
It is an implementation guard around one weapon-switch route, not a general
gameplay option.

## Which path uses it

The current source has two broad weapon-switch routes:

| Route | No-double-tap patch |
| --- | --- |
| Native weapon-switch request/hook | Not directly; the native path avoids the legacy touch sequence |
| Legacy touch fallback | Applied before the frame advances and restored before returning |

If a native request cannot be serviced and the current code deliberately falls
back to the legacy path, the fallback owns the patch for that invocation. The
patch module itself does not decide whether a weapon request is valid; the
caller has already passed the common weapon ownership, ammo, mode, and
Omega-Cannon checks.

## Runtime sequence

The current SwitchWeaponLegacyTouchFallback sequence is:

1. Read the current jump flags and remember the low jump state.
2. If needed, set the temporary jump flag required by the legacy weapon
   transition.
3. Write the weapon-change request and selected weapon.
4. Apply the ROM-specific no-double-tap instruction replacement.
5. Release the emulated screen and advance two emulated frames.
6. Inject the center touch in mouse mode, or reuse the current touch position
   in stylus mode.
7. Advance two more emulated frames.
8. Restore the ROM-specific original instruction.
9. Restore the saved jump flag when the temporary flag was used.
10. Return whether the guest's current weapon equals the requested weapon.

The apply and restore operations are deliberately paired around the fallback
frame window. The patch is not registered as a persistent battle patch and
does not remain installed between weapon requests.

## ROM-specific patch table

Addresses are ARM9 MainRAM addresses. The ROM group order is the same order
used by the current MelonPrime ROM table.

| ROM group | Patch address | Original/restore word | Temporary word |
| --- | ---: | ---: | ---: |
| JP1.0 | 0x020253B0 | 0x1A000004 | 0xE1A00000 |
| JP1.1 | 0x020253B0 | 0x1A000004 | 0xE1A00000 |
| US1.0 | 0x020253D4 | 0x1A000004 | 0xE1A00000 |
| US1.1 | 0x020253D4 | 0x1A000004 | 0xE1A00000 |
| EU1.0 | 0x020253CC | 0x1A000004 | 0xE1A00000 |
| EU1.1 | 0x020253D4 | 0x1A000004 | 0xE1A00000 |
| KR1.0 | 0x0200E2C4 | 0x1A000004 | 0xE1A00000 |

0xE1A00000 is an ARM NOP. The restore word is the branch instruction
0x1A000004 for every supported ROM group in the current source.

The patch table is maintained in
[MelonPrimePatchNoDoubleTapJump.cpp](../../../src/frontend/qt_sdl/MelonPrimePatchNoDoubleTapJump.cpp).
The caller and its frame sequencing are in
[MelonPrimeGameWeapon.cpp](../../../src/frontend/qt_sdl/MelonPrimeGameWeapon.cpp).

## What the patch does and does not prove

The relevant guest block first tests the 0x90000 request and branches at the
patch point when that request is accepted. The following player+0x39C binding
test is a separate normal jump-binding path and remains in place.

The reverse-engineering evidence therefore supports this precise statement:
the patch suppresses the 0x90000 branch during the legacy weapon-switch
window. It does not prove that every input reaching that branch is
double-tap-only. The current feature name and user-visible goal are retained,
but this document does not turn the label into a stronger guest-semantics
claim.

The patch also does not:

- change the configured jump binding;
- suppress a normal keyboard/controller jump outside the fallback;
- disable the touch boost gesture globally;
- alter the native weapon-switch route;
- write save data; or
- become a persistent entry in the normal patch registry.

## Interaction with other input features

### Stylus mode

Stylus mode changes which touch position the fallback uses. The fallback still
uses the same transient guest guard; it does not mean that the stylus cursor
policy or top-screen touch setting is part of this patch.

### Touch-screen aim only

Touch-screen aim only is a separate guarded multi-site patch. Its addresses
and restore contract are documented in
[input-compatibility.md](../melonprime-settings/input-compatibility.md).
Do not use its successful application as evidence that the weapon-switch
guard was applied.

### Native weapon switching

The native route requests the guest's weapon-equipping function through the
native request path. It does not need the legacy center-touch/frame-advance
sequence, so it does not directly need this transient branch suppression.

## Failure and restoration boundary

The patch module exposes only Apply and Restore and receives the already
resolved ROM group index. It does not perform a ROM-word ownership check in
the patch helper itself and it does not own weapon-request validation.

The caller currently places Restore after the second frame advance. Any future
change that adds an early return, exception-like control transfer, or
asynchronous path between Apply and Restore must preserve the paired
restoration contract. Static review should treat an unpaired apply as a
high-risk guest-state leak.

The global patch lifecycle and registry rules are documented in
[patch-system.md](../../architecture/gameplay/patch-system.md). This feature
is intentionally listed there as an out-of-registry transient patch.

## Reverse-engineering trail

The following mphCodex documents are useful context and are not copied into
this repository:

- C:\Users\Admin\Documents\git\mphCodex\mphAnalysis\Battle\TouchscreenInput\020253B0-double-tap-jump-direct-branch-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\gameFunctionAnalysis\JP1_0\current\020253A0-jump-input-gate-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\function_docs\JP1_0\020253A0-jump-input-gate-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\CallingRawFunctions\changeWeapon\current\summary\WeaponSwitch-NativeCall-Implementation-Notes-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\cheats\codesForMelonPrimeDS\done\Double-Tap Jump Disable patch.txt

The first path is especially important because it explicitly records that
0x020253B0 stops the whole 0x90000 route and is not proof of a
double-tap-only branch. The cheat text is a historical patch artifact; the
current melonPrimeDS source and ROM table are the shipping authority.

## Verification checklist

- Test a normal jump without a weapon-switch request.
- Test rapid weapon switching through the legacy touch fallback in each ROM
  group.
- Test mouse mode and stylus mode separately.
- Test a weapon switch with an active touch and with no active touch.
- Test native weapon switching separately from the fallback.
- Confirm the patch word is restored after a successful switch and a failed
  switch.
- Exercise mode restrictions, unavailable weapons, insufficient ammo, and
  Omega-Cannon restrictions to confirm the common gate still runs first.
- Inspect the post-call instruction or run a focused guest trace if runtime
  evidence is required.
- Report source/static results separately from physical gameplay acceptance.
