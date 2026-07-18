#!/usr/bin/env python3
"""Extend 5-field C++ localization rows to 11 fields using extended JSON chunks."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
QT_SDL = SCRIPTS.parent / "src" / "frontend" / "qt_sdl"

FIVE_FIELD_RE = re.compile(
    r'\{\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}'
)
OBJECT_FIVE_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*\}'
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


def format_entry(
    en: str,
    ja: str,
    de: str,
    es: str,
    fr: str,
    it: str,
    nl: str,
    pt: str,
    ru: str,
    zh: str,
    ko: str,
) -> str:
    return (
        f'{{{escape_cpp(en)}, {escape_cpp(ja)}, {escape_cpp(de)}, {escape_cpp(es)}, '
        f'{escape_cpp(fr)}, {escape_cpp(it)}, {escape_cpp(nl)}, {escape_cpp(pt)}, '
        f'{escape_cpp(ru)}, {escape_cpp(zh)}, {escape_cpp(ko)}}}'
    )


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
        path = SCRIPTS / f"loc_chunk_{i}_extended.json"
        if not path.exists():
            raise FileNotFoundError(path)
        for row in json.loads(path.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)

    manual = SCRIPTS / "loc_manual_fixes_extended.json"
    if manual.exists():
        for row in json.loads(manual.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)
    return lookup


def load_objects() -> dict[str, dict]:
    path = SCRIPTS / "loc_chunk_objects_extended.json"
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
        en, ja, de, es, fr = m.groups()
        row = find_row(lookup, en)
        if not row:
            missing.append(en)
            return m.group(0)
        return format_entry(
            en,
            ja,
            row["de"],
            row["es"],
            row["fr"],
            row["it"],
            row["nl"],
            row["pt"],
            row["ru"],
            row["zh"],
            row["ko"],
        )

    out = FIVE_FIELD_RE.sub(repl, text)
    if missing:
        print("missing keys:", len(missing), file=sys.stderr)
        for key in missing:
            print(f"  {key[:120]!r}", file=sys.stderr)
        raise SystemExit(1)
    return out


def decode_field(raw: str) -> str:
    if raw.startswith('R"('):
        return raw[3:-2]
    return raw.strip('"')


def replace_objects(text: str, objects: dict[str, dict]) -> str:
    missing: list[str] = []

    def field(s: str) -> str:
        if "\n" in s or len(s) > 120:
            return f'R"({s})"'
        return escape_cpp(s)

    def repl(m: re.Match[str]) -> str:
        name = m.group(1)
        if name not in objects:
            missing.append(name)
            return m.group(0)
        row = objects[name]
        ja = decode_field(m.group(2))
        return (
            f'{{\n        "{name}",\n        {field(ja)},\n        {field(row["de"])},\n        '
            f'{field(row["es"])},\n        {field(row["fr"])},\n        {field(row["it"])},\n        '
            f'{field(row["nl"])},\n        {field(row["pt"])},\n        {field(row["ru"])},\n        '
            f'{field(row["zh"])},\n        {field(row["ko"])}\n    }}'
        )

    out = OBJECT_FIVE_RE.sub(repl, text)
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
    print("extended localization tables to 11 fields")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
