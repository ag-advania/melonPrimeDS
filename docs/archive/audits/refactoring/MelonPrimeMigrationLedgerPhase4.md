# MelonPrime Migration Ledger Phase 4

Date: 2026-07-02
Plan: V3 Phase 4

This note is the owner list for compatibility-only config reads. The code stays
in its current layer because moving it across UI/runtime/patch boundaries would
change evaluation timing.

## Release Gate

Latest local release tag checked during Phase 4:

| Tag | Commit | Date | Note |
|---|---|---|---|
| `Release3.4.1` | `0294f84b` | 2026-06-27 | Latest local release tag before the V3 cleanup commits |

Decision: do not delete compatibility reads in Phase 4. Although a release after
the original V2 note exists, deleting now would break users who jump from an
older config directly to a post-V3 build before saving once. Revisit after the
first release that includes this Phase 4 ledger and the save-side legacy bool
normalization.

## Legacy Keys

| Key | Current behavior | Locations | Phase 4 decision |
|---|---|---|---|
| `Metroid.Aim.Enable.InstantAimFollow` | Old bool maps to `LowLatencyMode = ImmediateSync` when the new enum is still `Off`; public builds save the legacy bool back to `false`; developer builds may still mirror mode `3`. | `Config.cpp` default, `MelonPrimeDef.h::CfgKey`, `InputConfig/MelonPrimeInputConfig.cpp` load, `InputConfig/MelonPrimeInputConfigConfig.cpp` save, `MelonPrime.cpp::ReloadConfigFlags`, `MelonPrimeArm9Hook.cpp`, `MelonPrimePatchInstantAimFollow.cpp` | Keep. Do not add new reads. Delete only after a post-V3 release has given old configs a save cycle, then run S17 with migrated and unmigrated TOML samples. |
| `Metroid.Visual.HudFontSize` legacy migration path | Pre-anchor-system configs map pixel size to `HudTextScale` when `HudHpAnchor` is absent and `HudHpX` exists. The live HUD schema still owns `HudFontSize` for system/file font base size, so only the migration-side interpretation is legacy. | `EmuInstance.cpp` migration block; live schema/runtime/UI still use `MP_HUD_PROP_KEY_HudFontSize` normally | Keep. The migration is no-op after anchors exist and is guarded by `ShouldMigrateLegacyHudAnchors()`. Delete only if old pre-anchor configs are intentionally no longer supported. |
| Pre-anchor HUD positions | Old absolute `Hud*X/Y` values are preserved by setting anchor keys to `0` for configs that saved `HudHpX` before anchor keys existed. | `EmuInstance.cpp::ShouldMigrateLegacyHudAnchors()` and constructor migration block | Keep. It is no-op for migrated users and fresh configs. Guard condition was consolidated in Phase 4. |

## Enum Normalization

These are current-format enum/config gates, not delete-ready legacy keys. They
remain outside the binding table because their UI state is not a direct mirror
of the saved value.

| Setting | Behavior | Locations |
|---|---|---|
| `Metroid.Aim.LowLatencyMode` | UI clamps modes, hides developer-only mode in public builds, runtime normalizes developer-only mode to `ImmediateSync`, and low-latency modes require `DisableMphAimSmoothing`. | `InputConfig/MelonPrimeInputConfig.cpp`, `InputConfig/MelonPrimeInputConfigConfig.cpp`, `MelonPrime.cpp`, `MelonPrimeArm9Hook.cpp`, `MelonPrimePatchInstantAimFollow.cpp` |
| `Metroid.Input.WeaponSwitchMethod` | Checkbox UI maps to enum `0/1`; runtime enables native weapon switching when not legacy. | `InputConfig/MelonPrimeInputConfig.cpp`, `InputConfig/MelonPrimeInputConfigConfig.cpp`, `MelonPrime.cpp`, `MelonPrimeArm9Hook.cpp` |
| `Metroid.Input.BipedFireMethod` | Developer-only checkbox maps to enum `0/1`; public builds save legacy input. | `InputConfig/MelonPrimeInputConfig.cpp`, `InputConfig/MelonPrimeInputConfigConfig.cpp`, `MelonPrime.cpp`, `MelonPrimeArm9Hook.cpp` |
| `Metroid.Input.ZoomMethod` | Two checkbox modes map to enum `0/1/2`; runtime distinguishes preset binding and native toggle. | `InputConfig/MelonPrimeInputConfig.cpp`, `InputConfig/MelonPrimeInputConfigConfig.cpp`, `MelonPrime.cpp`, `MelonPrimeArm9Hook.cpp` |
| `Metroid.Input.Enable.DirectAltFormTransform` | UI checkbox/runtime gate; save path is non-mirror because the custom checkbox can replace the generated UI widget. | `InputConfig/MelonPrimeInputConfig.cpp`, `InputConfig/MelonPrimeInputConfigConfig.cpp`, `MelonPrime.cpp`, `MelonPrimeArm9Hook.cpp` |

## S17 Status

No compatibility key was deleted in Phase 4, so S17 was limited to code-path
review and build verification. Full S17 with migrated/unmigrated TOML samples is
required in the future deletion commit.
