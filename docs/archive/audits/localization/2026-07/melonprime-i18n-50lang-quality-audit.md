# MelonPrime 50-language expansion — translation quality audit (2026-07-05)

Structural status: complete. All three localization `.inc` files
(`MelonPrimeLocalization/MelonPrimeTranslations.inc`,
`MelonPrimeLocalization/MelonPrimeObjectTranslations.inc`,
`MelonPrimeLocalizationMelondsDialogs.inc`) have 100% row coverage for all 50
new languages (Afrikaans through Lithuanian, plus ChineseHongKong), verified
by exact key/value diff against the pre-expansion files: zero rows lost, zero
existing (original 32 + zh-Hant) values altered, all 50 additions land on
every row (703/703, 30/30, 263/263).

Content quality is uneven. `tools/audit-melonprime-i18n-he-coverage.py`
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

## Update (2026-07-06): `melonprime-i18n-qualityfix-pass6` applied

A 6-pass quality-fix series (`melonprime-i18n-qualityfix-pass1..6.zip`) targeted
the 14 worst languages from the table above (the former Severe/Bad/Moderate
tiers: Filipino, Swahili, Amharic, Kazakh, Hebrew, Basque, Slovak, Slovenian,
Zulu, Catalan, Odia, Estonian, Assamese, Kyrgyz). Verified independently before
applying (same method as before: structural key/value diff first, then
re-running the leak/plain-English detection scripts against the new file) —
did **not** just trust the pack's own self-reported `..._after: 0` numbers.

Results, re-measured directly against `MelonPrimeTranslations.inc` (703 rows):

- **Display-name leak: fully fixed, 0 remaining, for all 14 targeted
  languages.** No garbled `"<DisplayName>: English text"` artifacts left in
  any of them.
- **Filipino 64.7% → 18.3% bad, Swahili 60.6% → 9.5% bad** — real
  re-translation happened here, not just artifact cleanup; both are now
  at or better than the old "minor gap" tier.
- The other 12 targeted languages improved more modestly (roughly 3-7
  percentage points each, e.g. Zulu 34.1% → 31.2%, Hebrew 33.6% → 26.7%,
  Odia 20.6% → 17.2%): the leak artifact is gone, but a meaningful chunk of
  what used to be leaked text is now honest plain-English fallback rather
  than a genuine new translation. Still a net improvement and strictly safer
  (no more broken-looking garbled text), but these are not "done" yet.
- The ~36 untouched "minor gap" tier languages are confirmed byte-for-byte
  unchanged (re-verified, not just assumed) — this pass did not touch or
  regress them.

Updated per-language tier table (leak+plainEN rate, sorted worst-first) as of
this pass:

```txt
Zulu 31.2%, Slovak 30.7%, Slovenian 30.7%, Basque 30.6%, Kazakh 27.6%,
Hebrew 26.7%, Amharic 25.5%, Catalan 19.3%, Filipino 18.3%, Odia 17.2%,
Estonian 17.1%, Assamese 16.6%, Kyrgyz 14.7%, Maltese 13.5%, Afrikaans 12.7%,
Irish 12.5%, Lithuanian 12.1%, Albanian/Mongolian 11.9%, Azerbaijani 11.8%,
Uzbek/Bulgarian/Belarusian/Bosnian 11.7%, Armenian/Serbian/Macedonian 11.5%,
Latvian 10.8%, Georgian/Sinhala/Nepali/Lao 10.7%, Swahili 9.5%, Khmer 9.1%,
Malayalam/Burmese 9.1%, Malay 9.0%, Kannada/Gujarati/Tamil 7.3-7.4%,
Urdu/Telugu/Punjabi/Hindi/Bengali/Marathi 6.7%, Croatian 6.5%, Persian 5.8%,
Icelandic 5.5%, ChineseHongKong 2.8%.
```

Installed the accompanying `tools/audit-melonprime-i18n-qualityfix-pass1..6.py`
scripts and the updated `melonprime-i18n-handoff` skill alongside this data.

## Follow-up (not yet done)

- Zulu, Slovak, Slovenian, Basque, Kazakh, Hebrew, Amharic still sit at
  25-31% bad (leak-free now, but still meaningfully incomplete) — highest
  priority for a further real-translation pass, in that order.
- Catalan, Odia, Estonian, Assamese, Kyrgyz sit at 15-19% — next priority.
- The recurring ~11 "hard keys" that leak in the ~36 untouched languages are
  still present (this pass never targeted them) — fixing real translations
  for those exact 11 English source strings once would close most of the
  "minor gap" tier's remaining distance to 100% in one pass across ~36
  languages, same as noted before this update.
- Filipino/Swahili no longer need a full re-translation pass — they're now
  comparable to or better than the "minor gap" tier.

## Update (2026-07-06): `melonprime-i18n-release-candidate` applied — Filipino/Swahili polish, review scaffolding not committed

A much larger follow-on pack (`melonprime-i18n-release-candidate.zip`, base
`melonprime-i18n-phase9-native-review-packs.zip`) arrived, covering phases 4a
through 10 of the external session's own workflow: automated review-queue
generation (16,870 flagged rows) plus static-pattern audits (font/RTL/clipping
block checks) across **all** selectable languages, not just the 50 new ones —
its `README.md` even lists established languages like Greek, Russian, German,
French, Italian as having review-flagged (P2/P3) rows.

Despite that broad review scope, re-verified with the same structural
key/value diff used for every prior pack (zero rows lost/added across all
966 `kTranslations` keys — 703 direct + 263 spliced from
`MelonPrimeLocalizationMelondsDialogs.inc` — and 30 `kObjectTextTranslations`
keys) and found **only Filipino and Swahili actually changed content** (63 +
13 = 76 rows in `kTranslations`, 13 in `kObjectTextTranslations`). Every other
language, including the ones the README lists as having P2/P3 review flags,
is byte-for-byte unchanged — the phase6-10 work for those was flagging for
future native review, not auto-translating.

Spot-checked the Filipino/Swahili diffs directly (not just trusting the
pack's `coverage: OK` claim): these fix a specific artifact from the earlier
pass6 pass — partial word-substitution leftovers where only isolated English
words got swapped for a native word inside an otherwise-English sentence
(e.g. `"Controls how the laro's kasalukuyang aim direction follows..."`,
`"Checked: gamitin the native ARM9 laro function hook..."`). The new values
are fully natural Filipino/Swahili sentences
(`"Kinokontrol kung paano sumusunod ang kasalukuyang direksyon ng aim ng
laro sa target na direksyon ng aim."`). No new leak or plain-English
artifact was introduced in either language.

**Not committed to the repo**: the pack's `i18n_quality_phase4a/`through
`i18n_quality_phase9/` review-queue data (~80MB of CSV/JSON flagged-row
dumps) and the top-level `*_stats.json` / `QUALITYFIX_PASS*_REPORT.json`
files. These are the external session's own working artifacts (its review
queue, not translation content or code), the same category as game ROMs or
screenshots per this repo's asset policy — kept out of git history for size,
consistent with not committing the equivalent data from prior passes either.
The `audit-melonprime-i18n-phase5a..10.py` scripts were kept (small, and
useful as a record of the methodology), but running them will show `[FAIL]
missing ...` lines for the uncommitted review-queue paths — that is expected
and not a regression; only `audit-melonprime-localization.py` (the strict
gate) is required to be green, and it is.

**Manual UI verification is explicitly still pending** — the pack's own
`README.md` states the external session cannot launch the Qt app or visually
inspect rendering, so RTL punctuation/layout, complex-script glyph shaping,
and clipping candidates flagged in the phase 8 review data (11,125 font,
2,153 RTL, 3,153 clipping candidates) have **not** been visually confirmed in
this session either — this was a data/content-quality pass only. If a future
session inherits `i18n_quality_phase10/ui_verification_checklist.csv`-style
manual UI review work, treat it as a distinct, still-open task from this
content pass.

Applied, strict audit green, macOS build clean, launch/exit smoke clean.
