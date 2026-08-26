# Custom HUD — Adventure Scan Visor

While the Scan Visor is up in Adventure mode the game replaces its regular HUD
with the scan HUD and shows no reticle. The Custom HUD crosshair now hides for
the same span.

Scope is the crosshair only. HP, ammo, weapon inventory, and the radar keep
drawing — that is deliberate, and different from
[adventure camera scenes](custom-hud-adventure-camera-scene.md), where the whole
overlay hides.

## Detection

One byte holds the selected visor. The visor switch writes it, the scan-HUD
dispatcher reads it to choose between the scan and regular HUD paths, and the
regular-HUD component compares it against 0 and early-exits when non-zero — the
same three-way agreement (writer / dispatcher / consumer) that pins the address
down on every version.

```cpp
const bool active = Read8(ram, rom.isInAdventure) == 0x02
                 && Read8(ram, rom.scanVisorState) != 0x00;
```

```text
0 = Combat Visor
1 = Scan Visor
```

`!= 0` rather than `== 1`: the switch only ever writes 0 or 1, but the native
regular-HUD gate tests against 0, so this mirrors the game exactly.

The state flips inside the visor switch itself, so the Custom HUD crosshair
disappears and returns on the same frame boundary as the game's own visor
change. No latch, timer, or delay is needed, and reading live RAM (rather than
hooking the switch) restores correctly after a savestate load.

Not used, and why:

| Candidate | Problem |
|---|---|
| Scan target pointer / ID | The crosshair must also hide on a Scan Visor screen with no target |
| Scan progress timer | Only covers the scanning window, not the whole visor |
| SFX 464/465 playback | Momentary, not a persistent state |
| Visor message ID 107 | Its display timer expires while the visor stays up |

## Addresses

| Version | `scanVisorState` | Game mode byte (`isInAdventure`) |
|---|---:|---:|
| JP1_0 | `020E0568` | `020E9A3C` |
| JP1_1 | `020E0528` | `020E99FC` |
| US1_0 | `020DE690` | `020E78FC` |
| US1_1 | `020DEF10` | `020E83BC` |
| EU1_0 | `020DEF30` | `020E83DC` |
| EU1_1 | `020DEFB0` | `020E845C` |
| KR1_0 | `020D7D13` | `020E11F8` |

KR1_0 is the only version where this is not a standalone global: it is a field
of the HUD/player runtime state block at `020D7D00`, offset `+0x13`. It is read
directly like the others.

This is **not** `isInVisorOrMap` (`020DB0BD` on JP1_0, player-struct relative),
which the input path uses and which also covers the map screen. The map case is
already handled by `isMapOrUserActionPaused`.

## Code

- Address row: `MelonPrimeGameRomAddrTable.h`, `MP_ROM_FIELDS_HUD`
- Read + per-frame cache: `MelonPrimeHudRuntimeSample.inc`
  (`ReadHudRuntimeAdventurePauseState`, alongside the camera-scene flag)
- Visibility predicate: `ShouldDrawCustomHudCrosshair()` in
- `MelonPrimeHudRuntimePolicy.inc`, the single place the crosshair's
  draw/skip decision is made; `CustomHud_Render` consults it once per frame
  and the existing dirty-rect clear path handles the frame it turns off

## Verification status

Address provenance was cross-checked on all seven versions through the visor
switch writer, the SFX 464/465 and HUD message 107/108 call sites on the same
paths, the scan-HUD dispatcher, and the regular-HUD gate. Runtime confirmation
in an actual Scan Visor session has not been performed in-tree.
