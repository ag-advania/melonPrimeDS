# Purple damage notification

## Purpose and dependency

Damage Notify Purple displays the purple damage notification when the local
player's observed damage crosses the configured threshold. It is a host-side
state machine driven by guest memory reads, but the notification is produced
by writing the real guest Double Damage timer; it is not a ROM instruction
patch and it is not a network-authoritative damage meter.

| Control | Key | Default |
| --- | --- | --- |
| Damage notify purple | Metroid.GameFeature.DamageNotifyPurple | false |
| Required dependency | Metroid.GameFeature.DisableDoubleDamageMultiplier | false |

The notification feature is intentionally inactive unless Disable Double
Damage Multiplier is enabled. This keeps its interpretation aligned with the
damage path for which the notification was designed.

## Runtime algorithm

The implementation samples the local player's health state once per frame and
keeps emulation-owned previous/current state. The notification writes 10 to
the CPlayer state at offset 0x4B0, the same timer used by the guest's
double-damage effect. The dependency on Disable Double Damage Multiplier is
therefore safety-critical: without it, a purple notification can briefly grant
a real 2x damage effect to the local player. The constants are:

| Constant | Value |
| --- | ---: |
| Damage threshold | greater than 5 |
| Notification duration | 10 frames |
| Timer field | CPlayer + 0x4B0 |

The ordinary path compares the sampled main HP against the prior sample. A
drop greater than 5 starts or refreshes the purple timer. A small change, a
heal, an unchanged sample, or an invalid sample must not be reported as
damage.

## Weavel proxy handling

Weavel needs a separate interpretation because the apparent damage state can
include an active proxy entity. The implementation:

1. reads the main player HP;
2. checks the proxy-active flag, bit 0x20 in the relevant more-flags field;
3. when active, reads proxy HP at proxy entity offset 0xD0;
4. treats the proxy maximum as 100 for the combined comparison;
5. adds active proxy HP to the main HP representation; and
6. compares the resulting value against the prior valid sample.

The proxy is not blindly dereferenced. Attach/detach frames establish a new
baseline so that proxy creation or destruction is not reported as player
damage. Invalid, null, or junk proxy pointers are skipped rather than used
for arithmetic.

This means a single-frame HUD comparison may not match the notification on a
Weavel transition. That is an intentional false-positive guard.

## State and lifecycle

No ARM9 instruction patch is installed by this feature. The state is owned by
the MelonPrime runtime and is reset when the relevant player/match context is
invalidated. It must not carry a previous match's HP sample into the next
match.

The feature should be tested with:

- feature disabled;
- dependency disabled;
- both enabled;
- normal player;
- Weavel with proxy attach/detach; and
- invalid or changing player pointers.

The notification's presence proves only that the local sampling state machine
observed a qualifying change. It does not prove the remote server accepted a
damage value.

## Verification checklist

- Verify no notification appears with either prerequisite disabled.
- Apply a damage event of five or less and confirm the threshold is strict.
- Apply damage greater than five and verify a ten-frame notification.
- Verify repeated damage refreshes or updates the timer as implemented.
- Verify healing and unchanged HP do not trigger the notification.
- Test Weavel before, during, and after proxy activation.
- Test match leave/rejoin and invalid pointers for state reset.
- Compare local notification timing with, but do not equate it to, authoritative
  online results.

## Evidence and related material

Current source:

- MelonPrimeDamageNotifyPurple.cpp
- MelonPrimeGameSettings.cpp
- MelonPrime.cpp

The related multiplier patch is documented in
[Disable double-damage multiplier](damage-multiplier.md).

Supporting research is maintained separately:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\DoubleDamage\current\Player-Double-Damage-DeepDive-JP1_0.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\input\AltFormDirect\current\summary\Weavel-Halfturret-Entity-Spawn-Investigation-JP1_0.md

No full research report is copied into this page; the current runtime
algorithm and its safety boundaries are kept with the implementation.
