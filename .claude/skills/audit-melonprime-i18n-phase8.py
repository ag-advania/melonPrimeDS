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

PHASE8_DIR = Path("i18n_quality_phase8")
REQUIRED = [
    PHASE8_DIR / "font_coverage_report.md",
    PHASE8_DIR / "rtl_layout_report.md",
    PHASE8_DIR / "ui_clipping_report.md",
    PHASE8_DIR / "phase8_font_coverage_audit.csv",
    PHASE8_DIR / "phase8_rtl_layout_audit.csv",
    PHASE8_DIR / "phase8_ui_clipping_audit.csv",
    PHASE8_DIR / "phase8_review_queue_summary.json",
    PHASE8_DIR / "phase8_completion_report.md",
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

def count_status(path: Path):
    counts = Counter()
    with path.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            counts[row.get("status","")] += 1
    return counts

def main() -> int:
    failed = False

    # Required outputs
    for p in REQUIRED:
        if not p.exists():
            print(f"[FAIL] missing Phase 8 output: {p}")
            failed = True

    # Coverage
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

    # Fallback
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

    # Static blocker counts
    audits = {
        "font": PHASE8_DIR / "phase8_font_coverage_audit.csv",
        "rtl": PHASE8_DIR / "phase8_rtl_layout_audit.csv",
        "clipping": PHASE8_DIR / "phase8_ui_clipping_audit.csv",
    }
    for name, path in audits.items():
        if not path.exists():
            continue
        counts = count_status(path)
        print(f"[INFO] {name} status: {dict(counts)}")
        if counts.get("BLOCK", 0):
            print(f"[FAIL] {name} BLOCK rows: {counts.get('BLOCK', 0)}")
            failed = True

    if not failed:
        print("[PASS] Phase 8 static RTL/font/clipping audit passed")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
