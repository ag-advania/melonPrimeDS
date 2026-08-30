# Unlock all maps and hunters

## Purpose

Unlock All writes the guest's menu/save-related unlock fields so maps and
hunters are available to the game. It is a runtime reconciliation helper, not
a generic save-file editor and not a network entitlement bypass.

| Control | Key | Default |
| --- | --- | --- |
| Unlock all maps and hunters | Metroid.Data.Unlock | false |

The setting is intentionally applied outside the active gameplay path. The
UI text instructs the user to save in MPH after the desired state is visible,
then uncheck the option. Without that save step, a later save/load or guest
reset may discard the runtime-only write.

## Values written

The implementation receives five ROM-specific addresses. It reads before
writing and only changes fields that are not already in the unlocked state:

| Field | Width | Required value |
| --- | ---: | ---: |
| Unlock byte 1 | 8-bit | low bits include 0x03 |
| Unlock word 2 | 32-bit | 0x07FFFFFF |
| Unlock byte 3 | 8-bit | 0x7F |
| Unlock word 4 | 32-bit | 0xFFFFFFFF |
| Unlock byte 5 | 8-bit | 0x7F |

The first field uses OR semantics so unrelated upper bits are preserved:

~~~text
new_byte_1 = old_byte_1 | 0x03
~~~

The other fields are written to their explicit target values. The helper
returns whether any field changed, but repeated frames with already-unlocked
values are no-ops.

## ROM addresses

The table below is the current ARM9 MainRAM table used by the implementation.
The five offsets are consecutive or near-consecutive fields in the guest menu
state; they are not universal addresses across ROM revisions.

| ROM | Field 1 | Field 2 | Field 3 | Field 4 | Field 5 |
| --- | ---: | ---: | ---: | ---: | ---: |
| JP1.0 | 0x020E9999 | 0x020E999C | 0x020E99A0 | 0x020E99A4 | 0x020E99A8 |
| JP1.1 | 0x020E9959 | 0x020E995C | 0x020E9960 | 0x020E9964 | 0x020E9968 |
| US1.0 | 0x020E7859 | 0x020E785C | 0x020E7860 | 0x020E7864 | 0x020E7868 |
| US1.1 | 0x020E8319 | 0x020E831C | 0x020E8320 | 0x020E8324 | 0x020E8328 |
| EU1.0 | 0x020E8339 | 0x020E833C | 0x020E8340 | 0x020E8344 | 0x020E8348 |
| EU1.1 | 0x020E83B9 | 0x020E83BC | 0x020E83C0 | 0x020E83C4 | 0x020E83C8 |
| KR1.0 | 0x020E1155 | 0x020E1158 | 0x020E115C | 0x020E1160 | 0x020E1164 |

These are guest RAM addresses for the loaded ROM image, not file offsets.

## Lifecycle and save semantics

MelonPrimeInGame calls the reconciliation helper while the menu/game-settings
path is eligible. The helper does not patch executable instructions and has no
static patch restore callback. Turning the checkbox off stops future writes; it
does not restore the previous locked values.

Because the writes are intended to be saved by the game, users should:

1. enable Unlock All;
2. enter the relevant MPH menu;
3. confirm maps/hunters are visible;
4. use the game's own save operation;
5. verify the save completed; and
6. disable Unlock All before normal play.

The exact menu/save behavior is guest-version dependent. A successful memory
write alone is not proof that a save file was updated.

## Verification checklist

- Check all five values before and after enabling.
- Confirm repeated reconciliation does not rewrite stable values.
- Test each ROM revision with its own table.
- Save in the guest and reload with the option disabled.
- Confirm unrelated upper bits of field 1 remain unchanged.
- Verify the feature does not alter executable code or online match state.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimeInGame.cpp
- MelonPrimeGameRomAddrTable.h

Supporting research:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\README.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\current\04_SaveFlags-Patch-Reference-JP1_0-v13.md

The source table remains authoritative for this build; research addresses
from another dump must not be substituted.
