# Morph Ball Boost Assist Sensitivity

<!-- MELONPRIME_MORPH_BOOST_ASSIST_DOC_V6 -->
<!-- MELONPRIME_MORPH_BOOST_9000_DOC_V7 -->

## Purpose

`Metroid.Sensitivity.MorphBoostMouse` controls the mouse movement threshold used for Samus's Morph Ball swipe boost. It changes only the mouse-mode swipe path. Stylus Mode, the right-click R boost, and the Shift automatic boost cycle retain their existing paths.

## User-facing behavior

| Value | Behavior |
|---:|---|
| `0%` | Suppress the mouse swipe boost path by clearing `CanTouchBoost` each applicable frame. |
| `1%`～`99%` | Require more mouse movement than the game's default swipe threshold. |
| `100%` | Use the game's default squared threshold, `0x1FA4`. |
| `101%`～`9000%` | Require less mouse movement; `200%` is approximately half, `1000%` approximately one tenth, and `9000%` approximately one ninetieth of the default movement amplitude. `9000%` produces the minimum squared threshold of `1`. |

The threshold is derived from an amplitude percentage while the game compares squared magnitude:

```text
thresholdSquared = round(0x1FA4 * 10000 / percentage^2)
```

A zero percentage is handled separately and never enters the division.

## Runtime ownership and ordering

- `LoadRuntimeConfigSnapshot()` reads and clamps the persisted value on the cold config path.
- `ApplyRuntimeConfigSnapshot()` publishes the derived squared threshold to the per-instance `MelonPrimeCore` member.
- `m_morphBoostAssistThresholdSq` is a warm per-instance scalar because `HandleMorphBallBoost()` reads it during active gameplay.
- `HandleMorphBallBoost()` executes after standard button mapping and before the mouse aim path.
- No per-frame `Config::Table` lookup, allocation, lock, logging, or platform-specific branch is added.

## Input arbitration

The implementation preserves the accepted double-boost sequence:

```text
hold R -> mouse/stylus swipe boost -> release R -> button boost
```

In mouse mode, the configured threshold may suppress or promote the swipe path. The right-click R boost and Shift automatic cycle remain separate button paths. Stylus Mode does not use this mouse sensitivity setting.

## Configuration contract

```text
Key:     Metroid.Sensitivity.MorphBoostMouse
Type:    int
Default: 100
Range:   0～9000
Scope:   Instance*.Metroid.*
```

The key must remain in `DefaultInts`, use `CfgKey::MorphBoostMouseSens`, load/save through the non-HUD binding table, and cross the runtime through `RuntimeConfigSnapshot`.

## Localization terminology

The English source key remains `Morph Ball Boost Assist Sensitivity`. Official manuals were used as terminology references for the established game terms:

- English: `MORPH BALL`, `BOOST`
- Spanish: `MORFOSFERA`, `TURBO`
- French: `BOULE MORPHING`, `BOOST`
- German: `MORPH BALL`, `BOOST`
- Italian: `MORFOSFERA`, `TURBO`
- Japanese: `モーフボール`, `ブースト`

Reference manuals:

- https://www.nintendo.co.jp/data/software/manual/AMHJ_J.pdf
- https://m1.nintendo.net/docvc/NTR/JPN/AMHJ/AMHJ_J.pdf
- https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_EN.pdf
- https://csassets.nintendo.com/noaext/image/private/t_KA_PDF/DS_Metroid_Prime_Hunters
- https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_ES.pdf
- https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_FR.pdf
- https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_DE.pdf
- https://www.nintendo.com/eu/media/downloads/games_8/emanuals/nintendo_ds_21/Manual_NintendoDS_MetroidPrimeHunters_IT.pdf

The 14 languages called out by the localization quality audit receive explicit focused-correction rows in `MelonPrimeTranslationsMouseBoost.inc`: Zulu, Slovak, Slovenian, Basque, Kazakh, Hebrew, Amharic, Catalan, Odia, Estonian, Assamese, Kyrgyz, Filipino, and Swahili.

## Validation

Required checks:

```text
git diff --check
powershell -File tools/ci/audits/audit-config-defaults.ps1
powershell -File tools/ci/audits/check-inc-ownership.ps1
powershell -File tools/ci/audits/audit-melonprime-srp-performance.ps1
python tools/ci/audits/localization/audit-melonprime-localization.py
python tools/ci/audits/localization/audit-melonprime-all-new-language-coverage.py
python tools/ci/audits/localization/audit-morph-ball-boost-assist-translations.py
```

Compilation and runtime smoke testing remain distinct from static audits. Runtime testing should cover `0`, `50`, `99`, `100`, `101`, `200`, `1000`, and `9000`, including right-click R, Shift auto-cycle, transformation edges, and the R-hold/swipe/R-release sequence.
