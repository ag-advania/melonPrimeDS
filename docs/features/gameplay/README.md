# Gameplay feature documentation

This index groups gameplay changes that are easy to confuse with the input,
patch, or settings layers.

| Topic | Document | Implementation boundary |
| --- | --- | --- |
| Weapon-switch jump suppression | [Weapon-switch jump suppression](no-double-tap-jump.md) | A transient guest patch used only by the legacy touch fallback |
| Mouse-wheel weapon cycle | [Mouse-wheel weapon cycling](mouse-wheel-weapon-cycling.md) | Secondary host bindings feeding weapon-switch requests |
| Morph Ball assist threshold | [Morph Ball boost assist sensitivity](morph-ball-boost-assist-sensitivity.md) | Raw mouse threshold for the mouse-only assist path |
| Morph Ball boost feature | [Morph Ball boost](../morph-ball-boost.md) | Swipe, hold, right-click, and stylus distinctions |
| Wi-Fi reconnect | [Wi-Fi reconnect fix](wifi-reconnect-52200.md) | Out-of-game Error 52200 recovery path |
| Settings and patch lifecycle | [MelonPrime Settings details](../melonprime-settings/README.md) | Per-setting keys, guards, and ROM tables |

For a patch's apply/restore ownership, use the
[patch-system architecture reference](../../architecture/gameplay/patch-system.md).
The pages here explain why a user-visible gameplay behavior exists and when it
is allowed to run.
