# Quick Stop Movement

Quick Stop Movement is the always-on in-game movement policy that cancels
opposing digital directions. Pressing forward and back together, or left and
right together, produces no movement on that axis. This lets a keyboard
player stop quickly without waiting for a release event to be processed in a
later game frame.

It is not a separate checkbox and it is not a guest ROM patch.

## User-visible behavior

The movement keys are projected into a four-bit movement index:

| Bit | Direction |
| ---: | --- |
| 0 | Forward |
| 1 | Back |
| 2 | Left |
| 3 | Right |

The in-game preset binding table converts that index into a DS button mask.
Opposing pairs are removed before the mask is written:

| Held directions | Result |
| --- | --- |
| Forward only | Forward |
| Back only | Back |
| Forward + Back | Neither forward nor back |
| Left + Right | Neither left nor right |
| Forward + Right | Forward/right diagonal |
| Forward + Back + Right | Right only |
| All four | No movement |

The actual DS button values depend on the selected Touch R, Touch L, Dual R,
or Dual L control preset. The cancellation rule is shared; the direction-to-
button mapping is not.

## Runtime boundary

The policy is applied by the in-game movement path after the current preset
has been resolved. The hot path performs the movement-table lookup and merges
the result with the separately projected jump and fire inputs.

This feature:

- runs during in-game input synthesis;
- uses the per-join preset binding snapshot;
- preserves the selected preset's D-pad or face-button movement mapping;
- does not change aim deltas;
- does not change the jump, fire, zoom, or morph bindings;
- does not write ARM9 code or guest save data; and
- has no independent configuration key.

The preset snapshot is built at game join so a client and a host use the
same control-preset source. A change to the underlying preset should be
tested after a new join rather than assumed to update an already-built table.

## Menus are different

Quick Stop Movement is intentionally an in-game policy. Out-of-game menu
movement uses a fixed D-pad MenuMoveMask so the Adventure map and Hunter
License navigation remain independent of the selected in-game control
preset.

That means these two observations are not contradictory:

- WASD can stop an in-game player by canceling opposing directions; and
- the same physical keys can navigate an out-of-game menu through the fixed
  menu mapping.

The menu path does not synthesize fire, jump, zoom, or boost. It is a
movement-only projection.

## Relationship with SnapTap

Quick Stop Movement and SnapTap share the movement conflict stage but have
different goals:

| Policy | Opposing directions | Setting |
| --- | --- | --- |
| Normal movement resolution | Cancels both directions | Always on in-game |
| SnapTap | Resolves the conflict using tracked eligible edges/priority | Metroid.Operation.SnapTap |

With SnapTap off, pressing both directions on one axis neutralizes that axis.
With SnapTap on, the most recent eligible edge can win according to the
SnapTap resolver. Do not treat a “last direction wins” observation as a
failure of the cancellation policy without recording the SnapTap state.

The full SnapTap lifecycle, latency considerations, and verification matrix
are in [snaptap.md](../melonprime-settings/snaptap.md).

## Relationship with Joy2Key and analog input

The policy operates on the digital movement index after the host input path
has produced its directional state. Joy2Key or another remapper can change
the order and duration of the edges that reach this stage. Analog input does
not have the same keyboard edge semantics.

For a reproducible comparison, keep these constant:

- physical keyboard/controller and remapper state;
- Joy2Key compatibility setting;
- SnapTap setting;
- frame pacing and screen-sync mode;
- focus state; and
- active in-game control preset.

Stylus aim and touch gestures are separate input routes. They are not a
substitute for a digital opposing-key test.

## Source map

The movement index and hot-path merge are implemented in
src/frontend/qt_sdl/MelonPrimeGameInput.cpp. The preset-derived movement
table and fixed menu table are defined in
src/frontend/qt_sdl/MelonPrimeInputProjection.h and the related input
binding code.

The detailed host/guest input contract is in
[aim-input.md](../../architecture/input/aim-input.md). It records the
per-preset mappings, mirror handling, join-time snapshot, and the distinction
between in-game and menu movement.

## Verification checklist

- Hold forward and back together, then release each in both orders.
- Hold left and right together, then release each in both orders.
- Test each of the four in-game control presets.
- Test diagonal movement while the opposing pair is held.
- Test all four directions together.
- Repeat with SnapTap off and on, recording which direction wins when enabled.
- Repeat with Joy2Key/remapping in the deployment configuration.
- Check an Adventure map and Hunter License page to confirm fixed D-pad
  navigation still works.
- Confirm movement cancellation does not suppress jump, fire, zoom, or boost.
- Confirm no ROM bytes or save data change.
- Report keyboard behavior, controller behavior, and stylus behavior
  separately.
