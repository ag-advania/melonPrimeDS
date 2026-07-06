#!/usr/bin/env python3
from __future__ import annotations
import ast
import csv
import re
from pathlib import Path
from collections import Counter

FILES = {
    "main": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "object": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "dialogs": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}
AUDIT_CSV = Path("i18n_quality_phase7/phase7_object_translation_audit.csv")
PATCH_CSV = Path("i18n_quality_phase7/phase7_applied_object_patches.csv")
NATIVE_QUEUE_CSV = Path("i18n_quality_phase7/phase7_object_native_review_queue.csv")
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

    detected = set()
    for p in FILES.values():
        text = p.read_text(encoding="utf-8")
        detected.update(re.findall(r'MenuLangId::([A-Za-z0-9_]+)', text))

    for name, p in FILES.items():
        text = p.read_text(encoding="utf-8")
        rows = len(list(block_re.finditer(text)))
        print(f"[INFO] {name}: rows={rows}")
        for lang in sorted(detected):
            c = count_lang(text, lang)
            if c != rows:
                print(f"[FAIL] {name}: {lang} coverage {c}/{rows}")
                failed = True

    prefix_counts = Counter()
    fallback_counts = Counter()
    for name, p in FILES.items():
        text = p.read_text(encoding="utf-8")
        for m in block_re.finditer(text):
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

    if prefix_counts:
        print(f"[FAIL] prefix fallback remains: {dict(prefix_counts)}")
        failed = True
    if fallback_counts:
        print(f"[FAIL] fallback sentence remains: {dict(fallback_counts)}")
        failed = True

    for path in [AUDIT_CSV, PATCH_CSV, NATIVE_QUEUE_CSV]:
        if not path.exists():
            print(f"[FAIL] missing Phase 7 output: {path}")
            failed = True

    if AUDIT_CSV.exists():
        blocking = []
        with AUDIT_CSV.open("r", encoding="utf-8", newline="") as f:
            for row in csv.DictReader(f):
                if row.get("status") == "BLOCKING_REVIEW" or row.get("blocking_issues"):
                    blocking.append(row)
        if blocking:
            print(f"[FAIL] blocking ObjectTranslations review rows remain: {len(blocking)}")
            failed = True
        else:
            print("[PASS] ObjectTranslations blocking review rows: 0")

    if PATCH_CSV.exists():
        with PATCH_CSV.open("r", encoding="utf-8", newline="") as f:
            patches = list(csv.DictReader(f))
        print(f"[INFO] Phase 7 applied object patches: {len(patches)}")
        if len(patches) < 20:
            print("[FAIL] expected Filipino/Swahili object patches")
            failed = True

    if not failed:
        print("[PASS] Phase 7 object review audit passed")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
