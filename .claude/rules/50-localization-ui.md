# Localization and UI

- MelonPrime display text is owned by the localization module. Config/TOML keys, schema identifiers, enum names, and patch identifiers are not translated.
- Do not edit upstream-owned dialog implementations solely for localization. Use `LocalizeMelonDsDialog()` and the established window/dialog integration points.
- Preserve stable `QAction` identity and `QAction::MenuRole`. On macOS, About/Preferences/Quit roles affect native menu placement; translated text must not be used as an action identifier.
- Dynamic text must update when the selected language changes. Rebuild or refresh menus, dialogs, tooltips, status text, and cached labels through the existing localization refresh path.
- New languages must update the language enum/table, locale detection, display name, translation content, fallback behavior, and coverage/quality audits together.
- Preserve placeholders, accelerators, punctuation intent, line breaks, and rich-text markup. Never claim language quality from key coverage alone.
- HUD/menu settings must keep labels, tooltips, default/reset behavior, serialization keys, and edit-mode surfaces consistent.

Procedures and evidence: [`docs/development/localization/add-menu-language.md`](../../docs/development/localization/add-menu-language.md), [`docs/plans/localization/localization-refactor-plan.md`](../../docs/plans/localization/localization-refactor-plan.md), and [`docs/archive/audits/localization/`](../../docs/archive/audits/localization/).
