# MelonPrime Settings: detailed references

This directory contains the implementation-level reference for the controls
on the MelonPrime Settings tab. The parent document
../melonprime-settings.md is the compact inventory; this directory is the
place to record one setting or one tightly coupled patch in detail.

Each document uses the same evidence boundary:

- behavior, keys, defaults, and lifecycle are taken from the current
  melonPrimeDS source;
- guest addresses are absolute ARM9 MainRAM addresses and are grouped by ROM
  revision;
- source guards and restore behavior are described explicitly;
- mphCodex is cited as reverse-engineering context, not as authority over a
  conflicting current source value; the paths below are real files in the
  sibling checkout, not duplicated copies;
- static/source evidence is not presented as physical-runtime acceptance.

## Reference map

| Settings area | Detailed reference |
| --- | --- |
| Menu Language | [menu-language.md](menu-language.md) |
| Collapsible settings sections | [section-disclosure.md](section-disclosure.md) |
| MPH and native aim sensitivity | [sensitivity.md](sensitivity.md) |
| Aim Follow Mode, FPS Camera Lock, and native aim modes | [aim-follow.md](aim-follow.md) |
| Morph Ball swipe boost | [morph-ball-boost.md](morph-ball-boost.md) plus [existing feature reference](../morph-ball-boost.md) |
| Wi-Fi active friend/rival bitset | [wifi-bitset.md](wifi-bitset.md) |
| Wi-Fi reconnect Error 52200 | [existing feature reference](../gameplay/wifi-reconnect-52200.md) |
| Shadow Freeze | [shadow-freeze.md](shadow-freeze.md) (the existing runtime-hook note is linked as history) |
| Noxus Blade persistence | [noxus-blade-persistence.md](noxus-blade-persistence.md) |
| DS firmware language | [firmware-language.md](firmware-language.md) |
| Online HEADSHOT notification | [online-headshot.md](online-headshot.md) |
| Online enemy HP meter | [online-enemy-hp.md](online-enemy-hp.md) |
| Stage/mode matrix expansion | [stage-matrix.md](stage-matrix.md) |
| Disable Double Damage Multiplier | [damage-multiplier.md](damage-multiplier.md) |
| Damage Notify Purple | [damage-notify-purple.md](damage-notify-purple.md) |
| Power-Up Pickup Effects | [powerup-pickup.md](powerup-pickup.md) |
| SnapTap | [snaptap.md](snaptap.md) |
| Unlock save data | [save-data-unlock.md](save-data-unlock.md) |
| Headphone audio setting | [headphone-audio.md](headphone-audio.md) |
| DS name | [ds-name.md](ds-name.md) |
| Video quality presets | [video-presets.md](video-presets.md) |
| SFX and music volume | [volume.md](volume.md) |
| Hunter License hunter/color | [hunter-license.md](hunter-license.md) |
| Joy2Key, Stylus, and touch behavior | [input-compatibility.md](input-compatibility.md) |
| Weapon, transform, fire, and zoom methods | [input-methods.md](input-methods.md) |
| Screen synchronization | [screen-sync.md](screen-sync.md) |
| Cursor clip and in-game layout | [cursor-layout.md](cursor-layout.md) |
| In-game aspect ratio | [aspect-ratio.md](aspect-ratio.md) |
| Low HP warning | [low-hp-warning.md](low-hp-warning.md) |
| Developer-only aim/input hooks | [developer-hooks.md](developer-hooks.md) |

The existing references linked above are intentionally retained as the
canonical deep dives where they already contain implementation history. The
new documents add the missing settings-specific explanations and cross-links.

## Source map

The primary UI/configuration sources are:

- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.ui
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp
- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfigConfig.cpp
- src/frontend/qt_sdl/MelonPrimeHudPropSchema.inc
- src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp
- src/frontend/qt_sdl/MelonPrimeArm9Hook.cpp
- src/frontend/qt_sdl/MelonPrimeGameSettings.cpp
- src/frontend/qt_sdl/MelonPrimeDef.h
- src/frontend/qt_sdl/Config.cpp

Patch ownership is split across MelonPrimePatch*.cpp, the ARM9 hook include
fragments, MelonPrimePatchRegistry.cpp, and MelonPrimeGameRomAddrTable.h.
