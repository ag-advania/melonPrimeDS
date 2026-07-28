# Morph Ball Boost mouse-mode controls

<!-- MELONPRIME_FINAL_DOC_AUDIT_V18 -->

## User-facing hierarchy

The three Morph Ball settings are placed directly below **Enable Aim Sub-pixel Accumulator** in the Sensitivity section.

1. **Disable Morph Ball Swipe Boost** — default **OFF**. Unchecked keeps swipe boost enabled.
2. **Use Custom Raw Mouse Movement Threshold** — default **OFF**.
3. **Morph Ball Boost Required Mouse Movement** — range `1..46339`, default `90`; enabled only when swipe boost is not disabled and custom mode is ON.

The row position is resolved from the actual sub-pixel widget at runtime instead of using fixed `QFormLayout` row numbers.

## Compatibility contract

The stored key remains the positive boolean `Metroid.Input.MorphBoostSwipeEnabled`. The UI uses an inverted checkbox binding:

| Disable checkbox | Stored positive key | Runtime behavior |
|---|---:|---|
| unchecked (default) | `true` | Swipe boost enabled |
| checked | `false` | Mouse swipe boost disabled |

Keeping the positive storage key avoids rewriting existing TOML files and preserves V14 runtime/config compatibility.

## Runtime modes

| Disable swipe boost | Custom threshold | Mouse-mode behavior |
|---|---|---|
| ON | any | Mouse swipe boost is disabled. R/right-click and Shift auto-cycle remain available. |
| OFF | OFF | The game-internal `altSteerDelta` amount owns swipe detection. The numeric setting is ignored. |
| OFF | ON | Current-frame raw `m_input.mouseX/Y` must reach the configured threshold. Accepted input creates one native swipe pulse; rejected frames keep `CanTouchBoost` closed. |

The custom path uses the same current-frame raw vector for qualification and direction. It does not queue the input for a later frame and does not add an aim-frame delay. Stylus Mode is unchanged.

`90` is the MelonPrime default for the raw-input option. It does **not** represent MPH's native internal swipe threshold, and the practical feel depends on the input backend, mouse DPI, and polling behavior.

## Reset behavior

**Reset sensitivity values** restores every setting currently parented under the Sensitivity section from the compiled `Config.cpp` default tables. This includes the three Morph Ball controls.

The reset changes widgets only. Values are not committed to TOML or runtime state until the settings dialog's normal Save/OK path runs; cancelling the dialog leaves the saved configuration unchanged.

## Migration

Existing V14 `MorphBoostSwipeEnabled` values are loaded inversely into the Disable checkbox. A legacy missing key with required movement `0` appears checked, preserving the former disabled state. No config-key rename is performed.

## Validation boundary

Static audits cover config/default parity, inverted checkbox persistence, current-frame gate structure, localization row coverage, dynamic UI placement, reset-to-default source ownership, CLAUDE layout, documentation links, and `git diff --check`.

Compilation and manual Windows/gameplay testing remain separate required checks before claiming release runtime validation.
