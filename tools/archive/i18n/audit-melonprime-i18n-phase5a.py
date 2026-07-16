#!/usr/bin/env python3
from __future__ import annotations
import ast
import csv
import json
import re
from pathlib import Path
from collections import Counter

FILES = {
    "MelonPrimeTranslations.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "MelonPrimeObjectTranslations.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "MelonPrimeLocalizationMelondsDialogs.inc": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}

DECISIONS_CSV = Path("i18n_quality_phase5a/phase5a_final_decisions.csv")
block_re = re.compile(r'(?P<head>\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{\n)(?P<body>.*?)(?P<tail>\n\s*\}\s*\})', re.S)

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

    # Fallback checks
    prefix_counts = Counter()
    fallback_sentence_counts = Counter()
    for name, path in FILES.items():
        text = path.read_text(encoding="utf-8")
        for m in block_re.finditer(text):
            body = m.group("body")
            for lang in TARGET_LANGS:
                val = parse_lang_value(body, lang)
                if not val:
                    continue
                if any(val.startswith(pref + " ") or val == pref for pref in NATIVE_PREFIXES.get(lang, [])):
                    prefix_counts[lang] += 1
                if lang == "Filipino" and val.startswith("Paliwanag — "):
                    fallback_sentence_counts[lang] += 1
                if lang == "Swahili" and val.startswith("Maelezo — "):
                    fallback_sentence_counts[lang] += 1
    if prefix_counts:
        print(f"[FAIL] prefix fallback remains: {dict(prefix_counts)}")
        failed = True
    if fallback_sentence_counts:
        print(f"[FAIL] fallback sentence remains: {dict(fallback_sentence_counts)}")
        failed = True

    # Phase 5A decision file check
    if not DECISIONS_CSV.exists():
        print(f"[FAIL] missing decision CSV: {DECISIONS_CSV}")
        return 1
    unresolved = []
    with DECISIONS_CSV.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("final_decision") in {"TRANSLATE_UI", "NEEDS_CONTEXT"}:
                unresolved.append(row)
    if unresolved:
        print(f"[FAIL] unresolved Phase 5A rows: {len(unresolved)}")
        failed = True
    else:
        print("[PASS] Phase 5A final decisions have no TRANSLATE_UI/NEEDS_CONTEXT rows")

    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
