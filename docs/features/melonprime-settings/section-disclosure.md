# Settings-section disclosure controls

## Purpose

The arrow-shaped headers in the MelonPrime Settings tab are collapsible
section controls. They persist whether a section is expanded; they do not
enable, disable, or apply the feature controls inside that section.

This distinction is useful when a feature appears “missing” after reopening
the dialog: first expand the relevant section, then inspect the feature's own
checkbox or combo value.

## Keys and defaults

| Section | Key | Default |
| --- | --- | --- |
| Sensitivity | Metroid.UI.SectionSensitivity | expanded |
| Bug Fixes | Metroid.UI.SectionBugFix | expanded |
| Game Feature Improvements | Metroid.UI.SectionGameFeature | expanded |
| Disable Features | Metroid.UI.SectionDisableFeatures | expanded |
| Power-Up Pickup Effects | Metroid.UI.SectionPowerUpPickupEffects | expanded |
| Gameplay Toggles | Metroid.UI.SectionGameplay | expanded |
| Video Quality | Metroid.UI.SectionVideo | expanded |
| Volume | Metroid.UI.SectionVolume | expanded |
| License Apply | Metroid.UI.SectionLicense | expanded |
| Input Settings | Metroid.UI.SectionInputSettings | collapsed |
| Input Method | Metroid.UI.SectionInputMethod | collapsed |
| Screen Sync | Metroid.UI.SectionScreenSync | collapsed |
| Cursor Clip Settings | Metroid.UI.SectionCursorClipSettings | collapsed |
| In-Game Apply | Metroid.UI.SectionInGameApply | collapsed |
| In-Game Aspect Ratio | Metroid.UI.SectionInGameAspectRatio | collapsed |
| Low HP Warning | Metroid.UI.SectionLowHpWarning | collapsed |
| Developer Only | Metroid.UI.SectionDeveloperOnly | collapsed |

The default state is the source-defined initial UI state. A user's saved
expanded/collapsed choice should not be interpreted as a feature default.

## Save behavior

The collapsible-section helper owns these values and restores them when the
dialog is reopened. The values are local UI configuration. They do not enter
the guest settings reconciliation path, the ARM9 patch registry, or the
renderer preset transaction.

The Input Method and Developer Only sections are also affected by build
availability. A section's disclosure state can be stored even if all of its
controls are hidden or disabled in a release build; that does not enable a
developer hook.

## Verification checklist

- Expand and collapse every section.
- Restart/reopen the settings dialog and confirm each state persists.
- Confirm expanding a section does not toggle the feature inside it.
- Confirm feature parent/child wiring still works after a section is reopened.
- Confirm developer-only visibility is still controlled by
  MELONPRIME_ENABLE_DEVELOPER_FEATURES.

## Evidence

Current source:

- MelonPrimeDef.h
- InputConfig/MelonPrimeInputConfig.cpp
- InputConfig/MelonPrimeInputConfigConfig.cpp
- Config.cpp

This is a host UI state and has no mphCodex guest address reference.
