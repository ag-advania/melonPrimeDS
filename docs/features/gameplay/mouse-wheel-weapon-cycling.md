# Mouse Wheel Weapon Cycling

<!-- MELONPRIME_MOUSE_WHEEL_WEAPON_CYCLE_DOC_V7 -->

## Purpose

`Metroid.Input.MouseWheelWeaponCycle` controls only the hardcoded mouse-wheel path that cycles through the sorted weapon order. The existing Next Weapon, Previous Weapon, and direct weapon bindings remain independent.

## Behavior

| Value | Behavior |
|---|---|
| `true` (default) | Mouse-wheel deltas cycle through available weapons exactly as before. |
| `false` | Mouse-wheel deltas are ignored by the hardcoded cycle path, leaving wheel input available for other bindings or external input tools. |

Disabling the option does not clear or consume the wheel input at capture time. It only removes `m_input.wheelDelta` from the weapon-cycle trigger and direction calculation. Keyboard/controller Next Weapon and Previous Weapon inputs still use the sorted cycle order, and direct weapon selection still works.

## Runtime ownership

- The boolean is loaded through `RuntimeConfigSnapshot` on the cold config path.
- `ApplyRuntimeConfigSnapshot()` stores it in the per-instance warm scalar `m_enableMouseWheelWeaponCycle`.
- `HandleInGameLogic()` uses it to avoid calling `ProcessWeaponSwitch()` for wheel-only input while disabled.
- `ProcessWeaponSwitch()` also masks `wheelDelta` to zero so simultaneous Next/Previous input cannot accidentally consume a disabled wheel delta.
- No platform-specific branch, allocation, lock, or per-frame config lookup is added.

## Configuration contract

```text
Key:     Metroid.Input.MouseWheelWeaponCycle
Type:    bool
Default: true
Scope:   Instance*.Metroid.*
```

## Regression checks

1. Enabled: wheel up/down cycles weapons in the historical direction.
2. Disabled: wheel movement alone never changes weapons.
3. Disabled: Next Weapon and Previous Weapon bindings still cycle weapons.
4. Disabled: direct weapon bindings still select weapons.
5. Toggle the setting while a game is running and verify the next config reload applies it.
6. Verify both mouse mode and Stylus Mode, including Omega Cannon restrictions.
