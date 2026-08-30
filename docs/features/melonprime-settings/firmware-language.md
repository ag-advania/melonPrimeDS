# Use DS Firmware Language

## Setting contract

| Item | Value |
| --- | --- |
| Key | Metroid.BugFix.UseFirmwareLanguage |
| Default | false |
| Apply location | Out-of-game frame |
| Patch kind | Direct guest byte write; no code cave |
| Source | MelonPrimePatchUseFirmwareLanguage.cpp |

The setting reads the effective user-language bits from the emulated DS
firmware and maps them to the language-slot encoding used by MPH. It changes
the guest's language data field; it is separate from Metroid.UI.MenuLanguage,
which controls the MelonPrime Qt UI.

## Firmware-to-game mapping

The firmware value is masked to three bits:

~~~text
firmwareLanguage = firmware.Settings & 0x07
~~~

| Firmware value | Language | Game language bits |
| ---: | --- | ---: |
| 0 | Japanese | 0x05 |
| 1 | English | 0x00 |
| 2 | French | 0x21 |
| 3 | German | 0x22 |
| 4 | Italian | 0x23 |
| 5 | Spanish | 0x24 |
| 6 | Chinese | 0x00 fallback |
| 7 | Reserved | 0x00 fallback |

Only the lower six bits of the guest byte are language bits. The write
preserves the upper two flags:

~~~text
newValue = (oldValue & ~0x3F) | (mappedLanguage & 0x3F)
~~~

No write is made when the result already equals the current byte.

## ROM address table

| ROM | Guest language byte |
| --- | --- |
| JP1.0 | 0x020E98E8 |
| JP1.1 | 0x020E98A8 |
| US1.0 | 0x020E77A8 |
| US1.1 | 0x020E8268 |
| EU1.0 | 0x020E8288 |
| EU1.1 | 0x020E8308 |
| KR1.0 | 0xFFFFFFFF, unsupported |

The KR sentinel is deliberate. The source comment records an older candidate
at 0x020E10AA, but the current implementation does not write it.

## Region and mode conditions

- EU and US apply in Adventure and multiplayer.
- JP applies in multiplayer but skips the write when the guest
  isInAdventure byte equals 0x02. The UI warns that JP Adventure is not
  playable with the option enabled.
- KR is a no-op.
- The function is called while not in game, so it reconciles the byte every
  out-of-game frame rather than using a one-shot persistent patch state.

The setting does not patch the firmware itself and does not change the
firmware file on disk.

## Verification checklist

- Set each supported firmware language and confirm the mapped lower six bits.
- Preload nonzero upper two flags and confirm they survive the write.
- Confirm no write for Chinese/reserved beyond the English fallback value.
- Confirm JP Adventure skips while JP multiplayer applies.
- Confirm US/EU Adventure applies.
- Confirm KR never writes the sentinel/candidate address.
- Verify guest text and menu behavior on physical/runtime sessions before
  claiming full language acceptance.

## References

- src/frontend/qt_sdl/MelonPrimePatchUseFirmwareLanguage.cpp
- src/frontend/qt_sdl/MelonPrimeGameRomAddrTable.h
- src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/analysis/mphAnalysis/_JP1_0/current/Language-Setting-All-Versions.md
