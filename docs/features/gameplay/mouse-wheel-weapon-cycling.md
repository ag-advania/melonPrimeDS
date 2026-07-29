# Mouse Wheel Weapon Switching

Mouse-wheel weapon switching is configured through standard input bindings, not a dedicated toggle.

## Bindings

| Action | Config key | Default |
|---|---|---|
| Next Weapon (Primary) | `Keyboard.HK_MetroidWeaponNext` | `J` (unchanged; shown as `(J)` in the label) |
| Next Weapon (Secondary) | `Keyboard.HK_MetroidWeaponNextSecondary` | Mouse Wheel Down |
| Previous Weapon (Primary) | `Keyboard.HK_MetroidWeaponPrevious` | `K` (unchanged; shown as `(K)` in the label) |
| Previous Weapon (Secondary) | `Keyboard.HK_MetroidWeaponPreviousSecondary` | Mouse Wheel Up |

Either binding for an action activates that action. Set a Secondary binding to **None** to disable wheel weapon switching while keeping keyboard/mouse-button Primary bindings.

Secondary defaults use physical wheel direction and match the historical special-case behavior (`wheelDelta < 0` → next).

## Removed setting

`Metroid.Input.MouseWheelWeaponCycle` / **Disable Mouse Wheel Weapon Cycling** has been removed. Stale TOML entries are ignored.
