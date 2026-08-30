# Use DS firmware name

## Purpose

Use DS Firmware Name clears the guest's “use the internally stored game name”
flag so MPH can use the Nintendo DS firmware name path. It is a guest menu
data write and is separate from the MelonPrime UI language setting.

| Control | Key | Default |
| --- | --- | --- |
| Use firmware name | Metroid.Use.Firmware.Name | false |

The setting is applied only while the menu/game-settings path is eligible. The
UI recommends saving the setting in MPH and then unchecking the option.

## Byte operation

The target byte is dsNameFlagAndMicVolume. Only bit 0 is cleared:

~~~text
new_value = old_value & 0xFE
~~~

Bits 1 through 7, including the neighboring microphone-volume information
held in the same byte, are preserved. If bit 0 is already clear, no write
occurs.

## ROM addresses

| ROM | dsNameFlagAndMicVolume |
| --- | ---: |
| JP1.0 | 0x020E99A9 |
| JP1.1 | 0x020E9969 |
| US1.0 | 0x020E7869 |
| US1.1 | 0x020E8329 |
| EU1.0 | 0x020E8349 |
| EU1.1 | 0x020E83C9 |
| KR1.0 | 0x020E1165 |

These are ARM9 MainRAM addresses from the current ROM table. They must be
selected by ROM revision.

## Interaction with hunter and license options

The DS name flag and license presentation data are separate fields. The
firmware-name change may alter the name displayed in the license/profile
presentation, while the selected hunter and rank-color options alter their
own bytes. In the current UI, the High-Level/HL presentation can also use the
name path; verify the visible result in the actual menu rather than assuming a
particular layout from the checkbox text.

## Lifecycle and saving

The helper does not install executable code and does not restore the old bit
when unchecked. Unchecking stops future writes only. To make the setting
survive a reset, save using the guest's own MPH save flow, then test with the
host option disabled.

## Verification checklist

- Verify bit 0 changes from 1 to 0 and other bits remain unchanged.
- Test all supported ROM revisions.
- Confirm no write occurs when bit 0 is already clear.
- Save in MPH and reload with Use firmware name disabled.
- Check the displayed name and HL/license presentation separately from menu
  localization.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimeInGame.cpp
- MelonPrimeGameRomAddrTable.h

Supporting research:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_JP1_0\current\Language-Setting-All-Versions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\README.md
