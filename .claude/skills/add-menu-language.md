# Add a MelonPrime Menu Language

## Scope

Use this when adding a new selectable UI language to MelonPrimeDS's menu/settings
localization system (`MelonPrime::UiText`), or when picking up a translation-content
pack from an external source (another AI session, a contributor, a generated batch).

This is infrastructure + process documentation. It does not itself contain
translation content.

Primary files:

| File | Role |
|------|------|
| `src/frontend/qt_sdl/MelonPrimeLocalization.h` | `MenuLangId` enum — the persisted, ordered list of languages |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeLanguageRegistry.h` | `LanguageInfo` struct fields, `SplashFontGroup` enum |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeLanguageRegistry.cpp` | `kLanguageInfos[]` table, `LanguageTagToMenuLang()`, `QLocaleLanguageToMenuLang()`, macOS `applePrefixes[]` |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeSplashLocalization.cpp` | `NoRomSplashUiFont()` — per-script font family lists |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc` | `kTranslations[]` — exact-match short-string translations |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc` | `kObjectTextTranslations[]` — objectName-keyed long descriptions |
| `src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc` | Extra translation rows for upstream melonDS dialogs — **spliced into `kTranslations` via `#include`, lives in `qt_sdl/` root, not the `MelonPrimeLocalization/` subdir** |
| `src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslationDynamic.cpp` | `Tr()`-family dynamic/decorated string handling (instance labels, slot labels, camera suffixes, etc.) |
| `.claude/skills/audit-melonprime-localization.py` | **Strict gate.** Must stay green. Covers the original languages + zh-Hant only (see "Scope note" below) |

Related docs (not indexed elsewhere — read these too):

- [melonprime-localization-refactor-plan.md](../rules/melonprime-localization-refactor-plan.md) — the file-split plan this system followed (`MelonPrimeLocalization.cpp` → facade + `MelonPrimeLocalization/*`)
- [melonprime-localization-refactor-audit.md](../rules/melonprime-localization-refactor-audit.md) — audit of that split, including the `zh-Hant` fallback-lookup fix
- [notes/melonprime-i18n-50lang-quality-audit.md](../rules/notes/melonprime-i18n-50lang-quality-audit.md) — live tier table for the 50-language expansion's translation quality; update this file (not this skill) when a new quality-fix pass lands

## The three critical rules

1. **`MenuLangId` is append-only.** Persisted user config stores the raw enum int
   (`MenuLangId::First = 100` and up). Inserting a new value in the middle silently
   shifts every later language's saved selection to a different language on the
   user's next launch. Always add new languages at the end, immediately before `Count`.
2. **A pack reporting "100% coverage" has NOT been quality-checked.** Coverage audits
   (row presence, non-empty fields) cannot detect wrong-language content, plain
   English left in a "translated" field, or a broken fallback artifact. Always
   independently verify actual string *content* before applying a pack — see
   "Verifying a translation pack" below. This has bitten real packs twice in this
   repo's history (a 0-6% real-translation batch that self-reported 100%, and a
   systematic "display-name leak" artifact in an otherwise-good batch).
3. **Never mechanically reformat the `.inc` translation files.** A row-based parser
   used to "prettify" these files can silently drop the
   `#include "MelonPrimeLocalizationMelondsDialogs.inc"` line — it lives inside the
   `kTranslations` array body as non-brace literal text, not as a `{...}` row, so a
   naive regenerator skips it and quietly loses 263 rows' worth of coverage with no
   compiler error. This has already happened once in this repo and was caught only
   by `grep -n "MelonPrimeLocalizationMelondsDialogs"` before commit. If you must
   reformat, diff every row's key/value set before and after, and grep for that
   `#include` line, before trusting the result.

## Adding a new language: step by step

### 1. `MenuLangId` enum (`MelonPrimeLocalization.h`)

Append one value before `Count`:

```cpp
NewLanguageName,   // appended — never insert earlier in the list
```

### 2. `LanguageInfo` entry (`MelonPrimeLanguageRegistry.cpp`)

Add a row to `kLanguageInfos[]`:

```cpp
{
    MenuLangId::NewLanguageName,
    "xx",                      // BCP-47-ish stable code, for reference/debugging
    "Native Display Name",     // shown in the language combo box
    MenuLangId::NewLanguageName, // translationBase: itself if it has its own translation set,
                                 // or an existing language to inherit its coverage by fallback
                                 // (e.g. a regional variant -> its base language)
    true,                       // selectable
    TextDirection::LeftToRight, // RightToLeft for Arabic/Hebrew/Persian/Urdu-family
    false,                      // requiresShapedSplash — true for complex/reordering scripts
    SplashFontGroup::Latin,     // pick an existing group or add a new one (step 4)
},
```

If the new language is a **regional variant of an existing one** (e.g. a new
Chinese/Arabic/Portuguese/Spanish/English variant), set `translationBase` to the
existing base language instead of itself. It then inherits ~100% of that language's
translation coverage through the fallback chain with zero new translation content
required — this is how `ChineseHongKong` (`translationBase = ChineseTraditional`)
and the English/Chinese regional variants work.

### 3. Locale detection (`MelonPrimeLanguageRegistry.cpp`)

Update all three detection paths so the OS locale actually resolves to the new
language:

- `LanguageTagToMenuLang(tag)` — BCP-47 tag matching. **Watch for prefix
  collisions**: if a new tag is a strict string-prefix of an existing check (e.g.
  `"fil"` vs `"fi"`), the shorter/existing check must not fire first, so place the
  more specific tag's check earlier in the function.
- `QLocaleLanguageToMenuLang(lang)` — add a `case QLocale::NewLanguage: return
  MenuLangId::NewLanguageName;`. Confirm the `QLocale::Language` enum value exists
  in Qt's `qlocale.h` before wiring it (all languages added in the 2026-07 fifty-
  language expansion were confirmed present).
- macOS `applePrefixes[]` array — add the language's prefix, same ordering caution
  as the BCP-47 tag matching above (more specific before more general).
- For a region variant (`zh-HK` style), add the region check **before** the
  existing base-language catch-all so the variant isn't swallowed by its parent.

### 4. Splash font group (only if no existing `SplashFontGroup` fits)

If the new language needs a distinct script/font family list for the no-ROM splash
screen, add a new `SplashFontGroup` enum value
(`MelonPrimeLanguageRegistry.h`) and a matching `case` block in
`NoRomSplashUiFont()` (`MelonPrimeSplashLocalization.cpp`) with an ordered list of
font family names to try. Reuse an existing group when the script is shared
(e.g. Persian/Urdu reuse `Arabic`; `ChineseHongKong` reuses `ChineseTraditional`).

### 5. Get translation content

Options, roughly in order of reliability observed in this repo's history:

- A pre-made pack of full replacement `.inc` files from an external session —
  fastest, but **must** be verified per "Verifying a translation pack" below
  before applying.
- Extract translation tasks with the existing tooling
  (`tools/melonprime_extract_all_ai_translation_tasks.py` if present in the repo;
  check `tools/` and regenerate the pattern if it was removed) and hand the
  per-language JSONL to a translation-capable session, then apply with
  `tools/melonprime_apply_ai_translation_jsonl.py`.
- Direct subagent dispatch (`Agent` tool) for translation content is **not
  reliable for bulk work**: dispatching many parallel subagents to translate in
  this repo's history mostly reported "completed" while having silently failed
  mid-execution due to session/rate limits, with only file output on disk proving
  which ones actually succeeded. If you try this, verify every expected output
  file exists and has the right row count (`wc -l`) before trusting a
  "completed" notification — do not trust the notification text alone.

### 6. Verifying a translation pack (do this before applying, every time)

Coverage (every field non-empty) is necessary but nowhere near sufficient. Run
both of these independently — do not trust a pack's self-reported numbers:

- **Structural diff**: parse both the pre-pack and post-pack `.inc` files into
  `(key, {lang: value})` tuples (brace-depth matching + a `{MenuLangId::X, "..."},`
  regex works well) and diff by key. Confirm zero rows lost, zero values changed
  for any language you did NOT intend to touch, and every intended language
  present on every row.
- **Content-quality check**, per targeted language:
  - *Plain-English rate*: count rows where the translated value is byte-identical
    to the English source key. Some are legitimate (`OK`, literal `%1`, config key
    names) — a full sentence being byte-identical to English is not.
  - *Display-name leak*: check whether the string starts with the language's own
    native display name followed by `:` or ` -` (e.g. Amharic `"አማርኛ: Zoomed
    (Scope)"`). This is a broken-fallback artifact, not a translation, and has
    shown up as a systematic pattern across dozens of languages in at least one
    real pack.
  - Spot-check a handful of short strings against a script/language you can
    verify are NOT another, related language's text pasted in (this caught
    literal Thai script submitted as Khmer and Lao translations in one rejected
    batch, and Spanish/Finnish/Czech text submitted as Basque/Estonian/Slovenian).

If quality is uneven, present a per-language tier breakdown to the project owner
and let them decide whether to apply everything and track gaps, apply only the
good subset, or wait for a fix — this is a product decision, not something to
resolve unilaterally.

### 7. Dynamic-text fallback wording (only if a dynamic string needs distinct wording)

`TrInstanceDialogText()` / `TrSpecialDynamicText()` in
`MelonPrimeTranslationDynamic.cpp` switch on
`ResolveTranslationLanguage(ActiveMenuLanguage())` — the **resolved** (fallback
base) language, not the actual active one. This means a `case
MenuLangId::YourRegionalVariant:` label inside that switch is dead code whenever
`YourRegionalVariant`'s `translationBase` resolves to something else; it will
never match itself. If a regional variant needs its own wording for a specific
dynamic string, add an explicit check **before** the switch:

```cpp
if (ActiveMenuLanguage() == MenuLangId::YourRegionalVariant)
    return QStringLiteral("distinct wording here");
```

then let the existing `switch (ResolveTranslationLanguage(...))` handle the
fallback case as before.

### 8. Run the audits

- `python3 .claude/skills/audit-melonprime-localization.py` — **must stay green.**
  This is the strict, always-on gate.
  - **Scope note**: this script intentionally does NOT check the 50-language
    expansion's representative-key coverage (doing so before those languages had
    any translation content would have failed immediately). If your new
    language is one of the "big batch" languages, its coverage lives in a
    separate WARN-only script (check for
    `audit-melonprime-all-new-language-coverage.py` or an
    `audit-melonprime-i18n-*-coverage.py` variant in `.claude/skills/`).
  - If you are adding a genuinely new, permanent, first-class language (not part
    of an experimental big batch), consider extending this strict script's
    representative-key checks to include it once its content quality is solid.
- Any WARN-only coverage script relevant to the batch you touched.
- Rebuild (`build-macos-dev.sh` or the Windows/Linux equivalent per
  [build.md](../rules/build.md)) and smoke test: launch, open the language combo
  box, select the new language, confirm the settings dialog and no-ROM splash
  render without mojibake, confirm RTL/shaping if applicable, restart and confirm
  the selection persisted.

## Checklist

- [ ] `MenuLangId` value appended at the end (not inserted mid-enum)
- [ ] `LanguageInfo` row added with correct `translationBase`, direction, and splash font group
- [ ] `LanguageTagToMenuLang()` updated, prefix-collision order checked
- [ ] `QLocaleLanguageToMenuLang()` updated
- [ ] macOS `applePrefixes[]` updated, prefix-collision order checked
- [ ] New `SplashFontGroup` + font list added, only if no existing group fits
- [ ] Translation pack verified independently (structural diff + content-quality check), not just coverage-audited
- [ ] Dynamic-text fallback checked for any string needing distinct regional-variant wording
- [ ] `audit-melonprime-localization.py` green
- [ ] Relevant WARN-only coverage audit run
- [ ] Build + launch/exit + language-switch + persistence smoke test
- [ ] If quality is uneven, tier breakdown presented to the project owner before deciding whether to apply
