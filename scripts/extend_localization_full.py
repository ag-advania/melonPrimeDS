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


def normalize_key(key: str) -> str:
    normalized = re.sub(r"\\+n", "\n", key)
    return normalized


def find_applied_once_row(lookup: dict[str, dict], en: str) -> dict | None:
    flags_match = re.search(r"flags=(0x[0-9a-f]+)", en)
    if not flags_match:
        return None
    flags = flags_match.group(1)
    for row in lookup.values():
        row_en = row.get("en", "")
        if row_en.startswith("Applied once on settings close") and f"flags={flags})." in row_en:
            return row
    return None


def key_variants(key: str) -> list[str]:
    variants = {key, normalize_key(key)}
    for base in list(variants):
        variants.add(base.replace("…", "..."))
        variants.add(base.replace("...", "…"))
        variants.add(base.replace("—", "-"))
        variants.add(base.replace("-", "—"))
        variants.add(base.replace("–", "-"))
        variants.add(base.replace("→", "->"))
        variants.add(base.replace("->", "→"))
        variants.add(base.replace(" → ", "→"))
        variants.add(base.replace("→", " → "))
        variants.add(base.replace("\\n", "\n"))
        variants.add(base.replace("\n", "\\n"))
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
            base = lookup.get(row["en"], {})
            merged = {**base, **row}
            if "ja" not in row and "ja" in base:
                merged["ja"] = base["ja"]
            lookup[row["en"]] = merged
            for variant in key_variants(row["en"]):
                lookup[variant] = merged
    return lookup


def load_objects() -> list[dict]:
    path = SCRIPTS / "loc_chunk_objects_full.json"
    if not path.exists():
        raise FileNotFoundError(path)
    return json.loads(path.read_text(encoding="utf-8"))


def object_field(s: str) -> str:
    if "\n" in s or len(s) > 120:
        return f'R"({s})"'
    return escape_cpp(s)


def format_object_entry(row: dict) -> str:
    name = row["objectName"]
    lines = [f"    {{\n        \"{name}\",\n        {object_field(row['ja'])},"]
    for key in ("de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko",
                "ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"):
        lines.append(f"        {object_field(row[key])},")
    lines[-1] = lines[-1].rstrip(",")
    lines.append("    }")
    return "\n".join(lines)


def generate_objects_block(rows: list[dict]) -> str:
    body = ",\n".join(format_object_entry(row) for row in rows)
    return f"constexpr ObjectTextTranslation kObjectTextTranslations[] = {{\n{body}\n}};\n\n"


def find_row(lookup: dict[str, dict], en: str) -> dict | None:
    for variant in key_variants(en):
        row = lookup.get(variant)
        if row:
            return row
    if en.startswith("Applied once on settings close"):
        return find_applied_once_row(lookup, en)
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


def repair_applied_once_rows(text: str, lookup: dict[str, dict]) -> tuple[str, int]:
    """Rewrite OSD slot description rows whose en keys were over-escaped."""
    repaired = 0

    def repl(m: re.Match[str]) -> str:
        nonlocal repaired
        en = m.group(1)
        if not en.startswith("Applied once on settings close"):
            return m.group(0)
        row = find_applied_once_row(lookup, en)
        if not row:
            return m.group(0)
        repaired += 1
        return format_row(row)

    return TWENTY_FIVE_FIELD_RE.sub(repl, text), repaired


def main() -> int:
    lookup = load_lookup()
    objects = load_objects()

    localization = QT_SDL / "MelonPrimeLocalization" / "inc"
    melonds_updated = 0
    for melonds_path in sorted(localization.glob("MelonPrimeDialogsTranslations*.inc")):
        source = melonds_path.read_text(encoding="utf-8")
        if '#include "' in source:
            continue
        melonds_text, updated = sync_full_pairs(replace_pairs(source, lookup), lookup)
        melonds_path.write_text(melonds_text, encoding="utf-8")
        melonds_updated += updated

    cpp_path = QT_SDL / "MelonPrimeLocalization.cpp"
    cpp = cpp_path.read_text(encoding="utf-8")
    obj_start = cpp.index("constexpr ObjectTextTranslation kObjectTextTranslations[] = {")
    obj_end = cpp.index("QString TranslateExact")

    before_objects, main_updated = sync_full_pairs(
        replace_pairs(cpp[:obj_start], lookup), lookup)
    before_objects, repair_updated = repair_applied_once_rows(before_objects, lookup)
    obj_block = generate_objects_block(objects)
    cpp_path.write_text(before_objects + obj_block + cpp[obj_end:], encoding="utf-8")
    print(
        f"synced localization tables ({main_updated + melonds_updated + repair_updated} rows, "
        f"{len(objects)} object descriptions)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
