#!/usr/bin/env python3
from __future__ import annotations
import ast
import csv
import re
from pathlib import Path
from collections import Counter

FILES = {
    "MelonPrimeTranslations.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "MelonPrimeObjectTranslations.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "MelonPrimeLocalizationMelondsDialogs.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}

REVIEW_MATRIX = Path("i18n_quality_phase6/phase6_high_risk_review_matrix.csv")
PATCHES_CSV = Path("i18n_quality_phase6/phase6_applied_semantic_patches.csv")

TARGET_LANGS = ["Filipino","Swahili","Amharic","Kazakh","Hebrew","Basque","Slovak","Slovenian","Zulu","Kyrgyz","Assamese","Estonian","Odia","Catalan"]
NATIVE_PREFIXES = {
    "Amharic": ["አማርኛ:"],
    "Kazakh": ["Қазақша:"],
    "Hebrew": ["עברית:"],
    "Basque": ["Euskara:"],
    "Slovak": ["Slovenčina:"],
    "Slovenian": ["Slovenščina:"],
    "Zulu": ["isiZulu:"],
    "Filipino": ["Filipino:"],
    "Swahili": ["Kiswahili:"],
    "Kyrgyz": ["Кыргызча:"],
    "Assamese": ["অসমীয়া:"],
    "Estonian": ["Eesti:"],
    "Odia": ["ଓଡ଼ିଆ:"],
    "Catalan": ["Català:"],
}

BAD_TRANSLATION_PHRASES = [
    "the laro",
    "the mchezo",
    "Share your",
    "Applied once on mga setting",
    "Applied once on mipangilio",
    "Replaces the in-laro",
    "Replaces the in-mchezo",
    "Controls how the",
    "Checked: gamitin the",
    "Checked: tumia the",
    "Does not overwrite your",
    "When playing with an Aspect Ratio",
    "Changes the HP halaga",
    "Changes the HP thamani",
    "Generate sharable TOML for the kasalukuyang",
    "Generate sharable TOML for the sasa",
    "Maalum HUD code copied to the clipboard",
    "The pasted Maalum HUD code is not",
]

block_re = re.compile(r'(?P<head>\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{\n)(?P<body>.*?)(?P<tail>\n\s*\}\s*\})', re.S)

def cpp_unescape(lit: str) -> str:
    return ast.literal_eval(lit)

def parse_lang_value(body: str, lang: str) -> str | None:
    mm = re.search(r'\{MenuLangId::' + re.escape(lang) + r',\s*("(?:(?:\\.)|[^"\\])*")\},', body)
    return cpp_unescape(mm.group(1)) if mm else None

def count_lang(text: str, lang: str) -> int:
    return len(re.findall(r'\{MenuLangId::' + re.escape(lang) + r'\s*,', text))

def main() -> int:
    failed = False

    # Coverage
    detected = set()
    for path in FILES.values():
        text = path.read_text(encoding="utf-8")
        detected.update(re.findall(r'MenuLangId::([A-Za-z0-9_]+)', text))

    for name, path in FILES.items():
        text = path.read_text(encoding="utf-8")
        rows = len(list(block_re.finditer(text)))
        print(f"[INFO] {name}: rows={rows}")
        for lang in sorted(detected):
            c = count_lang(text, lang)
            if c != rows:
                print(f"[FAIL] {name}: {lang} coverage {c}/{rows}")
                failed = True

    # Fallback + known bad phrase audit on translation values only
    prefix_counts = Counter()
    fallback_counts = Counter()
    bad_hits = []
    for name, path in FILES.items():
        text = path.read_text(encoding="utf-8")
        for m in block_re.finditer(text):
            key = cpp_unescape('"' + m.group("key") + '"')
            body = m.group("body")
            for lang in TARGET_LANGS:
                val = parse_lang_value(body, lang)
                if not val:
                    continue
                if any(val.startswith(pref + " ") or val == pref for pref in NATIVE_PREFIXES.get(lang, [])):
                    prefix_counts[lang] += 1
                if lang == "Filipino" and val.startswith("Paliwanag — "):
                    fallback_counts[lang] += 1
                if lang == "Swahili" and val.startswith("Maelezo — "):
                    fallback_counts[lang] += 1
                if lang in {"Filipino", "Swahili"}:
                    for phrase in BAD_TRANSLATION_PHRASES:
                        if phrase in val:
                            bad_hits.append((name, key, lang, phrase))

    if prefix_counts:
        print(f"[FAIL] prefix fallback remains: {dict(prefix_counts)}")
        failed = True
    if fallback_counts:
        print(f"[FAIL] fallback sentence remains: {dict(fallback_counts)}")
        failed = True
    if bad_hits:
        print(f"[FAIL] known bad mixed translation phrases remain: {bad_hits[:20]}")
        failed = True

    if not REVIEW_MATRIX.exists():
        print(f"[FAIL] missing review matrix: {REVIEW_MATRIX}")
        failed = True
    if not PATCHES_CSV.exists():
        print(f"[FAIL] missing patches CSV: {PATCHES_CSV}")
        failed = True
    else:
        with PATCHES_CSV.open("r", encoding="utf-8", newline="") as f:
            patches = list(csv.DictReader(f))
        print(f"[INFO] semantic patches listed: {len(patches)}")
        if len(patches) < 40:
            print("[FAIL] expected semantic patches for Filipino/Swahili high-risk rows")
            failed = True

    if not failed:
        print("[PASS] Phase 6 semantic audit passed")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
