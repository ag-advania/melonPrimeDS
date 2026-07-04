#!/usr/bin/env python3
"""Extend 11-field C++ localization rows to 25 fields using full JSON chunks."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
QT_SDL = SCRIPTS.parent / "src" / "frontend" / "qt_sdl"

LANG_FIELDS = (
    "ja", "de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
    "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro",
)

ELEVEN_FIELD_RE = re.compile(
    r'\{\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*,\s*"((?:\\.|[^"\\])*)"\s*\}'
)
_STRING = r'"((?:\\.|[^"\\])*)"'
TWENTY_FIVE_FIELD_RE = re.compile(
    r"\{\s*" + _STRING + r"(?:\s*,\s*" + _STRING + r"){25}\s*\}"
)
OBJECT_ELEVEN_RE = re.compile(
    r'\{\s*"([^"]+)"\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*,\s*(R"\((?:.|\n)*?\)"|"[^"]*")\s*\}'
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


def format_row(row: dict) -> str:
    parts = [escape_cpp(row["en"]), escape_cpp(row["ja"])]
    for field in ("de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
                  "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"):
        parts.append(escape_cpp(row[field]))
    return "{" + ", ".join(parts) + "}"


def format_entry(en: str, ja: str, row: dict) -> str:
    parts = [escape_cpp(en), escape_cpp(ja)]
    for field in ("de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
                  "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"):
        parts.append(escape_cpp(row[field]))
    return "{" + ", ".join(parts) + "}"


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
        path = SCRIPTS / f"loc_chunk_{i}_full.json"
        if not path.exists():
            raise FileNotFoundError(path)
        for row in json.loads(path.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)

    manual = SCRIPTS / "loc_manual_fixes_full.json"
    if manual.exists():
        for row in json.loads(manual.read_text(encoding="utf-8")):
            lookup[row["en"]] = row
            for variant in key_variants(row["en"]):
                lookup.setdefault(variant, row)
    return lookup


def load_objects() -> dict[str, dict]:
    path = SCRIPTS / "loc_chunk_objects_full.json"
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
        return format_entry(en, ja, row)

    out = ELEVEN_FIELD_RE.sub(repl, text)
    if missing:
        print("missing keys:", len(missing), file=sys.stderr)
        for key in missing:
            print(f"  {key[:120]!r}", file=sys.stderr)
        raise SystemExit(1)
    return out


def sync_full_pairs(text: str, lookup: dict[str, dict]) -> tuple[str, int]:
    updated = 0

    def repl(m: re.Match[str]) -> str:
        nonlocal updated
        en = m.group(1)
        ja = m.group(2)
        row = find_row(lookup, en)
        if not row:
            return m.group(0)
        updated += 1
        if "ja" in row:
            return format_row(row)
        return format_entry(en, ja, row)

    return TWENTY_FIVE_FIELD_RE.sub(repl, text), updated


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
        lines = [f'{{\n        "{name}",\n        {field(ja)},']
        for key in ("de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
                    "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"):
            lines.append(f"        {field(row[key])},")
        lines[-1] = lines[-1].rstrip(",")
        lines.append("    }")
        return "\n".join(lines)

    out = OBJECT_ELEVEN_RE.sub(repl, text)
    if missing:
        print("missing objects:", missing, file=sys.stderr)
        raise SystemExit(1)
    return out


def main() -> int:
    lookup = load_lookup()
    objects = load_objects()

    melonds_path = QT_SDL / "MelonPrimeLocalizationMelondsDialogs.inc"
    melonds_text, melonds_updated = sync_full_pairs(
        replace_pairs(melonds_path.read_text(encoding="utf-8"), lookup), lookup)
    melonds_path.write_text(melonds_text, encoding="utf-8")

    cpp_path = QT_SDL / "MelonPrimeLocalization.cpp"
    cpp = cpp_path.read_text(encoding="utf-8")
    obj_start = cpp.index("constexpr ObjectTextTranslation kObjectTextTranslations[] = {")
    obj_end = cpp.index("QString TranslateExact")

    before_objects, main_updated = sync_full_pairs(
        replace_pairs(cpp[:obj_start], lookup), lookup)
    obj_block = replace_objects(cpp[obj_start:obj_end], objects)
    cpp_path.write_text(before_objects + obj_block + cpp[obj_end:], encoding="utf-8")
    print(f"synced localization tables ({main_updated + melonds_updated} rows updated from JSON)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
