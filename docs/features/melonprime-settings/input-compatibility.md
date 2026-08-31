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
| Temporarily restore HUD hit-testing for a Standard transform | Metroid.Enable.touchScreenAimOnlySuspendForTransform | true |
| Hide stylus cursor in game | Metroid.Enable.stylusHideCursorInGame | false |
| Confine stylus cursor to top screen | Metroid.Enable.stylusConfineCursorToTopScreen | false |
| Hold stylus cursor at center while not clicking | Metroid.Enable.stylusHoldCursorAtCenterWhenNotClicking | false |

The controls are not interchangeable:

- Joy2Key adapts input through the compatibility/remapper path.
- Stylus mode selects touch/stylus aim semantics and disables incompatible
  native aim controls.
- Top-screen touch enables a UI routing path.
- Touch-screen aim only is a guarded guest hit-test patch and is applied only
  while Stylus Mode is also enabled.
- Its indented Standard-transform option briefly restores the Transform-button
  hit-test around the legacy simulated tap, then reapplies the owning patch.
- Hide, top-screen confinement, and hold-at-center are host cursor
  policies; their complete contract is in
  [stylus-cursor-policy.md](stylus-cursor-policy.md).

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

Stylus mode and Touch-screen aim only remain different layers, but the current
patch deliberately requires both configuration values. Stylus Mode is the host
input mode; Touch-screen aim only patches three guest HUD hit-tests. A checked
child value retained while Stylus Mode is off is inactive rather than an
independent patch request.

## Top-screen touch and cursor presentation

Top-screen touch is a GUI routing option. It changes where touch input is
accepted in the rendered layout; it does not patch the guest's weapon or
camera code.

Hide stylus cursor in game calls the screen cursor policy. It changes the
visible cursor presentation while the relevant in-game stylus state is active.
It does not disable cursor capture, change aim deltas, or write a ROM byte.
The policy restores the ordinary cursor presentation when the conditions end.

Top-screen confinement and hold-at-center are related but independent options.
Confinement limits the host pointer to the rendered top-screen rectangle on
Windows without requesting mouse-look capture. Hold-at-center keeps the host
pointer at the selected screen center while no stylus click is held, so every
drag starts with its full range. Neither option changes guest touch
coordinates or installs the touch-screen aim-only patch.
See [stylus-cursor-policy.md](stylus-cursor-policy.md) for the state gate,
combination matrix, and platform boundary.

## Touch-screen aim only patch

The complete patch words, parent/child state matrix, Standard-transform
bracketing sequence, lifecycle, and mphCodex research pointers are maintained
in [Touch-screen aim only](touch-screen-aim-only.md). This page keeps only the
compatibility relationship so the ROM table has one owner.

## Compatibility matrix

| Combination | Expected interpretation |
| --- | --- |
| Normal input, stylus off | Ordinary sensitivity/native settings apply |
| Stylus on | Stylus transform owns aim; many normal aim controls are bypassed |
| Touch-only checked, stylus off | Saved parent value is retained, but the guest patch is restored/inactive |
| Stylus on + touch-only on | Stylus host mode plus the three-site guest hit-test patch |
| Standard transform + temporary exception on | Patch is restored for the simulated Transform tap, then reapplied |
| Hide cursor on | Presentation change only |
| Top-screen confinement on | Windows host clip in the active stylus state; not aim capture |
| Hold cursor at center on | Host warp while no stylus click is held; active drags are not continuously recentered |
| Joy2Key on | Compatibility/remapper path remains in the chain |

## Verification checklist

- Record all eight controls before comparing input behavior.
- Test stylus mode with the controls that the UI disables.
- Test touch-only and its Standard-transform exception on every ROM in the
  dedicated patch table.
- Verify guard failure and leave/stop restoration.
- Test top-screen touch with each screen layout used by the deployment.
- Confirm cursor hiding does not change aim deltas.
- Test top-screen confinement and hold-at-center independently and together.
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
