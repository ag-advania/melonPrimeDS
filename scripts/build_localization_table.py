#!/usr/bin/env python3
"""Merge AI-translated JSON chunks into C++ localization tables (in-place)."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
QT_SDL = SCRIPTS.parent / "src" / "frontend" / "qt_sdl"

# Match legacy two-field rows only (skip already-extended five-field rows).
PAIR_RE = re.compile(
    r'\{\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}(?!\s*,\s*")'
)
OBJECT_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*\}(?!\s*,\s*(?:R"\(|"))'
)


def escape_cpp(s: str) -> str:
    return (
        '"'
        + s.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\n", "\\n")
        .replace("\r", "\\r")
        .replace("\t", "\\t")
        + '"'
    )


def format_entry(en: str, ja: str, de: str, es: str, fr: str) -> str:
    return f'{{{escape_cpp(en)}, {escape_cpp(ja)}, {escape_cpp(de)}, {escape_cpp(es)}, {escape_cpp(fr)}}}'


def key_variants(key: str) -> list[str]:
    variants = {key}
    variants.add(key.replace("…", "..."))
    variants.add(key.replace("...", "…"))
    variants.add(key.replace("—", "-"))
    variants.add(key.replace("-", "—"))
    variants.add(key.replace("–", "-"))
    variants.add(key.replace("→", "->"))
    variants.add(key.replace("->", "→"))
    variants.add(key.replace(" → ", "→"))
    variants.add(key.replace("→", " → "))
    variants.add(key.replace("\\n", "\n"))
    variants.add(key.replace("\n", "\\n"))
    return list(variants)


def load_lookup() -> dict[str, dict]:
    lookup: dict[str, dict] = {}
    for i in range(5):
        path = SCRIPTS / f"loc_chunk_{i}_translated.json"
        if not path.exists():
            raise FileNotFoundError(path)
        for row in json.loads(path.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)

    manual = SCRIPTS / "loc_manual_fixes.json"
    if manual.exists():
        for row in json.loads(manual.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)
    return lookup


def load_objects() -> dict[str, dict]:
    path = SCRIPTS / "loc_chunk_objects_translated.json"
    if not path.exists():
        raise FileNotFoundError(path)
    return {row["objectName"]: row for row in json.loads(path.read_text(encoding="utf-8"))}


def find_row(lookup: dict[str, dict], en: str) -> dict | None:
    for variant in key_variants(en):
        row = lookup.get(variant)
        if row:
            return row
    return None


def replace_pairs(text: str, lookup: dict[str, dict]) -> str:
    missing: list[str] = []

    def repl(m: re.Match[str]) -> str:
        en = m.group(1)
        ja = m.group(2)
        row = find_row(lookup, en)
        if not row:
            missing.append(en)
            return m.group(0)
        return format_entry(en, ja, row["de"], row["es"], row["fr"])

    out = PAIR_RE.sub(repl, text)
    if missing:
        print("missing keys:", len(missing), file=sys.stderr)
        for key in missing:
            print(f"  {key[:120]!r}", file=sys.stderr)
        raise SystemExit(1)
    return out


def replace_objects(text: str, objects: dict[str, dict]) -> str:
    missing: list[str] = []

    def repl(m: re.Match[str]) -> str:
        name = m.group(1)
        raw = m.group(2)
        if name not in objects:
            missing.append(name)
            return m.group(0)
        row = objects[name]
        if raw.startswith('R"('):
            ja = raw[3:-2]
        else:
            ja = raw.strip('"')

        def field(s: str) -> str:
            if "\n" in s or len(s) > 120:
                return f'R"({s})"'
            return escape_cpp(s)

        return (
            f'{{\n        "{name}",\n        {field(ja)},\n        {field(row["de"])},\n        '
            f'{field(row["es"])},\n        {field(row["fr"])}\n    }}'
        )

    out = OBJECT_RE.sub(repl, text)
    if missing:
        print("missing objects:", missing, file=sys.stderr)
        raise SystemExit(1)
    return out


def main() -> int:
    lookup = load_lookup()
    objects = load_objects()

    localization = QT_SDL / "MelonPrimeLocalization" / "inc"
    for melonds in sorted(localization.glob("MelonPrimeDialogsTranslations*.inc")):
        text = melonds.read_text(encoding="utf-8")
        if '#include "' in text:
            continue
        melonds.write_text(replace_pairs(text, lookup), encoding="utf-8")

    cpp_path = QT_SDL / "MelonPrimeLocalization.cpp"
    cpp = cpp_path.read_text(encoding="utf-8")
    obj_start = cpp.index("constexpr ObjectTextTranslation kObjectTextTranslations[] = {")
    obj_end = cpp.index("QString TranslateExact")

    before_objects = replace_pairs(cpp[:obj_start], lookup)
    obj_block = replace_objects(cpp[obj_start:obj_end], objects)
    cpp_path.write_text(before_objects + obj_block + cpp[obj_end:], encoding="utf-8")
    print("patched localization tables")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
