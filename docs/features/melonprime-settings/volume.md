# SFX and music volume

## Controls

The volume UI uses a value plus an explicit apply checkbox for each channel:

| Control | Key | Default | Range |
| --- | --- | ---: | ---: |
| Apply SFX volume | Metroid.Apply.SfxVolume | false | checkbox |
| SFX volume | Metroid.Volume.SFX | 9 | 0 to 9 in the UI; four-bit guest field |
| Apply music volume | Metroid.Apply.MusicVolume | false | checkbox |
| Music volume | Metroid.Volume.Music | 9 | 0 to 9 in the UI; four-bit guest field |

The values are guest settings. They are not the same as the host mixer
volume, renderer audio latency, or the headphone output mode.

## Encoding

SFX and music share neighboring guest bytes but use different bit contracts.

SFX preserves the upper two bits, stores the four-bit step value in bits 2
through 5, and sets the low two control bits:

~~~text
sfx = (old & 0xC0) | ((steps & 0x0F) << 2) | 0x03
~~~

Music preserves all bits except bits 2 through 5:

~~~text
music = (old & 0xC3) | ((steps & 0x0F) << 2)
~~~

The read-before-write behavior avoids unnecessary guest writes. The step value
is masked to four bits; the settings UI exposes 0 through 9 even though the
underlying field can represent 0 through 15.

## ROM addresses

The operation/sound byte is also used by the headphone setting. The volume
bytes are:

| ROM | Operation/sound | SFX byte | Music byte |
| --- | ---: | ---: | ---: |
| JP1.0 | 0x020E9998 | 0x020E9999 | 0x020E999A |
| JP1.1 | 0x020E9958 | 0x020E9959 | 0x020E995A |
| US1.0 | 0x020E7858 | 0x020E7859 | 0x020E785A |
| US1.1 | 0x020E8318 | 0x020E8319 | 0x020E831A |
| EU1.0 | 0x020E8338 | 0x020E8339 | 0x020E833A |
| EU1.1 | 0x020E83B8 | 0x020E83B9 | 0x020E83BA |
| KR1.0 | 0x020E1154 | 0x020E1155 | 0x020E1156 |

## Lifecycle and saving

The values are reconciled through the guest menu/game-settings path while
outside active gameplay. Turning Apply off prevents future writes but does not
restore the prior volume. To persist the result, use the game's own save
operation and verify after reload.

Changing a host mixer control is not evidence that these guest bytes changed.
Conversely, a successful guest write may not change the host's global mixer.

## Verification checklist

- Test UI steps 0 and 9 for each channel. A raw-field test may additionally
  use 15 to verify the four-bit mask, but 15 is outside the settings UI range.
- Confirm SFX low control bits are set and upper bits are preserved.
- Confirm music changes only its four-bit field.
- Test SFX and music independently and together with headphone mode.
- Save in MPH, reload with Apply disabled, and verify the guest values.
- Record whether the observation concerns guest volume or host mixer volume.

## Evidence and related material

Current source:

- MelonPrimeGameSettings.cpp
- MelonPrimeInGame.cpp
- MelonPrimeGameRomAddrTable.h

Related setting:

- [Headphone audio setting](headphone-audio.md)

Supporting research:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\current\unique_sources\025_9_ControlFlags_0205EAA8-settings-word0-audio-volume-apply-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\current\unique_sources\028_9_ControlFlags_Settings-020E9998-Byte-Code-Mapping-Revision-JP1_0.md
