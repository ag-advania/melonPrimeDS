# Custom HUD — Adventure camera scenes

Adventure mode plays camera scenes (elevator rides, door/cutscene pans, scripted
looks) during which the game blanks its own HUD, including the crosshair. Custom
HUD used to keep drawing over those scenes. It now hides for the duration.

Hiding here is *not* a hand-back to the native HUD: the NoHUD patch mask,
including the helmet-layer suppression, stays applied exactly as it was. Only
the Custom HUD overlay and the radar overlay stop drawing.

## Detection

The native upper-HUD function reads the currently active `CameraSequence`
instance pointer, tests bit 2 (`0x04`, *BlockInput*) of its first flags byte, and
— when set — clears HUD BG1–BG3 and returns before reaching the crosshair
renderer. Custom HUD reads the same two values, so it follows the game's own
decision rather than a list of scene IDs:

```cpp
const uint32_t sequence = Read32(ram, rom.currentCameraSequence);
const bool blocking = sequence >= 0x02000000u && sequence < 0x02400000u
                   && (Read8(ram, sequence) & 0x04u) != 0u;
```

The pointer is range-checked against main RAM because it comes straight from
emulated memory and can be stale on a transition frame.

Scoped to Adventure: the check runs only when `isInAdventure == 0x02`, which the
HUD runtime already reads, so battle/multiplayer frames pay nothing.

`CameraSequence.Current != 0` alone is too broad — non-blocking sequences leave
the native crosshair up, so they must leave Custom HUD up too.

Flags byte layout at instance `+0x00`:

| bit | mask | meaning |
|---:|---:|---|
| 0 | `0x01` | Complete |
| 1 | `0x02` | CanEnd |
| 2 | `0x04` | BlockInput — gates the upper-HUD early return |
| 3 | `0x08` | ForceAlt |
| 4 | `0x10` | ForceBiped |
| 5 | `0x20` | Loop |

## Addresses

`currentCameraSequence` holds the pointer to the active instance (0 = none). The
same global is written by the sequence-activate function and cleared by the
sequence-end function on every version, which is what rules out a neighbouring
global being misidentified.

| Version | `currentCameraSequence` | Game mode byte (`isInAdventure`) |
|---|---:|---:|
| JP1_0 | `020DBB70` | `020E9A3C` |
| JP1_1 | `020DBB30` | `020E99FC` |
| US1_0 | `020D9CB0` | `020E78FC` |
| US1_1 | `020DA530` | `020E83BC` |
| EU1_0 | `020DA550` | `020E83DC` |
| EU1_1 | `020DA5D0` | `020E845C` |
| KR1_0 | `020D33A4` | `020E11F8` |

KR1_0's upper-HUD prologue and return form are optimised differently, but the
pointer read, flags offset, bit-2 extraction, early return, and crosshair call
order are the same.

## Timing

Read once per frame after the ARM9 game update and before the visibility
decision. `ReadHudRuntimeAdventurePauseState()` does this and stores the result
in the per-frame HUD runtime cache alongside `isAdventure` /
`isMapOrUserActionPaused`, so a sequence that ends mid-frame cannot produce a
pointer/flags pair that disagrees with itself across two reads.

## Code

- Address row: `MelonPrimeGameRomAddrTable.h`, `MP_ROM_FIELDS_HUD`
- Detection + per-frame cache: `MelonPrimeHudRenderRuntime.inc`
  (`IsCameraSequenceBlockingInput`, `ReadHudRuntimeAdventurePauseState`)
- Overlay gate: `MelonPrimeHudRenderMain.inc` (`CustomHud_Render`)
- Radar gate: `CustomHud_ShouldHideForGameplayState` /
  `CustomHud_ShouldDrawRadarOverlay`, used by the GL, Vulkan, and software
  presentation paths

## Verification status

The instruction-level evidence (HUD gate, three-way pointer agreement between
HUD reader / activate writer / end clearer, flags semantics) was cross-checked on
all seven versions. A runtime dump confirming the flag actually being set for a
given scene exists for JP1_0 only; the other versions are covered by detecting
the game's own gate rather than by per-scene data, so regional scene differences
follow that version's real behaviour.
