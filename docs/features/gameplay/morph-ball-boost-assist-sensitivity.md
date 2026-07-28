# Morph Ball Boost Assist Sensitivity

<!-- MELONPRIME_MORPH_BOOST_ASSIST_DOC_V6 -->
<!-- MELONPRIME_MORPH_BOOST_9000_DOC_V7 -->
<!-- MELONPRIME_MORPH_BOOST_NATIVE_SWIPE_DOC_V9 -->

## Purpose

`Metroid.Sensitivity.MorphBoostMouse` controls the mouse movement threshold used for Samus's original Morph Ball touch/swipe boost. It does not convert mouse movement into an R-button boost. Stylus Mode, manual right-click R boost, and the Shift automatic boost cycle retain their existing paths.

## User-facing behavior

| Value | Behavior |
|---:|---|
| `0%` | Suppress the mouse swipe boost path by clearing `CanTouchBoost` each applicable frame. |
| `1%`～`99%` | Require more mouse movement than the game's default swipe threshold. |
| `100%` | Preserve the game's default squared threshold, `0x1FA4`. |
| `101%`～`9000%` | Require less movement. The first configured-threshold crossing emits one native touch/swipe pulse. Continuous movement cannot emit another pulse until movement falls below the configured threshold and the native boost state finishes. |

The threshold remains an amplitude percentage converted to the squared quantity used by the game:

```text
thresholdSquared = round(0x1FA4 * 10000 / percentage^2)
```

`9000%` maps to the minimum stored squared threshold of `1`.

## Why V7 became overpowered

V7 promoted every below-native `altSteerDelta` frame that exceeded the configured threshold. At very high sensitivity, ordinary continuous mouse motion could therefore be promoted on consecutive frames. Each accepted frame entered the game's touch/swipe boost path again, allowing repeated Boosting/contact-damage states rather than one physical swipe gesture.

V9 retains the native touch/swipe route but makes promotion a one-frame pulse. `m_morphBoostSwipePulseLatched` records that the current gesture has already emitted its swipe. Promotion is not repeated while the gesture remains above threshold. The latch re-arms only when movement is below threshold and both `CPlayer +0x4C4 bit26` Boosting and the cached boost busy timer are clear.

## Why V8 is not used

V8 routed high-sensitivity assistance through a native R charge/release cycle. That was safer than repeated vector promotion, but it changed the feature into an R/Shift-style boost. V9 removes the V8 R-cycle state and never synthesizes R for mouse sensitivity assistance.

## Native game-flow evidence

Reverse-engineering notes are stored in `Zection6V/mphCodex`:

- `02021C04-altform-boost-input-update-JP1_0.md` identifies the ability gate, touch boost gate, R hold/release processing, speed application, and Boosting state in the game's own update function.
- `0200BDE0-player-boost-contact-damage-JP1_0.md` shows that Boosting contact consumes `player+0x149` damage and ends the current Boosting state.
- `0200C49C-player-end-boost-altattack-JP1_0.md` documents clearing the Samus Boosting flag.

The implementation therefore leaves damage, speed, effects, and boost termination to the original touch/swipe game path. It changes only whether one gesture is accepted at the fixed native comparison.

## Runtime ownership and state

- `LoadRuntimeConfigSnapshot()` reads and clamps the persisted value on the cold path.
- `ApplyRuntimeConfigSnapshot()` publishes the squared threshold and resets the transient swipe-pulse latch.
- `m_morphBoostAssistThresholdSq` and `m_morphBoostSwipePulseLatched` are warm per-instance scalars.
- Config reload, startup, boot reset, stop, focus loss, game leave, and game join reset the latch.
- The hot path performs no config lookup, allocation, lock, or logging.

## Input arbitration

- `0%` disables mouse swipe boost.
- `1%`～`99%` suppresses the native threshold until the configured higher threshold is crossed.
- `100%` retains the previous native behavior.
- Above `100%`, one threshold crossing emits one native swipe pulse.
- A true native-threshold flick and an assisted below-native flick share the same one-gesture latch above `100%`, preventing a double pulse from one continuous motion.
- Manual R and Shift remain independent. Movement present while either is held is latched so releasing the button does not immediately create an unrelated assisted swipe.
- The accepted `hold R -> native swipe -> release R` sequence remains available after a distinct swipe gesture.

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

Runtime testing must compare one short flick with a sustained movement at `101`, `200`, `1000`, and `9000`. A sustained movement must produce at most one assisted native swipe until the movement returns below threshold and the boost finishes. Also test `0`, `50`, `99`, `100`, manual R, Shift, transformation edges, enemy contact, and `hold R -> swipe -> release R`.
