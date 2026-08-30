# Input compatibility and stylus controls

## Controls

These controls select the input source and compatibility behavior around the
native MelonPrime aim path:

| Control | Key | Default |
| --- | --- | --- |
| Joy2Key compatibility support | Metroid.Apply.joy2KeySupport | true |
| Stylus mode | Metroid.Enable.stylusMode | false |
| Top-screen touch | Metroid.Enable.topScreenTouch | false |
| Touch-screen aim only | Metroid.Enable.touchScreenAimOnly | false |
| Hide stylus cursor in game | Metroid.Enable.stylusHideCursorInGame | false |
| Direct alt-form transform | Metroid.Input.Enable.DirectAltFormTransform | false |

The controls are not interchangeable:

- Joy2Key adapts input through the compatibility/remapper path.
- Stylus mode selects touch/stylus aim semantics and disables incompatible
  native aim controls.
- Top-screen touch enables a UI routing path.
- Touch-screen aim only is a guarded guest hit-test patch.
- Hide stylus cursor is presentation-only.
- Direct alt-form transform selects a native guest transform hook.

## Joy2Key compatibility support

Joy2Key support is enabled by default for compatibility with the existing
keyboard/controller mapping path. It may add an input conversion step and
therefore can affect latency or edge ordering. Disabling it is appropriate
only when the deployment uses the normal MelonPrime input path and does not
need the remapper behavior.

When investigating SnapTap, immediate edges, or native weapon/zoom input,
record this flag. An apparent input-method regression may be a compatibility
path difference instead.

## Stylus mode

Stylus mode changes the source and interpretation of aim input. When enabled,
the UI disables or bypasses the ordinary controls that do not apply to stylus
aim, including:

- Aim spin/sensitivity;
- Aim Y scale;
- Aim adjust;
- aim accumulator;
- zoom sensitivity scaling;
- low-latency/native aim hook controls; and
- legacy MPH smoothing controls where the selected stylus path does not use
  them.

The direct stylus transform path remains active. Thus a disabled spin control
does not mean stylus aim is disabled; it means the normal mouse/controller
scaling path is not being used.

Stylus mode and Touch-screen aim only are also different. Stylus mode is a
host input mode. Touch-screen aim only patches a guest control hit-test so
touch input is treated as the aim source. They may be used together, but one
must not be used as evidence that the other is working.

## Top-screen touch and cursor presentation

Top-screen touch is a GUI routing option. It changes where touch input is
accepted in the rendered layout; it does not patch the guest's weapon or
camera code.

Hide stylus cursor in game calls the screen cursor policy. It changes the
visible cursor presentation while the relevant in-game stylus state is active.
It does not disable cursor capture, change aim deltas, or write a ROM byte.
The policy restores the ordinary cursor presentation when the conditions end.

## Touch-screen aim only patch

The patch writes mov r0,#0, value 0xE3A00000 at three guarded call/hit-test
sites per ROM. The exact original call words are version-specific and are
stored in MelonPrimePatchTouchScreenAimOnly.cpp.

| ROM | Site 1 | Site 2 | Site 3 |
| --- | ---: | ---: | ---: |
| JP1.0 / JP1.1 | 0x02026C70 | 0x02026E40 | 0x02026F70 |
| US1.0 | 0x02026C94 | 0x02026E64 | 0x02026F94 |
| US1.1 / EU1.1 | 0x02026C94 | 0x02026E64 | 0x02026F94 |
| EU1.0 | 0x02026C8C | 0x02026E5C | 0x02026F8C |
| KR1.0 | 0x0200C308 | 0x0200C4C8 | 0x0200C5E8 |

Apply requires every site to match its ROM-specific original or already
applied value. Restore requires the replacement value and writes back only
the owned site words. The registry applies the patch on battle runtime/config
reload and restores it on leave/stop.

The double-tap gesture and touch boost are separate behavior and are not
covered by these three addresses.

## Compatibility matrix

| Combination | Expected interpretation |
| --- | --- |
| Normal input, stylus off | Ordinary sensitivity/native settings apply |
| Stylus on | Stylus transform owns aim; many normal aim controls are bypassed |
| Touch-only on, stylus off | Guest touch hit-test patch plus normal host routing |
| Stylus on + touch-only on | Two distinct layers; test both independently |
| Hide cursor on | Presentation change only |
| Joy2Key on | Compatibility/remapper path remains in the chain |

## Verification checklist

- Record all six keys before comparing input behavior.
- Test stylus mode with the controls that the UI disables.
- Test touch-only on every ROM in the patch table.
- Verify guard failure and leave/stop restoration.
- Test top-screen touch with each screen layout used by the deployment.
- Confirm cursor hiding does not change aim deltas.
- Compare Joy2Key on/off using the same physical input source.

## Evidence and related material

Current source:

- MelonPrimeInputConfig.cpp
- MelonPrimePatchTouchScreenAimOnly.cpp
- MelonPrimeScreenCursorPolicy.cpp
- MelonPrimeInputProjection.h
- MelonPrimeDef.h

Deep existing documentation:

- docs/architecture/input/aim-input.md
- docs/architecture/gameplay/patches/native-aim-delta-register-injection.md
- docs/architecture/gameplay/patches/immediate-input-edge-overlay.md

The guest touch/aim research is maintained separately:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Control\current\Player-Control-Preset-Dumps-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md
