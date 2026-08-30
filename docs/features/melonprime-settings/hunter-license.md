# Hunter and license color

## Controls

These controls alter the guest's selected hunter and license/rank color fields.
They are menu/save-data writes, not runtime player-object hooks.

| Control | Key | Default |
| --- | --- | --- |
| Apply selected hunter | Metroid.HunterLicense.Hunter.Apply | false |
| Selected hunter | Metroid.HunterLicense.Hunter.Selected | 0 |
| Apply selected license color | Metroid.HunterLicense.Color.Apply | false |
| Selected license color | Metroid.HunterLicense.Color.Selected | 0 |

The combo index is converted to guest bit fields. The index meanings belong to
the current UI list and must not be replaced with a raw hunter ID without
checking the list order.

## Bit operations

Hunter selection uses bits 3 through 6 of the mainHunter byte:

~~~text
new_hunter = (old & 0x87) | (selected * 0x08 & 0x78)
~~~

License color uses the upper two bits of rankColor while preserving the lower
six bits:

~~~text
new_color = (old & 0x3F) | color_bits
~~~

The implementation performs a read-before-write and does nothing when the
result equals the current byte. It does not overwrite unrelated flags.

## ROM addresses

| ROM | mainHunter | rankColor |
| --- | ---: | ---: |
| JP1.0 | 0x020ECF40 | 0x020ECF43 |
| JP1.1 | 0x020ECF00 | 0x020ECF03 |
| US1.0 | 0x020EAE00 | 0x020EAE03 |
| US1.1 | 0x020EB8C0 | 0x020EB8C3 |
| EU1.0 | 0x020EB8E0 | 0x020EB8E3 |
| EU1.1 | 0x020EB960 | 0x020EB963 |
| KR1.0 | 0x020E44BC | 0x020E44BF |

These are ARM9 MainRAM addresses. The four-byte spacing between mainHunter
and rankColor is part of the guest structure and should not be collapsed into
a one-byte assumption.

## Lifecycle and saving

The writes occur through the menu/game-settings reconciliation path. They are
not executable patches, so patch-registry restoration is not expected.
Disabling Apply prevents future writes but does not restore the previous
hunter/color selection.

For persistence, save in MPH after the desired selection is visible, then
disable the host option and reload. Data Unlock and Use DS Firmware Name can
change the menu presentation independently; test them separately.

## Verification checklist

- Verify selected index-to-bit mapping for every combo entry.
- Confirm only hunter bits 3 through 6 change.
- Confirm only color bits 6 and 7 change.
- Test both Apply controls independently.
- Save and reload with the host options disabled.
- Test all ROM revisions and confirm unrelated flags remain intact.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimeInGame.cpp
- MelonPrimeGameRomAddrTable.h

Supporting research:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_JP1_0\current\License-Data-Struct-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\README.md
