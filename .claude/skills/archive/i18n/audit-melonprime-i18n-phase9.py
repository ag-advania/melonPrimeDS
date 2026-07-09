#!/usr/bin/env python3
from __future__ import annotations
import ast
import csv
import json
import re
from pathlib import Path
from collections import Counter

FILES = {
    "main": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"),
    "object": Path("src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"),
    "dialogs": Path("src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"),
}

PHASE9_DIR = Path("i18n_quality_phase9")
REQUIRED = [
    PHASE9_DIR / "consolidated_native_review_queue.csv",
    PHASE9_DIR / "consolidated_native_review_queue.json",
    PHASE9_DIR / "native_review_pack_summary.json",
    PHASE9_DIR / "native_review_glossary.md",
    PHASE9_DIR / "native_review_context.md",
    PHASE9_DIR / "phase9_summary.json",
    PHASE9_DIR / "phase9_completion_report.md",
]

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

    for p in REQUIRED:
        if not p.exists():
            print(f"[FAIL] missing Phase 9 output: {p}")
            failed = True

    # Coverage and fallback
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

    # Review packs
    queue_csv = PHASE9_DIR / "consolidated_native_review_queue.csv"
    if queue_csv.exists():
        with queue_csv.open("r", encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        print(f"[INFO] consolidated review records: {len(rows)}")
        if not rows:
            print("[FAIL] consolidated review queue is empty")
            failed = True
        langs = sorted(set(r["language"] for r in rows if r.get("language")))
        pack_dir = PHASE9_DIR / "native_review_packs"
        missing_packs = []
        for lang in langs:
            lang_dir = pack_dir / lang
            for req_name in ["README.md", "glossary.md", "context.md", "strings_to_review.csv"]:
                if not (lang_dir / req_name).exists():
                    missing_packs.append(str(lang_dir / req_name))
        if missing_packs:
            print(f"[FAIL] missing language pack files: {missing_packs[:20]}")
            failed = True
        else:
            print(f"[PASS] language review packs present: {len(langs)}")

        priorities = Counter(r.get("priority","") for r in rows)
        print(f"[INFO] priority counts: {dict(priorities)}")
        if priorities.get("P1", 0) == 0 and priorities.get("P2", 0) == 0:
            print("[FAIL] expected P1/P2 rows in review queue")
            failed = True

    if not failed:
        print("[PASS] Phase 9 native review pack audit passed")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
