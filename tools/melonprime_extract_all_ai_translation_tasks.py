#!/usr/bin/env python3
"""Extract all missing translation tasks for all newly added MelonPrime languages.

Usage:
  python3 tools/melonprime_extract_all_ai_translation_tasks.py --repo .

Outputs:
  artifacts/localization-ai/all_new_languages.tasks.jsonl
  artifacts/localization-ai/by-language/<Language>.tasks.jsonl

This only extracts tasks. It does not call any MT API.
"""

from __future__ import annotations

import argparse
import json
import re
from pathlib import Path

RAW_STRING_RE = re.compile(r'R"(?P<delim>[A-Za-z0-9_]*)\(')
ROW_START_RE = re.compile(r'\{\s*"(?P<key>(?:\\.|[^"\\])*)"\s*,\s*\{', re.S)
LANG_VALUE_RE = re.compile(r'\{MenuLangId::(?P<lang>[A-Za-z0-9_]+),\s*"(?P<text>(?:\\.|[^"\\])*)"\},')

def cpp_unescape(s: str) -> str:
    # good enough for the generated normal string literals in these tables
    return bytes(s, "utf-8").decode("unicode_escape")

def find_array_body(text: str, name: str) -> str:
    marker = re.search(rf"\b{name}\s*\[\]\s*=\s*\{{", text)
    if not marker:
        raise RuntimeError(f"{name} array not found")
    start = marker.end() - 1
    depth = 0
    in_string = False
    esc = False
    i = start
    while i < len(text):
        ch = text[i]
        if in_string:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                in_string = False
        else:
            if ch == '"':
                in_string = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    return text[start + 1:i]
        i += 1
    raise RuntimeError(f"{name} did not terminate")

def split_rows(body: str):
    rows = []
    depth = 0
    start = None
    in_string = False
    esc = False
    for i,ch in enumerate(body):
        if in_string:
            if esc: esc = False
            elif ch == "\\": esc = True
            elif ch == '"': in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                rows.append(body[start:i+1])
                start = None
    return rows

def parse_rows_in_array(path: Path, array_name: str, surface: str):
    text = path.read_text(encoding="utf-8")
    body = find_array_body(text, array_name)
    return parse_rows_in_text(body, surface)

def parse_rows_in_text(body: str, surface: str):
    parsed = []
    for row in split_rows(body):
        m = ROW_START_RE.search(row)
        if not m:
            continue
        key = cpp_unescape(m.group("key"))
        values = {mm.group("lang"): cpp_unescape(mm.group("text")) for mm in LANG_VALUE_RE.finditer(row)}
        parsed.append({"surface": surface, "key": key, "values": values})
    return parsed

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", type=Path, default=Path("."))
    args = ap.parse_args()
    repo = args.repo.resolve()

    metadata = json.loads((repo / "tools/melonprime_all_new_language_metadata.json").read_text(encoding="utf-8"))
    languages = [x["id"] for x in metadata]
    meta_by_id = {x["id"]: x for x in metadata}

    exact_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc"
    object_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc"
    # Nested #include pulled in by MelonPrimeTranslations.inc (melonDS settings
    # dialog strings). It lives in qt_sdl/ root, not the MelonPrimeLocalization/
    # subdir, and is a flat sequence of row entries with no "kTranslations[] = {"
    # wrapper of its own (it is spliced into the parent array via #include), so
    # it is parsed directly rather than through find_array_body().
    melonds_dialogs_path = repo / "src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc"

    exact = parse_rows_in_array(exact_path, "kTranslations", "exact")
    exact += parse_rows_in_text(melonds_dialogs_path.read_text(encoding="utf-8"), "exact")
    obj = parse_rows_in_array(object_path, "kObjectTextTranslations", "object")

    outdir = repo / "artifacts/localization-ai"
    by_lang = outdir / "by-language"
    by_lang.mkdir(parents=True, exist_ok=True)

    all_out = outdir / "all_new_languages.tasks.jsonl"
    per_lang_files = {lang: (by_lang / f"{lang}.tasks.jsonl").open("w", encoding="utf-8") for lang in languages}
    total = 0
    try:
        with all_out.open("w", encoding="utf-8") as all_f:
            for row in exact + obj:
                source_text = row["key"]
                # For object rows, if English is ever added later, prefer it.
                if row["surface"] == "object":
                    source_text = row["values"].get("English", row["key"])

                for lang in languages:
                    if row["values"].get(lang):
                        continue
                    m = meta_by_id[lang]
                    task = {
                        "surface": row["surface"],
                        "key": row["key"],
                        "english": source_text,
                        "language_id": lang,
                        "language_code": m["code"],
                        "display": m["display"],
                        "text": "",
                        "notes": "Translate the English field into the target language. Keep placeholders (%1, %2), code-like tokens, ROM/HUD/DS/GBA/MPH names, arrows, punctuation semantics, and accelerator symbols intact."
                    }
                    line = json.dumps(task, ensure_ascii=False)
                    all_f.write(line + "\n")
                    per_lang_files[lang].write(line + "\n")
                    total += 1
    finally:
        for f in per_lang_files.values():
            f.close()

    print(f"[PASS] wrote {total} tasks")
    print(f"[INFO] all: {all_out}")
    print(f"[INFO] by-language: {by_lang}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
