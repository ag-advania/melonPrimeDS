# Headphone audio setting

## Purpose

Set MPH audio settings to headphones changes the guest's operation/sound byte
so the game uses its headphone output mode. It is a guest menu-data write
intended to be saved by the game.

| Control | Key | Default |
| --- | --- | --- |
| Set MPH audio settings to headphones | Metroid.Apply.Headphone | false |

The settings UI explicitly recommends changing a game setting and saving in
MPH, then unchecking this option. This is a one-way “ensure bits are set”
operation, not a live audio-device selector.

## Byte operation

The helper reads the existing byte and sets bits 3 and 4:

~~~text
new_value = old_value | 0x18
~~~

If both bits are already set, no write occurs. All other bits are preserved.
The implementation does not clear the bits when the checkbox is disabled, so
disabling the host option is not a revert operation.

## ROM addresses

| ROM | Operation/sound byte |
| --- | ---: |
| JP1.0 | 0x020E9998 |
| JP1.1 | 0x020E9958 |
| US1.0 | 0x020E7858 |
| US1.1 | 0x020E8318 |
| EU1.0 | 0x020E8338 |
| EU1.1 | 0x020E83B8 |
| KR1.0 | 0x020E1154 |

These are ARM9 MainRAM addresses selected from
MelonPrimeGameRomAddrTable.h. They are not addresses in the ROM file.

## Lifecycle and saving

The write occurs in the menu/game-settings reconciliation path while the
guest is outside active gameplay. It is not a code patch and has no patch
registry restoration. A guest image reload may remove the runtime change
unless the game has written it to its save data.

Recommended procedure:

1. enable the checkbox;
2. open MPH and change a normal game setting if required to make the game
   commit its settings;
3. save using MPH's own save path;
4. reload and verify the headphone mode; and
5. uncheck the checkbox for ordinary operation.

Do not claim that a PC/host headphone device was selected. The setting changes
the game's stored audio mode only.

## Verification checklist

- Verify the selected ROM address before and after the write.
- Confirm only bits 3 and 4 are added.
- Confirm already-set bits cause no write.
- Save in the guest and verify after reload with the checkbox disabled.
- Test that unrelated operation/sound flags survive.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimeInGame.cpp
- MelonPrimeGameRomAddrTable.h
- InputConfig/MelonPrimeInputConfig.ui

Supporting guest research:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\current\unique_sources\028_9_ControlFlags_Settings-020E9998-Byte-Code-Mapping-Revision-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\README.md
