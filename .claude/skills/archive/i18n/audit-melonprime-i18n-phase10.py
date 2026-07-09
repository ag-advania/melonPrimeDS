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

PHASE10_DIR = Path("i18n_quality_phase10")
REQUIRED = [
    PHASE10_DIR / "ui_review_report.md",
    PHASE10_DIR / "ui_verification_checklist.csv",
    PHASE10_DIR / "language_release_status.csv",
    PHASE10_DIR / "release_gate_summary.json",
    PHASE10_DIR / "ui_sample_plan.json",
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
            print(f"[FAIL] missing Phase 10 output: {p}")
            failed = True

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

    gate_path = PHASE10_DIR / "release_gate_summary.json"
    if gate_path.exists():
        gate = json.loads(gate_path.read_text(encoding="utf-8"))
        print(f"[INFO] release decision: {gate.get('release_decision')}")
        if not gate.get("coverage_ok"):
            print("[FAIL] release gate coverage is not OK")
            failed = True
        block_rows = gate.get("phase8_static_block_rows", {})
        if any(int(v or 0) for v in block_rows.values()):
            print(f"[FAIL] static block rows remain: {block_rows}")
            failed = True
        if gate.get("manual_ui_verification_completed") is True:
            print("[INFO] manual UI verification marked complete")
        else:
            print("[INFO] manual UI verification remains pending by design")

    checklist = PHASE10_DIR / "ui_verification_checklist.csv"
    if checklist.exists():
        with checklist.open("r", encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        if not rows:
            print("[FAIL] empty UI verification checklist")
            failed = True
        else:
            print(f"[INFO] UI checklist rows: {len(rows)}")

    if not failed:
        print("[PASS] Phase 10 release candidate audit passed")
    return 1 if failed else 0

if __name__ == "__main__":
    raise SystemExit(main())
