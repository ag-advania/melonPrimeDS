# MelonPrimeDS translation quality audit (2026-07-18)

Scope: every `.inc` under `src/frontend/qt_sdl/MelonPrimeLocalization/inc/`, using the current worktree as the source of truth. Existing historical audit results are not reused.

Review criteria for every data row:

- Preserve the information carried by the English source and Japanese translation: conditions, exceptions, negation, scope, numbers, ranges, placeholders, control names, and parenthetical notes.
- Use natural UI wording in the target language, with special attention to German, Spanish, French, and Italian.
- Preserve official Metroid Prime Hunters terminology where the Nintendo manuals provide a localized term.
- Preserve code-like identifiers and runtime semantics where translating them would make the option misleading.

Status legend: `pending`, `reviewing`, `reviewed`, `fixed`.

| File | Rows | Status | Notes |
|---|---:|---|---|
| MelonPrimeDialogsTranslations.inc | 0 | reviewed | Include manifest only. |
| MelonPrimeDialogsTranslationsAbout.inc | 5 | reviewing | Priority-language wording checked; remaining languages pending. |
| MelonPrimeDialogsTranslationsCheats.inc | 11 | reviewing | Priority-language wording, accelerators, and AR terminology checked; remaining languages pending. |
| MelonPrimeDialogsTranslationsEmulation.inc | 98 | reviewing | Priority-language review completed; corrected renderer/shader/thread/interpolation/controller terminology and several unnatural UI labels. Remaining languages pending. |
| MelonPrimeDialogsTranslationsFirmware.inc | 25 | reviewing | Priority-language colors and month names checked; corrected German “Graublau”. Remaining languages pending. |
| MelonPrimeDialogsTranslationsLan.inc | 18 | reviewing | Priority-language LAN wording checked; corrected articles, host-address terminology, and German lobby status. Remaining languages pending. |
| MelonPrimeDialogsTranslationsNetworkInterface.inc | 52 | reviewing | Priority-language review completed; corrected French instance terminology, timeout/firmware/IP/UI phrasing, articles, and savestate path terminology. Remaining languages pending. |
| MelonPrimeDialogsTranslationsPowerDateTime.inc | 14 | reviewing | Priority-language information checked; corrected French and Italian system date/time phrasing. Remaining languages pending. |
| MelonPrimeDialogsTranslationsRomRam.inc | 40 | reviewing | Priority-language review completed; restored filesystem/code terminology, byte plurals, and the “previous value” table-header meaning. Remaining languages pending. |
| MelonPrimeLocalizationMetalVideoPresets.inc | 2 | reviewing | Priority-language platform and renderer qualifiers checked; remaining languages pending. |
| MelonPrimeObjectTranslations.inc | 0 | reviewed | Include manifest only. |
| MelonPrimeObjectTranslationsAim.inc | 7 | reviewing | Implementation confirms the translations describe the current native TransformRequest path; stale English `.ui` source and extraction override corrected. Remaining rows still under review. |
| MelonPrimeObjectTranslationsDisplayInput.inc | 5 | reviewing | Priority-language information checked; restored patch semantics and official normal-form terminology. Remaining languages pending. |
| MelonPrimeObjectTranslationsFixes.inc | 5 | pending | |
| MelonPrimeObjectTranslationsGameplay.inc | 8 | pending | |
| MelonPrimeObjectTranslationsMethods.inc | 5 | reviewing | Preserved all method conditions in the priority languages; changed Biped wording to the manuals’ Normalform/forma normal/forme normale terminology. Remaining languages pending. |
| MelonPrimeTranslations.inc | 0 | reviewed | Include manifest only; key order verified separately. |
| MelonPrimeTranslationsColorsWeapons.inc | 54 | pending | |
| MelonPrimeTranslationsCommon.inc | 63 | reviewing | Priority-language review completed; corrected Japanese OK, Italian input/output/font wording, and font-filter terminology. Remaining languages pending. |
| MelonPrimeTranslationsCustomHud01.inc | 65 | pending | |
| MelonPrimeTranslationsCustomHud02.inc | 60 | pending | |
| MelonPrimeTranslationsCustomHud03.inc | 62 | pending | |
| MelonPrimeTranslationsCustomHud04.inc | 44 | pending | |
| MelonPrimeTranslationsDeveloper.inc | 5 | reviewing | Biped Fire label aligned with official normal-form terminology in ja/de/es/fr/it; remaining languages pending. |
| MelonPrimeTranslationsHotkeys.inc | 34 | reviewing | Restored omitted sorted-list ordering in ja/de/es/fr/it and official affinity-weapon terminology in de/fr/it. Full row review and remaining languages pending. |
| MelonPrimeTranslationsHudElements.inc | 51 | pending | |
| MelonPrimeTranslationsHudPowerups.inc | 3 | reviewing | Restored official BOOST/TURBO, BOMBEN/BOMBAS/BOMBES/BOMBE, Cape d’occultation, and Zerstörung x 2 terminology in the priority languages; remaining languages pending. |
| MelonPrimeTranslationsMenu.inc | 82 | reviewing | Priority-language review completed; fixed missing “screen” in Bottom only, instance/LAN/firmware/savestate terminology, VPN tunnel meaning, and several unnatural menu labels. Remaining languages pending. |
| MelonPrimeTranslationsMetroidDescriptions.inc | 30 | pending | |
| MelonPrimeTranslationsMetroidGameplay.inc | 36 | pending | |
| MelonPrimeTranslationsMetroidOptions.inc | 34 | pending | |
| MelonPrimeTranslationsOsd.inc | 29 | reviewing | Priority-language review completed; restored patch semantics, official Octolith terms, event-message meaning, and slot terminology. Remaining languages pending. |
| MelonPrimeTranslationsRampsStatus.inc | 21 | reviewing | Priority-language review completed; corrected shareable-TOML and dialog-application phrasing. Remaining languages pending. |
| MelonPrimeTranslationsSections.inc | 33 | reviewing | Priority-language review completed; restored scale/override meaning and replaced literal gameplay-toggle/input wording. Remaining languages pending. |

Current catalog baseline: 971 exact-match rows, 30 object-text rows, 82 selectable languages. Completion requires every row-bearing file above to reach `reviewed` or `fixed`, followed by strict coverage, key-order, ownership, and build verification.
