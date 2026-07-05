# MelonPrime i18n continuation skill

Use this skill when continuing the MelonPrimeDS `.inc` localization pack work.

## Core rule

Do **not** fake translation coverage.

A language is only "complete" when all 996 rows have a target-language entry:

- `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc` — 703 rows
- `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc` — 30 rows
- `src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc` — 263 rows

Coverage complete does **not** mean native-reviewed. Say "coverage complete / native review recommended" when appropriate.

## Current valid cumulative pack chain

```txt
ChineseHongKong / zh-HK / 中文（香港）
Filipino / fil / Filipino
Swahili / sw / Kiswahili
Croatian / hr / Hrvatski
Icelandic / is / Íslenska
Hindi / hi / हिन्दी
Bengali / bn / বাংলা
Urdu / ur / اردو
Punjabi / pa / ਪੰਜਾਬੀ
Telugu / te / తెలుగు
Persian / fa / فارسی
Malay / ms / Bahasa Melayu
Marathi / mr / मराठी
Tamil / ta / தமிழ்
Gujarati / gu / ગુજરાતી
Kannada / kn / ಕನ್ನಡ
Odia / or / ଓଡ଼ିଆ
Malayalam / ml / മലയാളം
Burmese / my / မြန်မာ
Uzbek / uz / Oʻzbek
Azerbaijani / az / Azərbaycanca
Nepali / ne / नेपाली
Sinhala / si / සිංහල
Khmer / km / ភាសាខ្មែរ
Lao / lo / ລາວ
Armenian / hy / Հայերեն
Albanian / sq / Shqip
Georgian / ka / ქართული
Bosnian / bs / Bosanski
Serbian / sr / Српски
Macedonian / mk / Македонски
Bulgarian / bg / Български
Belarusian / be / Беларуская
Maltese / mt / Malti
Mongolian / mn / Монгол
Latvian / lv / Latviešu
Lithuanian / lt / Lietuvių
Afrikaans / af / Afrikaans
Irish / ga / Gaeilge
Assamese / as / অসমীয়া
Amharic / am / አማርኛ
Estonian / et / Eesti
Kazakh / kk / Қазақша
Catalan / ca / Català
Kyrgyz / ky / Кыргызча
Slovak / sk / Slovenčina
Slovenian / sl / Slovenščina
Zulu / zu / isiZulu
Basque / eu / Euskara
Hebrew / he / עברית
```

Latest expected output after this pack:

```txt
melonprime-i18n-he.zip
```

## Remaining requested language pool

```txt
None. All requested languages are complete.
```

## Current newest language

```txt
Hebrew
```

## Notes

- Hebrew is right-to-left text. The text entries are Hebrew, but full RTL UI layout depends on the Qt/UI side supporting RTL direction and font shaping.
- Technical terms and proper names such as OpenGL, VSync, ARM9 BIOS, HUD, weapon names, and game mode names are intentionally kept readable.

## Audit

```bash
python3 .claude/skills/audit-melonprime-i18n-he-coverage.py
python3 .claude/skills/audit-melonprime-localization.py
cmake --build build-mac --parallel 4
```
