# Morph Ball Boost mouse-mode controls V15

<!-- MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_V15 -->

## User-facing hierarchy

1. **Disable Morph Ball Swipe Boost** — default **OFF**. Unchecked keeps swipe boost enabled.
2. **Use Custom Raw Mouse Movement Threshold** — default **OFF**.
3. **Morph Ball Boost Required Mouse Movement** — range `1..46339`, default `90`; enabled only when swipe boost is not disabled and custom mode is ON.

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

Stylus Mode is unchanged.

## Migration

Existing V14 `MorphBoostSwipeEnabled` values are loaded inversely into the new Disable checkbox. A legacy missing key with required movement `0` appears checked, preserving the former disabled state. No config-key rename is performed.
