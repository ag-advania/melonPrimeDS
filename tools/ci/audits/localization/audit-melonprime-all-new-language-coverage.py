#!/usr/bin/env python3
"""Audit coverage for all newly added MelonPrime languages."""

from __future__ import annotations
import json, re, sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
META = REPO / "tools/melonprime_all_new_language_metadata.json"
FILES = [
    (REPO / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslations.inc", "exact"),
    (REPO / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeObjectTranslations.inc", "object"),
]

def read_with_includes(path: Path, seen: set[Path] | None = None) -> str:
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen:
        return ""
    seen.add(path)
    text = path.read_text(encoding="utf-8")

    def expand(match: re.Match[str]) -> str:
        included = path.parent / match.group("path")
        return read_with_includes(included, seen) if included.exists() else match.group(0)

    return re.sub(r'#include\s+"(?P<path>[^"]+\.inc)"', expand, text)

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
        text = read_with_includes(path)
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
