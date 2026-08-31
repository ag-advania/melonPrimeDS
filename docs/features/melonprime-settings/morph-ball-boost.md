# Morph Ball boost settings

## Setting contract

These controls are dynamically grouped under Input Settings. They
configure the MelonPrime mouse-swipe assist; they do not replace the existing
right-click boost or the Shift hold-to-boost action described in the broader
[Morph Ball Boost feature reference](../morph-ball-boost.md).

| UI control | Key | Default | UI range/semantics |
| --- | --- | --- | --- |
| Disable Morph Ball Swipe Boost | Metroid.Input.MorphBoostSwipeEnabled | true | Inverted checkbox; checked means the stored feature is disabled |
| Use Custom Raw Mouse Movement Threshold | Metroid.Input.MorphBoostCustomRawThreshold | false | Checkbox |
| Morph Ball Boost Required Mouse Movement | Metroid.Input.MorphBoostSwipeDistance | 90 | Integer 1–46339 |

The legacy `Metroid.Sensitivity.MorphBoostMouse` percentage key is retained
only for older configuration compatibility (V7–V10). The current dynamic
spin box stores raw required movement in `MorphBoostSwipeDistance`; it is not a
percentage and is not the guest's native swipe threshold.

## Parent and child behavior

The **Disable Morph Ball Swipe Boost** label is intentionally inverted to keep
the persisted setting compatible with the current runtime name:

| Parent checkbox | Stored `MorphBoostSwipeEnabled` | Effect |
| --- | --- | --- |
| Checked | false | Disable mouse-mode swipe assist |
| Unchecked | true | Allow mouse-mode swipe assist |

When the parent is disabled, the custom-threshold checkbox and its description
are disabled. The required-movement spin box is enabled only when swipe assist
and custom raw threshold mode are both enabled. Stylus mode can also disable
the normal mouse-oriented controls.

The parent affects mouse-mode swipe arbitration only. The right-click `R`
boost, the Shift hold-to-boost auto-cycle, and the stylus path remain separate
input paths.

## Internal threshold mode

With swipe assist enabled and custom raw threshold mode **off**, the game-owned
`altSteerDelta` determines whether the current frame would qualify as a swipe:

~~~text
gameSwipe = (altSteerDelta.x^2 + altSteerDelta.y^2) > 0x1FA4
~~~

The host does not synthesize or normalize a raw vector in this mode. A visible
game-generated swipe is allowed to win for the frame. If there is no such swipe,
the native button-charge path remains available when the R or Shift action is
being used.

## Custom raw threshold mode

With custom mode **on**, the current frame's raw mouse sample is authoritative:

~~~text
rawMagnitudeSq = mouseX^2 + mouseY^2
configuredSwipe = rawMagnitudeSq >= requiredMovement^2
~~~

The sample must be nonzero and the boost must not already be busy. One accepted
sample opens the guest `CanTouchBoost` bit for a pulse and promotes the vector
to at least the game's swipe-promotion magnitude. The implementation clamps
the promoted components to signed 16-bit range, tracks the pulse until guest
busy state is observed and released, and closes the bit on every unaccepted
frame. A Shift auto-cycle does not emit the custom raw pulse.

The same `mouseX/mouseY` sample is subsequently used by the normal mouse aim
processing. The threshold check therefore does not consume the sample or add a
second aim frame.

## Runtime boundaries

The assist is evaluated in `HandleMorphBallBoost()` and is Samus-only. It is
active only in Morph Ball/Alt Form and non-stylus mouse mode. The state is
cleared when leaving the applicable form, changing mode, leaving/stopping a
match, or observing a timed-out/invalid pulse. The implementation writes the
guest `CanTouchBoost` flag and `altSteerDelta` only after the normal player
pointer/address resolution has succeeded; this is runtime input state, not a
static ROM patch.

The required movement value is squared once while loading the runtime snapshot.
Values outside 1–46339 are clamped before squaring so the signed 32-bit squared
threshold remains safe. If an old configuration has no explicit parent key,
the loader uses the legacy distance-zero convention and migrates through the
current UI controls.

## Verification checklist

- Confirm the inverted parent checkbox maps checked to stored `false` and
  unchecked to stored `true`.
- Test parent off: mouse swipe assist is disabled, while right-click and Shift
  paths remain available.
- Test parent on/custom off with small and large internal `altSteerDelta`.
- Test custom mode at required movement minus one, exactly the threshold, and
  threshold plus one; include horizontal, vertical, and diagonal samples.
- Confirm one pulse is emitted for one accepted sample and no duplicate pulse
  occurs while the guest boost is busy.
- Confirm the same raw sample still reaches aim processing without an extra
  frame or consumed delta.
- Test Samus versus non-Samus, Morph Ball versus biped, stylus mode, and
  Joy2Key/SnapTap combinations.
- Test config reload, leave, stop, and rejoin behavior. The Sensitivity reset
  button does not reset these controls because they now belong to Input
  Settings.

## Evidence and related material

Current source:

- src/frontend/qt_sdl/InputConfig/MelonPrimeInputConfig.cpp
- src/frontend/qt_sdl/MelonPrimeRuntimeConfig.cpp
- src/frontend/qt_sdl/MelonPrimeInGame.cpp
- src/frontend/qt_sdl/MelonPrimeGameInput.cpp
- src/frontend/qt_sdl/MelonPrimeLifecycle.cpp
- src/frontend/qt_sdl/MelonPrimeDef.h

Broader user-facing behavior and hotkey ownership:

- [Morph Ball Boost](../morph-ball-boost.md)

Reverse-engineering material is maintained in mphCodex and is referenced
instead of copied:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\Morph-Ball-Boost-Investigation-JP1_0\current\README.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\Morph-Ball-Boost-Investigation-JP1_0\current\Morph-Ball-Boost-AllVersions-AddressMap.md
