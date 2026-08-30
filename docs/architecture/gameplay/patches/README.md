# Patch-specific references

This directory contains implementation notes for patches that have a
distinct guest-address or lifecycle contract. The general apply/restore,
guard, registry, and ownership rules remain in
[patch-system.md](../patch-system.md).

| Patch area | Reference | Main boundary |
| --- | --- | --- |
| FPS camera lock | [fps-camera-lock.md](fps-camera-lock.md) | Match-scoped camera hook and mode gating |
| Immediate input edge overlay | [immediate-input-edge-overlay.md](immediate-input-edge-overlay.md) | Developer-only input edge projection |
| Instant Aim Follow | [instant-aim-follow.md](instant-aim-follow.md) | Aim-follow runtime hook and migration alias |
| Native aim delta register injection | [native-aim-delta-register-injection.md](native-aim-delta-register-injection.md) | Developer-only ARM9 register path |
| No-pickup patch | [no-picking-up-specific-items.md](no-picking-up-specific-items.md) | Selective power-up pickup behavior |
| Shadow Freeze | [shadow-freeze-runtime-hook.md](shadow-freeze-runtime-hook.md) | Runtime hook and restore lifecycle |
| Weapon-switch jump suppression | [Feature reference](../../../features/gameplay/no-double-tap-jump.md) | Transient patch outside the registry, wrapped around the legacy touch fallback |

The feature reference is under features because it explains the user-visible
weapon-switch behavior. Its ROM table and evidence boundary are still linked
from this architecture index so a patch audit has one obvious entry point.
