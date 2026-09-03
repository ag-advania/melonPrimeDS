# Input feature documentation

This is the navigation index for input behavior. The root README already owns
the user-facing default hotkey table; this index points to the implementation
and feature-specific explanations without copying that table.

## Choose by behavior

| Topic | Document | Scope |
| --- | --- | --- |
| Mouse/controller aim and platform routing | [Aim and platform input](../../architecture/input/aim-input.md) | Input projection, raw deltas, capture, and platform boundaries |
| Zoom input | [Zoom input methods](zoom-input-methods.md) | Legacy/native zoom routes and setting interactions |
| Zoom sensitivity | [Zoom aim sensitivity](../zoom-aim-sensitivity.md) | Native zoom state and sensitivity scaling |
| Stylus/touch compatibility | [Input compatibility settings](../melonprime-settings/input-compatibility.md) | Stylus mode, top-screen touch, touch-only aim, and cursor policy links |
| Pen tablet direct aim | [Pen tablet direct aim](pen-tablet-direct-aim.md) | Opt-in XP-Pen / OpenTabletDriver ingress, source authority, and cursor policy |
| Cursor presentation in stylus mode | [Stylus cursor policy](../melonprime-settings/stylus-cursor-policy.md) | Hide, top-screen confinement, and idle center hold |
| Quick Stop Movement | [Quick Stop Movement](quick-stop-movement.md) | In-game opposing-direction cancellation |
| Weapon switching | [Weapon-switch jump suppression](../gameplay/no-double-tap-jump.md) | Legacy touch fallback and its transient ARM9 guard |
| Morph Ball swipe boost | [Morph Ball boost](../morph-ball-boost.md) | Mouse/stylus/hold/right-click boost behavior |
| Mouse-wheel weapon cycle | [Mouse-wheel weapon cycling](../gameplay/mouse-wheel-weapon-cycling.md) | Secondary next/previous bindings |

## Related engineering references

- [Settings UI and edit mode](../../development/ui/settings-and-edit-mode.md)
- [Patch system](../../architecture/gameplay/patch-system.md)
- [Immediate input edge overlay](../../architecture/gameplay/patches/immediate-input-edge-overlay.md)
- [Native aim delta register injection](../../architecture/gameplay/patches/native-aim-delta-register-injection.md)
- [mphCodex reference workflow](../../reverse-engineering/mphcodex-workflow.md)

When input behavior changes, first identify whether the contract belongs to
host routing, input projection, guest patching, or a feature fallback. Keep
those lifecycles separate in the owning document.
