# MelonPrimeDS Zoom Input Methods

## Overview

MelonPrimeDS currently has three zoom input methods.

| UI state | Config value | Runtime mode | Behavior |
| --- | ---: | --- | --- |
| both zoom boxes unchecked | `0` | Legacy | Always press the fixed DS `R` bit for zoom. |
| `Use New Method for Zoom` checked | `1` | New | Read the in-game zoom binding at `player+0x3E0` and press that DS input bit. |
| `Use New Method 2 for Zoom` checked | `2` | New2 | Toggle native zoom state by calling `SetPlayerScopeZoom(player, enabled)` through an ARM9 trampoline. |

`Use New Method for Zoom` and `Use New Method 2 for Zoom` are mutually exclusive checkboxes. If both are off, the saved method is Legacy.

Relevant config:

```cpp
Metroid.Input.ZoomMethod
0 = Legacy fixed R
1 = New preset binding
2 = New native toggle
```

The default is `0` and the config range is `0..2`.

## Runtime Flags

`ReloadConfigFlags()` maps `Metroid.Input.ZoomMethod` to hot runtime booleans.

```cpp
m_enableNewZoomInputMethod = (ZoomMethod == 1);
m_enableNativeZoomToggle   = (ZoomMethod == 2);
```

Legacy is the state where both of those booleans are false.

When New2 is disabled, any native zoom pending call and previous button-down latch are cleared so a mode switch cannot consume stale input.

## Shared Input Flow

Zoom is projected from the host hotkey state into `IB_ZOOM` in `ProjectDownState()`.

`ProcessMoveAndButtonsFastImpl()` deliberately only merges `B` and `L`. It does not merge `R` anymore, because zoom can be preset-dependent and is applied separately in `ApplyZoomBindingInput()`.

Normal per-frame flow:

```text
UpdateInputState()
  -> ProjectDownState() sets IB_ZOOM
ProcessMoveAndButtonsFastFromReset()
  -> movement + B/L
ApplyZoomBindingInput()
  -> Legacy/New/New2 zoom behavior
```

The same zoom application is also used by the re-entrant frame-advance path.

## Legacy Method

Legacy mode keeps the old fixed-button behavior.

```cpp
zoomMask = 1 << INPUT_R;
m_inputMaskFast &= ~zoomMask;
```

`m_inputMaskFast` is DS input state where cleared bits mean pressed. Holding the external zoom button therefore holds DS `R`.

If `ImmediateInputEdgeOverlay` is enabled, the overlay also treats zoom as fixed `INPUT_R` in Legacy mode.

## New Method

New Method still acts like normal input, but it reads the game's current control-preset binding instead of assuming `R`.

`ApplyZoomBindingInput()` starts with fixed `R`, then replaces it with the in-game zoom binding when possible.

```cpp
zoomMask = 1 << INPUT_R;
if (m_enableNewZoomInputMethod && inGame) {
    zoomMask = Read16(player + 0x3E0) & 0x0FFF;
}
m_inputMaskFast &= ~zoomMask;
```

This makes Touch R/L and Dual R/L presets work with their own zoom binding. If the binding read fails or returns `0`, it falls back to fixed `R`.

If `ImmediateInputEdgeOverlay` is enabled, it uses the same idea and reads `player+0x3E0` for the zoom mask before writing the game input struct.

## ImmediateInputEdgeOverlay Interaction

`MelonPrimePatchImmediateInputEdgeOverlay.inc` is a side-effect ARM9 hook. It does not redirect PC. It runs at the action consumer entry, after the game has polled input and before Fire / Jump / Zoom / Movement handlers consume the input struct.

The hook reads the current player's binding fields:

| Offset | Action |
| ---: | --- |
| `+0x368` | move left |
| `+0x36C` | move right |
| `+0x370` | move forward |
| `+0x374` | move backward |
| `+0x398` | fire |
| `+0x39C` | jump |
| `+0x3E0` | zoom |

Then it overlays emulator-side held / pressed / released bits into `player+0x464`.

Zoom mask selection inside the overlay:

```cpp
if (m_enableNativeZoomToggle)
    zoomMask = 0;
else if (m_enableNewZoomInputMethod)
    zoomMask = ReadBindingLow(player + 0x3E0);
else
    zoomMask = 1 << INPUT_R;
```

So New2 removes zoom from the overlay path entirely. Movement, fire, and jump can still be overlaid, but zoom is handled only by the native setter toggle path.

## New Method 2

New2 is native toggle mode. It does not hold an input bit and does not use `player+0x3E0`.

The external zoom button is treated as a pressed-edge toggle:

```text
down now = true, down prev = false -> one toggle request
held     = true, down prev = true  -> no repeated toggles
released                         -> no action
```

On a pressed edge:

```text
1. read local player from ROM-specific global pointer
2. read current zoom state from player+0x850 bit0
3. desired = !current
4. if desired=true, require current weapon zoom-capable:
   player+0x858 -> weapon
   weapon+0x08 & 0x00000800
5. queue native setter call:
   SetPlayerScopeZoom(player, desired ? 1 : 0)
```

Zoom-off is allowed even if the current weapon is no longer zoom-capable. This is a failsafe for stale scope state after weapon changes or odd game states.

New2 also has an idle failsafe:

```text
if current zoom is enabled
and current weapon is not zoom-capable
then queue SetPlayerScopeZoom(player, 0)
```

It never writes `player+0x850` or `player+0x648` directly. Scope flags, SFX, HUD, crosshair, and FOV progression are left to the game's native setter/update code.

## New2 ARM9 Hook

New2 registers a ROM-specific `WeaponActionUpdate` hook address through `MelonPrimeArm9Hook`.

The dispatcher order checks `Dispatch_NativeZoomToggle` before `Dispatch_ImmediateInputEdgeOverlay`. This matters because several versions share the same action-consumer address. If a native zoom pending call exists, the zoom redirect happens first. After the trampoline returns to the original hook address with pending cleared, the original instruction path can continue and side-effect hooks can run normally.

### Address Table

| Version | LocalPlayerPtrGlobal | SetPlayerScopeZoom | WeaponActionUpdate |
| --- | ---: | ---: | ---: |
| JP1_0 | `020BE790` | `02015C98` | `02024174` |
| JP1_1 | `020BE750` | `02015C98` | `02024174` |
| US1_0 | `020BCA70` | `02015CB8` | `02024198` |
| US1_1 | `020BD2D0` | `02015CBC` | `02024198` |
| EU1_0 | `020BD2F0` | `02015CB0` | `02024190` |
| EU1_1 | `020BD370` | `02015CBC` | `02024198` |
| KR1_0 | `020B6240` | `0201CEBC` | `0200D07C` |

### Trampoline

New2 uses an ARM9 trampoline:

```text
trampoline = 02003F00
scratch    = 02003F40
```

Scratch layout:

| Offset | Value |
| ---: | --- |
| `+0x00` | player pointer |
| `+0x04` | enabled, low byte used |
| `+0x08` | SetPlayerScopeZoom address |
| `+0x0C` | return address, currently WeaponActionUpdate hook address |

Trampoline flow:

```text
save r0-r12, lr
r0 = scratch.player
r1 = scratch.enabled
r3 = scratch.setter
blx r3
restore r0-r12, lr
branch back to scratch.returnAddr
```

The pending request is cleared before redirecting to the trampoline. When the trampoline branches back to the hook address, the second dispatcher pass sees no pending request and falls through to the game's original instruction stream.

## Method Comparison

| Method | Uses DS input bit | Uses game binding | Uses ARM9 native setter | Toggle edge only | Main risk |
| --- | --- | --- | --- | --- | --- |
| Legacy | yes, fixed `R` | no | no | no | wrong preset on Dual-style controls |
| New | yes, `player+0x3E0` | yes | no | no | depends on binding field being valid |
| New2 | no | no | yes | yes | trampoline/code-cave correctness |

## Files

| File | Role |
| --- | --- |
| `MelonPrimeDef.h` | `ZoomMethod` config key and `0/1/2` constants |
| `Config.cpp` | default value and range |
| `InputConfig/MelonPrimeInputConfig.cpp` | INPUT METHOD UI checkboxes and mutual exclusion |
| `InputConfig/MelonPrimeInputConfigConfig.cpp` | save `ZoomMethod` |
| `MelonPrime.cpp` | load runtime flags |
| `MelonPrimeGameInput.cpp` | normal zoom input routing and include point for zoom hook |
| `MelonPrimePatchImmediateInputEdgeOverlay.inc` | overlay behavior for Legacy/New, zoom disabled for New2 |
| `MelonPrimePatchNativeZoomToggleHook.inc` | New2 native toggle implementation |
| `MelonPrimeArm9Hook.cpp` | ARM9 dispatch mask and address registration |

## Quick Tests

Legacy:

```text
1. uncheck both Zoom method boxes
2. hold zoom button
3. verify fixed DS R path behaves like the old build
```

New:

```text
1. check Use New Method for Zoom
2. switch between Touch/Dual presets
3. verify zoom follows player+0x3E0 binding
```

New2:

```text
1. check Use New Method 2 for Zoom
2. equip Imperialist
3. press zoom once: scope state should enable
4. hold zoom: state should not repeatedly flip
5. press zoom again: scope state should disable
6. switch to a non-zoom weapon while zoomed: failsafe should queue zoom off
```

Useful memory checks for New2:

```text
player+0x850 bit0 = native zoom enabled state
player+0x858      = current weapon pointer
weapon+0x08 bit 0x800 = zoom-capable flag
```
