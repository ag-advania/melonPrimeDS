# SnapTap

## Purpose

SnapTap changes the host directional-input resolution so opposite directional
edges can be resolved deterministically instead of depending on the order in
which both physical keys are released. It is an input policy, not a guest ROM
patch.

| Control | Key | Default |
| --- | --- | --- |
| SnapTap | Metroid.Operation.SnapTap | false |

The setting is evaluated in the MelonPrime input path and does not write an
ARM9 address. No ROM revision table or patch restore should be associated
with it.

## Intended behavior

For each opposing directional pair, the input layer tracks the active state
and the most recent eligible edge. When both directions are held, the policy
selects the direction according to the SnapTap rule rather than allowing a
stale simultaneous state to persist. The exact edge ordering is part of the
input implementation and should be tested with physical timestamps, not
inferred from the checkbox label.

The feature is useful for keyboard-style digital input. Analog-stick input,
touch gestures, and remappers can produce a different sequence of edges and
must be reported separately.

## Latency and compatibility

SnapTap may require retaining an edge until the opposing state is resolved.
That can alter the apparent release latency compared with the default
resolution. It does not increase the guest frame rate and it does not bypass
the regular input sampling boundary.

The Joy2Key compatibility layer and native input methods can also affect the
observed order. When comparing SnapTap on and off, keep these constant:

- keyboard or controller source;
- remapper/Joy2Key setting;
- frame pacing and sync mode;
- focus state; and
- the active input method.

Stylus mode is a separate input source and is not a suitable substitute for a
keyboard directional-edge test.

## Persistence and scope

The boolean is a host global setting. It is read by the input runtime and does
not alter save data or guest memory. A configuration reload changes the
runtime policy; it does not require joining a match.

## Verification checklist

- Test left/right and up/down with one direction held while pressing and
  releasing the opposite direction.
- Test both release orders.
- Test repeated taps, simultaneous key-down, and key rollover.
- Repeat with SnapTap disabled as the control condition.
- Repeat with Joy2Key/remapper enabled if that is the deployment configuration.
- Confirm no ROM bytes change and no save data changes.
- Record input device and frame-sync mode with any latency observation.

## Evidence and related material

Current source:

- MelonPrime.cpp input update path
- MelonPrimeInputConfig.cpp
- MelonPrimeDef.h

This feature has no guest address report in mphCodex. For underlying game
input concepts, consult the current input research without copying its
reverse-engineering narrative:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\Input-Direct-Injection-Investigation\current\summary\Input-Direct-Injection-Consolidated-AllVersions.md
