# Online HEADSHOT notification

## Setting contract

| Item | Value |
| --- | --- |
| Key | Metroid.GameFeature.ShowHeadshotOnline |
| Default | false |
| UI | Show Headshot Notification Online |
| Source | MelonPrimePatchShowHeadshotOnline.cpp |
| Patch kind | One guarded ARM9 word per ROM |

The option forces the standalone HEADSHOT notification path during Wi-Fi or
online matches. It does not calculate headshot eligibility and does not alter
damage; it changes the guest branch that suppresses the standalone display in
the online path.

## Patch values

Every supported ROM changes its site to NOP:

~~~text
apply = 0xE1A00000
revert = 0x1A000016
~~~

| ROM | Patch address |
| --- | --- |
| JP1.0 | 0x0201748C |
| JP1.1 | 0x0201748C |
| US1.0 | 0x020174AC |
| US1.1 | 0x020174B0 |
| EU1.0 | 0x020174A4 |
| EU1.1 | 0x020174B0 |
| KR1.0 | 0x02019A44 |

The common StaticWordPatch helper accepts only the expected revert or already
applied value and restores only the matching applied word. A foreign value
causes the patch to be rejected.

## Lifecycle

The registry applies the patch at BattleRuntime and ConfigReload. It restores
on match leave and emulator stop. If the option is off, the apply path calls
the restore path so a previously applied word is removed.

The patch is local to the emulator's guest image. Other players do not need to
enable it to see their own local notification, but the normal network
authority still controls game events.

## Verification checklist

- Validate the original word on all seven ROM groups.
- Test enabled/disabled transitions at battle entry and config reload.
- Confirm match leave restores the exact original word.
- Test online and local wireless separately; the feature is intended for the
  online notification path.
- Capture the visible standalone notification, not only a successful patch
  application log.

## References

- src/frontend/qt_sdl/MelonPrimePatchShowHeadshotOnline.cpp
- src/frontend/qt_sdl/MelonPrimePatchRegistry.cpp
- C:/Users/Admin/Documents/git/mphCodex/mnt/data/cheats/codesForMelonPrimeDS/done/Headshot WiFi patch.txt
