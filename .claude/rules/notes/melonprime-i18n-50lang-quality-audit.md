# MelonPrime 50-language expansion — translation quality audit (2026-07-05)

Structural status: complete. All three localization `.inc` files
(`MelonPrimeLocalization/MelonPrimeTranslations.inc`,
`MelonPrimeLocalization/MelonPrimeObjectTranslations.inc`,
`MelonPrimeLocalizationMelondsDialogs.inc`) have 100% row coverage for all 50
new languages (Afrikaans through Lithuanian, plus ChineseHongKong), verified
by exact key/value diff against the pre-expansion files: zero rows lost, zero
existing (original 32 + zh-Hant) values altered, all 50 additions land on
every row (703/703, 30/30, 263/263).

Content quality is uneven. `.claude/skills/audit-melonprime-i18n-he-coverage.py`
(installed alongside this data) only checks row-count coverage — it cannot
detect an entry that is present but wrong. Two defect patterns were found by
inspecting actual string content, not just presence:

1. **Plain English left in place** — the translated `text` field is byte-identical
   to the English source key. Expected for a handful of genuinely code-like
   values (`OK`, `%1`, literal config key names like `Metroid.Volume.Music`),
   not for full sentences.
2. **Display-name leak** — the string literally starts with the language's own
   native display name followed by `:` or ` -`, with the rest left in English,
   e.g. Amharic `"አማርኛ: Zoomed (Scope)"` (`አማርኛ` means "Amharic"). This affects a
   recurring fixed set of ~11 "hard" keys (`"Keyboard && mouse mappings"`,
   `"[Metroid] (H) UI No (Enter Starship)"`, `"Disabled (vanilla 25)"`, etc.)
   in nearly every one of the 50 languages, and a much larger set in the worst
   ones below — this is a systematic artifact of whatever tool produced the
   pack, not a one-off typo.

## Per-language quality (MelonPrimeTranslations.inc, 703 rows; `leak` = pattern 2, `plainEN` = pattern 1)

| Tier | Languages | leak+plainEN rate |
|---|---|---|
| Severe (needs a real re-translation pass) | Filipino, Swahili | 61–65% |
| Bad | Slovak, Slovenian, Zulu, Basque, Hebrew, Kazakh, Amharic | 31–34% |
| Moderate | Catalan, Odia, Estonian, Assamese, Kyrgyz | 15–23% |
| Minor gap (same recurring ~11 hard keys + a few legitimate technical terms) | remaining ~36 languages (Afrikaans, Irish, Icelandic, Azerbaijani, Albanian, Armenian, Uzbek, Urdu, Georgian, Kannada, Gujarati, Khmer, Croatian, Sinhala, Serbian, Tamil, ChineseHongKong, Telugu, Nepali, Punjabi, Hindi, Bulgarian, Belarusian, Bengali, Persian, Bosnian, Macedonian, Marathi, Malayalam, Maltese, Malay, Burmese, Mongolian, Lao, Latvian, Lithuanian) | 3–13% |

Regenerate this table with the script pattern in the session history if a
follow-up translation pass changes these numbers — no script was checked in
for it since it was a one-off investigation before deciding whether to apply
the pack.

## Decision (2026-07-05)

Project owner chose to apply the pack in full and record this as a known gap,
rather than hide the weak languages behind `selectable=false` or wait for a
fix. All 50 languages are `selectable=true` in
`MelonPrimeLanguageRegistry.cpp`. Untranslated/leaked strings fall back to
plain English display (not to another language, and not a crash/blank), so
the failure mode for an affected string is "still in English" — visible, but
not misleading about which language was picked.

## Formatting note (cosmetic, not a correctness issue)

The pack's generation tool appended all 50 new-language entries for a row by
cramming them onto the end of the previous last entry's line (some resulting
lines are 10,000+ characters), then padded with blank lines to preserve some
external line-count invariant. `MelonPrimeTranslations.inc` went from 13
blank lines (original) to 34,460. This is valid C++ (newlines are irrelevant
to the compiler) and does not affect behavior, but makes the file effectively
un-diffable and un-blameable going forward for the affected rows.

**Do not attempt a mechanical reformat pass without extreme care**: a
same-day attempt at reformatting this file (splitting each `{MenuLangId::X,
"..."},` onto its own line via a Python regeneration script) introduced two
real bugs before being reverted — first it silently dropped the
`#include "MelonPrimeLocalizationMelondsDialogs.inc"` line (which lives
*inside* the `kTranslations` array body, spliced in via `#include`, and is
not itself a `{...}` row so a row-based parser skips over it unless
explicitly preserved), then a corrected version introduced a stray bare `,`
immediately before that `#include` line. Both were caught by re-checking
`grep -n "MelonPrimeLocalizationMelondsDialogs"` and a full rebuild before
committing, and the fix was to revert to the pack's original (verified,
already-compiling) file rather than risk a partial regeneration. If this is
worth fixing later, verify with a full row-by-row key/value diff against the
current file (same method as the "Structural status" verification above)
*and* a full rebuild before trusting the result — the compiler will not
necessarily catch a dropped `#include` if the missing content is not itself
referenced anywhere at compile time in an error-producing way (it would
simply lose 263 translation rows' worth of language coverage in that TU).

## Follow-up (not yet done)

- Filipino and Swahili need a real translation pass, not a touch-up.
- The 7 "Bad" tier languages need the display-name-leak entries fixed
  (mechanically: strip the `"<DisplayName>: "` / `"<DisplayName> - "` prefix,
  then translate the remainder — the prefix-strip alone does not fix
  translation completeness, it just removes the confusing artifact).
- The recurring ~11 "hard keys" that leak in almost every language are worth
  fixing first since fixing them once (get real translations for those exact
  11 English source strings) closes most of the "minor gap" tier's remaining
  distance to 100% in one pass across ~36 languages.
