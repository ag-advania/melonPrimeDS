#!/usr/bin/env python3
"""Audit MelonPrime localization invariants.

This intentionally uses a small source parser instead of importing C++ data so
it can run before and after the localization files are physically split.
"""

from __future__ import annotations

import re
import sys
from collections import Counter
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
QT_SDL = REPO_ROOT / "src" / "frontend" / "qt_sdl"
HEADER = QT_SDL / "MelonPrimeLocalization.h"

REPRESENTATIVE_KEYS = [
    "Save",
    "Cancel",
    "OK",
    "ON",
    "OFF",
    "File",
    "Config",
    "Input Config",
    "File->Open ROM...",
    "to get started",
]

EXPECTED_TRANSLATION_FIELDS = 26
EXPECTED_OBJECT_FIELDS = 26


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def source_files() -> list[Path]:
    files = [QT_SDL / "MelonPrimeLocalization.cpp"]
    split_dir = QT_SDL / "MelonPrimeLocalization"
    if split_dir.exists():
        files.extend(sorted(split_dir.glob("*.cpp")))
        files.extend(sorted(split_dir.glob("*.inc")))
    return [path for path in files if path.exists()]


def all_source_text() -> str:
    parts = [read_text(HEADER)]
    parts.extend(read_text(path) for path in source_files())
    return "\n".join(parts)


def parse_menu_lang_values() -> dict[str, int]:
    text = read_text(HEADER)
    match = re.search(r"enum\s+class\s+MenuLangId\s*:\s*int\s*\{(?P<body>.*?)\n\};", text, re.S)
    if not match:
        raise RuntimeError("MenuLangId enum not found")

    values: dict[str, int] = {}
    current: int | None = None
    body = re.sub(r"//.*", "", match.group("body"))
    for raw_item in body.split(","):
        item = raw_item.strip()
        if not item:
            continue
        if "=" in item:
            name, expr = [part.strip() for part in item.split("=", 1)]
            if expr.isdigit() or (expr.startswith("-") and expr[1:].isdigit()):
                current = int(expr)
            elif expr in values:
                current = values[expr]
            else:
                raise RuntimeError(f"Unsupported MenuLangId initializer: {item}")
        else:
            name = item
            if current is None:
                current = 0
            else:
                current += 1
        values[name] = current
    return values


def find_array_body(text: str, name: str) -> str:
    marker = re.search(rf"\b{name}\s*\[\]\s*=\s*\{{", text)
    if not marker:
        raise RuntimeError(f"{name} array not found")

    start = marker.end() - 1
    depth = 0
    i = start
    in_string = False
    in_raw = False
    raw_end = ""
    escape = False
    while i < len(text):
        if in_raw:
            if text.startswith(raw_end, i):
                i += len(raw_end)
                in_raw = False
                continue
            i += 1
            continue
        if in_string:
            if escape:
                escape = False
            elif text[i] == "\\":
                escape = True
            elif text[i] == '"':
                in_string = False
            i += 1
            continue

        raw = re.match(r'R"(?P<delim>[A-Za-z0-9_]*)\(', text[i:])
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            i += raw.end()
            in_raw = True
            continue
        if text[i] == '"':
            in_string = True
        elif text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : i]
        i += 1
    raise RuntimeError(f"{name} array did not terminate")


def split_top_level_entries(body: str) -> list[str]:
    entries: list[str] = []
    depth = 0
    start: int | None = None
    in_string = False
    in_raw = False
    raw_end = ""
    escape = False
    i = 0
    while i < len(body):
        if in_raw:
            if body.startswith(raw_end, i):
                i += len(raw_end)
                in_raw = False
                continue
            i += 1
            continue
        if in_string:
            if escape:
                escape = False
            elif body[i] == "\\":
                escape = True
            elif body[i] == '"':
                in_string = False
            i += 1
            continue

        raw = re.match(r'R"(?P<delim>[A-Za-z0-9_]*)\(', body[i:])
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            i += raw.end()
            in_raw = True
            continue
        ch = body[i]
        if ch == '"':
            in_string = True
        elif ch == "{":
            if depth == 0:
                start = i
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0 and start is not None:
                entries.append(body[start : i + 1])
                start = None
        i += 1
    return entries


def cpp_unescape_normal(s: str) -> str:
    return bytes(s, "utf-8").decode("unicode_escape")


def parse_cpp_strings(entry: str) -> list[str]:
    values: list[str] = []
    i = 0
    while i < len(entry):
        raw = re.match(r'R"(?P<delim>[A-Za-z0-9_]*)\(', entry[i:])
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            content_start = i + raw.end()
            content_end = entry.find(raw_end, content_start)
            if content_end < 0:
                raise RuntimeError("Unterminated raw string literal")
            values.append(entry[content_start:content_end])
            i = content_end + len(raw_end)
            continue
        if entry[i] != '"':
            i += 1
            continue

        i += 1
        chars: list[str] = []
        escape = False
        while i < len(entry):
            ch = entry[i]
            if escape:
                chars.append("\\" + ch)
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                break
            else:
                chars.append(ch)
            i += 1
        values.append(cpp_unescape_normal("".join(chars)))
        i += 1
    return values


def parse_rows(array_name: str, expected_fields: int) -> list[list[str]]:
    text = all_source_text()
    body = find_array_body(text, array_name)
    rows = [parse_cpp_strings(entry) for entry in split_top_level_entries(body)]
    bad = [(idx + 1, len(row)) for idx, row in enumerate(rows) if len(row) != expected_fields]
    if bad:
        details = ", ".join(f"row {idx}: {count}" for idx, count in bad[:10])
        raise RuntimeError(f"{array_name} field count mismatch: {details}")
    return rows


def pass_line(message: str) -> None:
    print(f"[PASS] {message}")


def fail(message: str) -> None:
    print(f"[FAIL] {message}")
    raise SystemExit(1)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)
    pass_line(message)


def main() -> int:
    enum_values = parse_menu_lang_values()
    source = all_source_text()

    require(enum_values.get("First", -1) >= 100, "MenuLangId::First >= 100")
    require(enum_values["Arabic"] >= 100, "Arabic persisted value >= 100")
    require(enum_values["Italian"] >= 100, "Italian persisted value >= 100")
    require(
        re.search(r"kMenuLanguageSystemDefault\s*=\s*-1", read_text(HEADER)) is not None,
        "System default config value is -1",
    )
    require(
        "kMenuLanguageLegacyNative = 0" in read_text(HEADER)
        and "kMenuLanguageLegacyEnglish = 1" in read_text(HEADER)
        and "kMenuLanguageLegacyNative" in source
        and "kMenuLanguageLegacyEnglish" in source,
        "Legacy 0/1 migration constants are present",
    )
    require(
        "lang == MenuLangId::ChineseTraditional" in source
        and re.search(r"if\s*\(\s*lang\s*==\s*MenuLangId::ChineseTraditional\s*\)\s*continue\s*;", source),
        "ChineseTraditional is not selectable",
    )

    exact_rows = parse_rows("kTranslations", EXPECTED_TRANSLATION_FIELDS)
    object_rows = parse_rows("kObjectTextTranslations", EXPECTED_OBJECT_FIELDS)

    exact_keys = [row[0] for row in exact_rows]
    object_keys = [row[0] for row in object_rows]
    duplicate_exact = sorted(key for key, count in Counter(exact_keys).items() if count > 1)
    duplicate_object = sorted(key for key, count in Counter(object_keys).items() if count > 1)
    require(not duplicate_exact, f"Exact translation keys are unique ({len(exact_keys)} keys)")
    require(not duplicate_object, f"Object translation keys are unique ({len(object_keys)} keys)")

    empty_exact = [
        (row[0], idx)
        for row in exact_rows
        for idx, value in enumerate(row)
        if value == ""
    ]
    empty_object = [
        (row[0], idx)
        for row in object_rows
        for idx, value in enumerate(row)
        if value == ""
    ]
    require(not empty_exact, "Exact translations have no empty string fields")
    require(not empty_object, "Object translations have no empty string fields")

    exact_by_key = {row[0]: row for row in exact_rows}
    missing_representatives = [key for key in REPRESENTATIVE_KEYS if key not in exact_by_key]
    require(not missing_representatives, "Representative exact keys are present")

    selectable = [
        name
        for name, value in enum_values.items()
        if value >= enum_values["First"] and name not in {"Count", "First", "ChineseTraditional"}
    ]
    for key in REPRESENTATIVE_KEYS:
        row = exact_by_key[key]
        require(all(value for value in row), f"Representative key '{key}' is non-empty for all translation columns")
    require("Arabic" in selectable and "Italian" in selectable, "Arabic and Italian are selectable languages")

    print(f"[INFO] Selectable languages audited: {len(selectable)}")
    print(f"[INFO] Exact translation keys: {len(exact_rows)}")
    print(f"[INFO] Object translation keys: {len(object_rows)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
