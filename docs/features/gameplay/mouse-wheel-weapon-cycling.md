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

## Binding mouse wheel to hotkeys

Input configuration can assign **Mouse Wheel Up** / **Mouse Wheel Down** to any keyboard/mouse hotkey slot. These are stored as reserved integer codes (`MelonPrime::InputKey::MouseWheelUp` / `MouseWheelDown`) alongside existing Qt key and mouse-button mappings — no config format change.

At runtime, a wheel tick becomes a one-frame virtual button press (then auto-release). When weapon cycling is enabled, wheel deltas still cycle weapons independently of any hotkey bound to the same wheel direction.

Direction follows the **physical** wheel (top rotating away from the user = Up), including when the OS has natural scrolling enabled. Qt's inverted deltas are normalized via `MelonPrime::PhysicalWheelSteps()`.
