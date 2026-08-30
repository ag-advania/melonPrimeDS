# Menu language

## This setting controls

The Menu Language control selects the language used by the MelonPrime overlay
and settings UI. It is a host-side presentation setting. It does not rewrite the
language byte inside the running game, and it does not change the language of
the emulated firmware. Those are separate controls:

| Control | Key | Default | Scope |
| --- | --- | --- | --- |
| Menu Language | Metroid.UI.MenuLanguage | -1 | MelonPrime/Qt UI |
| Use firmware language | Metroid.BugFix.UseFirmwareLanguage | false | Guest ROM firmware data |

The complete settings-tab inventory is in the parent
[MelonPrime Settings reference](../melonprime-settings.md). This page records
the implementation contract and the distinction between the two language
systems.

## Values and UI behavior

The persisted value is a language ID. The value -1 means system/default
language. The combo box is populated dynamically from
AllSelectableMenuLanguages rather than from a hard-coded list duplicated in
the .ui file.

The UI layer:

1. reads Metroid.UI.MenuLanguage at startup;
2. maps -1 to the host/system language;
3. exposes only the language IDs returned by the registered menu-language
   list;
4. writes the selected ID when the setting is changed; and
5. reloads the visible menu strings through the normal localization path.

The settings tab therefore must not assume that every locale has a guest-side
equivalent. A newly registered UI locale may be available even when the ROM
firmware language patch has no corresponding byte value.

## What is and is not translated

The setting affects labels, buttons, tooltips, and other strings owned by the
MelonPrime UI. Game text rendered by the ROM remains governed by the ROM's own
language/content. The guest firmware language setting is implemented by
MelonPrimePatchUseFirmwareLanguage.cpp and has ROM-specific safety rules; it is
documented separately in [Firmware language](firmware-language.md).

This separation is important for bug reports. “The settings tab is in the
selected language” is evidence for this setting only. It is not evidence that
the guest firmware or in-game text changed.

## Persistence and reload

The value is a host configuration value. Changing it does not require a ROM
patch, a match join, or a save-data write. The exact moment at which all
already-open widgets repaint depends on the existing localization/reload path;
the setting should not be documented as a guest runtime language switch.

If a language is removed from the selectable list in a future build, a stored
unknown ID must fall back safely rather than indexing the list without bounds
checking. This is also why the default and list ownership belong to the UI
implementation rather than to the ROM patch table.

## Verification checklist

- Start with Metroid.UI.MenuLanguage=-1 and confirm the host/system language is
  selected.
- Select each registered language and verify the settings-tab strings.
- Restart the application and confirm the selected ID persists.
- Confirm that changing this setting does not modify the firmware-language
  address table or trigger a guest patch.
- Test an unavailable/stale language ID and confirm a safe fallback.

## Evidence and related material

Current implementation:

- Source: MelonPrimeDef.h, MelonPrimeInputConfig.cpp, and the localization
  registration used by the settings UI.
- Detailed UI ownership: docs/development/ui/settings-and-edit-mode.md.

Related research in the separate mphCodex repository is useful for the guest
side, but is not copied here:

- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\_JP1_0\current\Language-Setting-All-Versions.md
- C:\Users\Admin\Documents\git\mphCodex\mnt\data\analysis\mphAnalysis\topics\gameplay\SaveFlags\README.md

When the two repositories disagree, the current melonPrimeDS source and its
ROM guard tables are authoritative for the shipped setting.
