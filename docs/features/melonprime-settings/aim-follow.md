# Instant aim follow and camera lock

## Controls

| Control | Key | Default | Availability |
| --- | --- | --- | --- |
| Instant aim follow | Metroid.Aim.Enable.InstantAimFollow | false | legacy/compatibility control |
| FPS Camera Lock | Metroid.Aim.Enable.FpsCameraLock | false | public settings control |
| Low-latency mode | Metroid.Aim.LowLatencyMode | 0 | public control with backend/runtime gates; 0=Off |
| Native aim hook mode | Metroid.Aim.NativeHookMode | 0 | developer build only |

This page explains the user-visible follow/lock behavior and its boundaries.
Native hook modes and immediate input tracing are documented in
[Developer hooks](developer-hooks.md). General aim scaling is documented in
[Aim and sensitivity](sensitivity.md).

## Instant aim follow

Instant aim follow is a legacy compatibility switch. It belongs to the
historical host-side aim-follow path and must not be described as equivalent to
FPS Camera Lock:

- aim follow changes how host input is carried toward the current aim state;
- camera lock changes a guest camera update path; and
- neither setting automatically enables the other.

The setting is retained so existing configurations do not silently change.
New behavioral claims should be made only after testing the current build,
because the native/stylus paths can intentionally bypass the legacy control.

## FPS Camera Lock

FPS Camera Lock is a guarded static ROM patch. It changes the camera update
instructions for the selected ROM revision; it is not a host frame-rate
setting and it does not change the screen aspect ratio.

| ROM | Patched span |
| --- | --- |
| JP1.0 / JP1.1 | 0x02028070 through 0x02028080, five words |
| US1.0 / US1.1 / EU1.1 | 0x02028094 through 0x020280A4, five words |
| EU1.0 | 0x0202808C through 0x0202809C, five words |
| KR1.0 | 0x0200B200, one word |

The implementation validates the expected original instruction sequence for
the ROM before writing. The KR layout is intentionally different and must not
be treated as a shortened copy of the other regions.

Operationally:

1. the selected guest revision identifies the patch table;
2. the patch checks the expected words;
3. enabling the setting applies the camera-lock replacement;
4. disabling it restores only the words owned by this patch; and
5. leaving or stopping the match restores the original state through the
   patch registry.

A successful write means the guard accepted the current RAM contents. It does
not prove that the resulting camera behavior is correct on every game mode.
Test first-person movement, morph ball, menu transitions, respawn, and a
second match.

## Low-latency mode

Low-latency mode is adjacent but separate. It changes the input-to-frame
timing path and may install backend/runtime hooks. It is not implied by FPS
Camera Lock or Instant aim follow. Hardware/backend eligibility and runtime
evidence must be reported separately from a configuration write.

## Configuration boundaries

Native/stylus input compatibility can suppress legacy aim paths. The settings
UI should still expose the stored value, but a test in stylus mode is not a
valid acceptance test for legacy Instant aim follow. Similarly, a static
camera patch test must record the ROM revision; an address match from another
revision is not evidence.

## Verification checklist

- Verify each setting is independently toggleable.
- Test FPS Camera Lock on every supported ROM revision in the table.
- Confirm guard failure leaves the original words unchanged.
- Verify leave/stop restoration and a subsequent match.
- Test camera behavior through first-person movement, morph ball, respawn,
  menus, and repeated match joins.
- Test Instant aim follow with normal input and separately with stylus/native
  modes.
- Report Low-latency mode as a separate timing experiment.

## Evidence and related material

Current source:

- MelonPrimePatchFpsCameraLock.cpp
- MelonPrimePatchRegistry.cpp
- MelonPrimePatchLifecycle.cpp
- MelonPrimeInputConfig.cpp

Detailed related docs:

- docs/architecture/gameplay/patches/fps-camera-lock.md
- docs/architecture/input/aim-input.md
- docs/features/zoom-aim-sensitivity.md

Reverse-engineering context is maintained in mphCodex. The relevant Widescreen
and camera research should be consulted for instruction intent, but the
current melonPrimeDS guard/replacement table is authoritative:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\AimHook\current\summary\MelonPrimeDS-AIM-Hook-Implementation-Checklist-AllVersions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_Commons\current\Widescreen.md
