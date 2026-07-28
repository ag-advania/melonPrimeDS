# Mouse Wheel Weapon Cycling

<!-- MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_V15 -->

<!-- MELONPRIME_DISABLE_CHECKBOX_SEMANTICS_WHEEL_DOC_V15 -->

## User-facing setting

The UI is **Disable Mouse Wheel Weapon Cycling** and defaults unchecked. The stored compatibility key remains positive:

```text
Key:     Metroid.Input.MouseWheelWeaponCycle
Type:    bool
Default: true
```

| Disable checkbox | Stored key | Behavior |
|---|---:|---|
| unchecked (default) | `true` | Mouse-wheel deltas cycle weapons. |
| checked | `false` | Wheel-only cycling is disabled and scrolling remains available for other bindings. |

The InputConfig binding uses an explicit inverted-bool kind, so loading and saving are exact inverses without changing existing TOML data.

Next Weapon, Previous Weapon, and direct weapon bindings remain independent and continue to work while the checkbox is checked.
