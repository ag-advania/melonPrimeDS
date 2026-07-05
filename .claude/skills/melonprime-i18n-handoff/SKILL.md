# MelonPrime i18n continuation skill

## Current pack

```txt
melonprime-i18n-qualityfix-pass6.zip
```

## Status

- Structural coverage is complete for all requested languages.
- pass1 fixed broken `LanguageName: English text` fallback patterns.
- pass2 fixed non-Latin long-description English-heavy rows in main/dialog files.
- pass3 targeted `MelonPrimeObjectTranslations.inc` long technical descriptions.
- pass4/pass5 improved Filipino/Swahili fallback sentences and common UI/HUD labels.
- pass6 used Japanese rows as reference to further localize Filipino/Swahili menu, dialog, HUD, color, font, and setting labels.
- Remaining exact English copies in Filipino/Swahili are mostly technical/HUD/game labels, weapon/map names, abbreviations, aspect ratios, and compact developer-facing terms.

## Completed languages

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

## Audits

```bash
python3 .claude/skills/audit-melonprime-i18n-qualityfix-pass6.py
python3 .claude/skills/audit-melonprime-localization.py
cmake --build build-mac --parallel 4
```
