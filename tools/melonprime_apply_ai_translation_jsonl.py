#!/usr/bin/env python3
"""Apply AI-translated JSONL rows into MelonPrime translation .inc files.

Input row format:
  {"surface":"exact|object","key":"Save","language_id":"Hindi","text":"सहेजें"}

Usage:
  python3 tools/melonprime_apply_ai_translation_jsonl.py --repo . --input translated.jsonl

The script inserts a `{MenuLangId::Language, "..."},` value into the matching
translation row when that language is missing. Existing values are preserved by
default; pass --replace-existing to replace them.

"exact" rows are tried against MelonPrimeTranslations.inc first, then against
MelonPrimeLocalizationMelondsDialogs.inc (a nested #include pulled into the
same kTranslations array, holding melonDS settings-dialog strings) for any key
not found in the first file.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

def cpp_escape(s: str) -> str:
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'

def load_translations(path: Path):
    rows = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        row = json.loads(line)
        if not row.get("text"):
            continue
        rows.append(row)
    return rows

def find_matching_row(text: str, key: str, start_pos: int = 0) -> tuple[int, int] | None:
    key_lit = cpp_escape(key)
    marker = re.search(r'\{\s*' + re.escape(key_lit) + r'\s*,\s*\{', text[start_pos:])
    if not marker:
        return None
    start = start_pos + marker.start()
    i = start
    depth = 0
    in_string = False
    esc = False
    while i < len(text):
        ch = text[i]
        if in_string:
            if esc: esc = False
            elif ch == "\\": esc = True
            elif ch == '"': in_string = False
        else:
            if ch == '"': in_string = True
            elif ch == "{": depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return start, i + 1
        i += 1
    return None

def upsert_value_in_row(row: str, lang: str, value: str, replace_existing: bool) -> tuple[str, bool]:
    lang_re = re.compile(r'(?P<indent>\s*)\{MenuLangId::' + re.escape(lang) + r',\s*"(?P<old>(?:\\.|[^"\\])*)"\},')
    m = lang_re.search(row)
    new_value = cpp_escape(value)
    if m:
        if not replace_existing:
            return row, False
        return row[:m.start()] + f'{m.group("indent")}{{MenuLangId::{lang}, {new_value}}},' + row[m.end():], True

    # Insert before closing of inner values block. In current style this is the
    # line before the row's final "}".
    insert_at = row.rfind("\n        }")
    if insert_at < 0:
        insert_at = row.rfind("}")
    insertion = f'            {{MenuLangId::{lang}, {new_value}}},'
    if insert_at >= 0:
        return row[:insert_at] + "\n" + insertion + row[insert_at:], True
    return row, False

def apply_to_file(path: Path, rows, replace_existing: bool):
    """Returns (changed_count, not_found_rows) where not_found_rows is the
    subset of `rows` whose key does not exist in this file (so callers can try
    them against another file)."""
    text = path.read_text(encoding="utf-8")
    changed = 0
    not_found = []
    for item in rows:
        match = find_matching_row(text, item["key"])
        if not match:
            not_found.append(item)
            continue
        a, b = match
        old = text[a:b]
        new, did = upsert_value_in_row(old, item["language_id"], item["text"], replace_existing)
        if did:
            text = text[:a] + new + text[b:]
            changed += 1
    if changed:
        path.write_text(text, encoding="utf-8")
    return changed, not_found

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path("."))
    ap.add_argument("--input", type=Path, required=True)
    ap.add_argument("--replace-existing", action="store_true")
    args = ap.parse_args()

    repo = args.repo.resolve()
    rows = load_translations(args.input)

    exact_rows = [r for r in rows if r.get("surface") == "exact"]
    obj_rows = [r for r in rows if r.get("surface") == "object"]

    exact_changed_1, exact_not_found_1 = apply_to_file(
        repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc",
        exact_rows,
        args.replace_existing,
    )
    # Nested #include pulled in by MelonPrimeTranslations.inc (melonDS settings
    # dialog strings); lives in qt_sdl/ root, not the MelonPrimeLocalization/
    # subdir. Only try keys the first file did not have.
    exact_changed_2, exact_not_found_2 = apply_to_file(
        repo / "src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc",
        exact_not_found_1,
        args.replace_existing,
    )
    exact_changed = exact_changed_1 + exact_changed_2
    exact_skipped = len(exact_not_found_2)

    obj_changed, obj_not_found = apply_to_file(
        repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc",
        obj_rows,
        args.replace_existing,
    )
    obj_skipped = len(obj_not_found)

    print(f"[PASS] applied exact={exact_changed}, object={obj_changed}")
    print(f"[INFO] skipped exact={exact_skipped}, object={obj_skipped}")
    if exact_not_found_2:
        sample = ", ".join(sorted({r["key"] for r in exact_not_found_2})[:10])
        print(f"[WARN] exact keys not found in either file (sample): {sample}")
    if obj_not_found:
        sample = ", ".join(sorted({r["key"] for r in obj_not_found})[:10])
        print(f"[WARN] object keys not found: {sample}")
    print("[NEXT] Run: python3 .claude/skills/audit-melonprime-all-new-language-coverage.py")
    print("[NEXT] Run: python3 .claude/skills/audit-melonprime-localization.py")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
