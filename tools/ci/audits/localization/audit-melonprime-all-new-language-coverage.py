#!/usr/bin/env python3
"""Audit coverage for all newly added MelonPrime languages."""

from __future__ import annotations
import json, re, sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
META = REPO / "tools/melonprime_all_new_language_metadata.json"
FILES = [
    (REPO / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc", "exact"),
    # Nested #include pulled in by MelonPrimeTranslations.inc (melonDS settings
    # dialog strings); lives in qt_sdl/ root, not the MelonPrimeLocalization/
    # subdir, and is not its own top-level array (spliced into kTranslations).
    (REPO / "src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc", "exact"),
    (REPO / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc", "object"),
]

def count_rows(text: str) -> int:
    return len(re.findall(r'\{\s*"(?:\\.|[^"\\])*"\s*,\s*\{', text))

def count_lang(text: str, lang: str) -> int:
    return len(re.findall(r'\{MenuLangId::' + re.escape(lang) + r'\s*,', text))

def main() -> int:
    langs = json.loads(META.read_text(encoding="utf-8"))
    failed = False
    totals = {"exact": 0, "object": 0}
    lang_counts = {"exact": {lang["id"]: 0 for lang in langs}, "object": {lang["id"]: 0 for lang in langs}}
    for path, surface in FILES:
        text = path.read_text(encoding="utf-8")
        total = count_rows(text)
        totals[surface] += total
        print(f"[INFO] {path.name} ({surface}): rows={total}")
        for lang in langs:
            lang_counts[surface][lang["id"]] += count_lang(text, lang["id"])

    for surface in ("exact", "object"):
        total = totals[surface]
        print(f"[INFO] {surface} total rows: {total}")
        for lang in langs:
            c = lang_counts[surface][lang["id"]]
            if c < total:
                print(f"[WARN] {surface} {lang['id']}: {c}/{total}")
                failed = True
            else:
                print(f"[PASS] {surface} {lang['id']}: {c}/{total}")

    if failed:
        print("[WARN] Some languages are not fully covered yet")
        return 0
    print("[PASS] All new languages appear on every audited row")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
