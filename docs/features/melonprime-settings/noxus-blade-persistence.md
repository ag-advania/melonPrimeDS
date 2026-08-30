# Noxus blade persistence fix

## Purpose

The Noxus blade persistence option fixes the case where Noxus's blade state
does not clear correctly when the local player returns through the relevant
game-state path. It is a guest-code hook, not a visual setting and not a
general weapon unlock.

| Control | Key | Default |
| --- | --- | --- |
| Fix Noxus blade persistence | Metroid.BugFix.FixNoxusBladePersistence | false |

The parent [MelonPrime Settings reference](../melonprime-settings.md) lists
the setting with the other gameplay patches.

## Hook behavior

The hook checks the current player object before touching the blade state:

1. resolve the local CPlayer pointer;
2. require the player pointer to be valid;
3. inspect the hunter ID at player offset 0x400;
4. continue only when the hunter is Noxus, ID 4;
5. clear the blade-persistence field at player offset 0x704 as a 16-bit
   value; and
6. return through the version-specific continuation.

The player guard and hunter-ID guard are essential. The patch must not clear
offset 0x704 for another hunter or for an invalid player object.

## ROM-specific hook sites

| ROM | Hook address | Register used for player pointer |
| --- | --- | --- |
| JP1.0 / JP1.1 | 0x02017D30 | r10 |
| US1.0 | 0x02017D50 | r10 |
| US1.1 | 0x02017D54 | r10 |
| EU1.0 | 0x02017D48 | r10 |
| EU1.1 | 0x02017D54 | r10 |
| KR1.0 | 0x0201A2B4 | r6 |

KR is not an address-only alias. Its player pointer register differs, so the
hook must use the KR-specific stub and continuation.

## Lifecycle and safety

The hook is installed only for the selected ROM and only while the relevant
match/runtime scope is active. Installation is guarded by the expected
original instruction. Disabling the setting or leaving/stopping the match
must remove the hook and restore the original instruction through the patch
registry.

This patch changes a guest runtime field. It does not promise that a previously
saved item, inventory, or unlock state is rewritten. A report that the blade
works after a transition is evidence for the runtime fix only.

## Verification checklist

- Enable the option and test Noxus through the transition that previously
  reproduced the persistence issue.
- Repeat with a different hunter and confirm offset 0x704 is not cleared by
  this hook.
- Test invalid/null player conditions without a crash.
- Verify all ROM-specific hook sites, especially KR's r6 path.
- Disable the option and confirm the original instruction is restored.
- Leave and rejoin a match to verify no stale hook remains.
- Distinguish runtime persistence behavior from saved unlock/inventory data.

## Evidence and related material

Current source:

- MelonPrimePatchFixNoxusBladePersistence.cpp
- MelonPrimePatchRegistry.cpp
- MelonPrimePatchLifecycle.cpp

Supporting research is kept in mphCodex and should be consulted instead of
duplicating the full reverse-engineering narrative here:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\Morph-Ball-Boost-Investigation-JP1_0\current\MelonPrime-Morph-Ball-Boost-Source-Review-JP1_0\patched_source\MelonPrimePatchFixNoxusBladePersistence.cpp
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_JP1_0\current\Player-Data-Struct-JP1_0.md
