# Morph Ball Boost Assist Sensitivity

<!-- MELONPRIME_MORPH_BOOST_ASSIST_DOC_V6 -->
<!-- MELONPRIME_MORPH_BOOST_9000_DOC_V7 -->
<!-- MELONPRIME_MORPH_BOOST_SHIFT_CADENCE_DOC_V10 -->

## Purpose

`Metroid.Sensitivity.MorphBoostMouse` controls the mouse movement threshold used for Samus's original Morph Ball touch/swipe boost. It does not convert mouse movement into an R-button boost. Stylus Mode, manual right-click R boost, and the Shift automatic boost cycle retain their existing paths.

## User-facing behavior

| Value | Behavior |
|---:|---|
| `0%` | Suppress the mouse swipe boost path by clearing `CanTouchBoost` each applicable frame. |
| `1%`～`99%` | Require more mouse movement than the game's default swipe threshold. |
| `100%` | Preserve the game's default squared threshold, `0x1FA4`. |
| `101%`～`9000%` | Require less movement. One native touch/swipe pulse may be emitted per completed Shift-equivalent interval: native Boosting/cooldown clear plus an equivalent elapsed-frame count greater than `0x0A`. Continuous movement no longer has to fall below the threshold before the next interval. |

The threshold remains an amplitude percentage converted to the squared quantity used by the game:

```text
thresholdSquared = round(0x1FA4 * 10000 / percentage^2)
```

`9000%` maps to the minimum stored squared threshold of `1`.

## Why V7 became overpowered

V7 promoted every below-native `altSteerDelta` frame that exceeded the configured threshold. At very high sensitivity, ordinary continuous mouse motion could therefore be promoted on consecutive frames. Each accepted frame entered the game's touch/swipe boost path again, allowing repeated Boosting/contact-damage states rather than one physical swipe gesture.

V10 retains the native touch/swipe route and keeps promotion to one frame, but replaces the gesture latch with a native-cycle state machine. After emitting a pulse, it waits for `CPlayer +0x4C4 bit26` Boosting or the cached `player+0x14A` busy timer to appear, then re-arms only after that native busy state clears and the private elapsed-frame counter has passed the same `> 0x0A` readiness threshold used by Shift. Mouse movement does not have to return below threshold. This prevents frame-by-frame damage while allowing the same `busy clear && equivalent elapsed frames > 0x0A` cadence used by the Shift auto-cycle.

## Why V8 is not used

V8 routed high-sensitivity assistance through a native R charge/release cycle. That was safer than repeated vector promotion, but it changed the feature into an R/Shift-style boost. V10 removes the V8 R-cycle state and never synthesizes R for mouse sensitivity assistance.

## Native game-flow evidence

Reverse-engineering notes are stored in `Zection6V/mphCodex`:

- `02021C04-altform-boost-input-update-JP1_0.md` identifies the ability gate, touch boost gate, R hold/release processing, speed application, and Boosting state in the game's own update function.
- `0200BDE0-player-boost-contact-damage-JP1_0.md` shows that Boosting contact consumes `player+0x149` damage and ends the current Boosting state.
- `0200C49C-player-end-boost-altattack-JP1_0.md` documents clearing the Samus Boosting flag.

The implementation therefore leaves damage, speed, effects, and boost termination to the original touch/swipe game path. It changes only whether one gesture is accepted at the fixed native comparison.

## Runtime ownership and state

- `LoadRuntimeConfigSnapshot()` reads and clamps the persisted value on the cold path.
- `ApplyRuntimeConfigSnapshot()` publishes the squared threshold and resets the transient swipe-cycle state.
- `m_morphBoostAssistThresholdSq`, `m_morphBoostSwipePulseState`, and `m_morphBoostSwipePulseElapsedFrames` are warm per-instance scalars.
- Config reload, startup, boot reset, stop, focus loss, game leave, and game join reset the state machine.
- The hot path performs no config lookup, allocation, lock, or logging.

## Input arbitration

- `0%` disables mouse swipe boost.
- `1%`～`99%` suppresses the native threshold until the configured higher threshold is crossed.
- `100%` retains the previous native behavior.
- Above `100%`, one accepted event emits one native swipe pulse.
- A true native-threshold event and an assisted below-native event share the same native-cycle state, preventing multiple pulses inside one Shift-equivalent busy/charge interval.
- Sustained motion may emit again when the native busy timer is clear and the equivalent `> 0x0A` interval has elapsed; it does not wait for movement to fall below threshold.
- Shift owns its own automatic cycle while held. Manual R does not block the native swipe pulse, preserving `hold R -> swipe -> release R`.

## Configuration contract

```text
Key:     Metroid.Sensitivity.MorphBoostMouse
Type:    int
Default: 100
Range:   0～9000
Scope:   Instance*.Metroid.*
```

## Localization

The V7 label and 76 direct translation rows are retained. V8's descriptions that claimed an R/Shift-style cycle are removed. The existing description remains accurate: values above `100%` require less mouse movement, while right-click R and Shift remain unchanged.

## Validation

Required static checks remain:

```text
git diff --check
powershell -File tools/ci/audits/audit-config-defaults.ps1
powershell -File tools/ci/audits/check-inc-ownership.ps1
powershell -File tools/ci/audits/audit-melonprime-srp-performance.ps1
python tools/ci/audits/localization/audit-melonprime-localization.py
python tools/ci/audits/localization/audit-melonprime-all-new-language-coverage.py
python tools/ci/audits/localization/audit-morph-ball-boost-assist-translations.py
```

Runtime testing must compare one short flick with sustained movement at `101`, `200`, `1000`, and `9000`. Sustained movement may repeat, but never more than once per completed Shift-equivalent busy/charge interval. Compare its repeat cadence directly with held Shift. Also test `0`, `50`, `99`, `100`, manual R, Shift, transformation edges, enemy contact, and `hold R -> swipe -> release R`.
