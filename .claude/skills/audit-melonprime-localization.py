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
RAW_STRING_RE = re.compile(r'R"(?P<delim>[A-Za-z0-9_]*)\(')
LANGUAGE_COLUMNS = [
    ("Japanese", "ja"),
    ("German", "de"),
    ("Spanish", "es"),
    ("French", "fr"),
    ("Italian", "it"),
    ("Dutch", "nl"),
    ("Portuguese", "pt"),
    ("Russian", "ru"),
    ("ChineseSimplified", "zh-Hans"),
    ("Korean", "ko"),
    ("Arabic", "ar"),
    ("Indonesian", "id"),
    ("Ukrainian", "uk"),
    ("Greek", "el"),
    ("Swedish", "sv"),
    ("Thai", "th"),
    ("Czech", "cs"),
    ("Danish", "da"),
    ("Turkish", "tr"),
    ("Norwegian", "nb"),
    ("Hungarian", "hu"),
    ("Finnish", "fi"),
    ("Vietnamese", "vi"),
    ("Polish", "pl"),
    ("Romanian", "ro"),
]


def read_text(path: Path, seen: set[Path] | None = None) -> str:
    if seen is None:
        seen = set()
    path = path.resolve()
    if path in seen:
        return ""
    seen.add(path)

    text = path.read_text(encoding="utf-8")

    def expand_inc(match: re.Match[str]) -> str:
        include_name = match.group("path")
        candidates = [path.parent / include_name, QT_SDL / include_name]
        for candidate in candidates:
            if candidate.exists():
                return read_text(candidate, seen)
        return match.group(0)

    return re.sub(r'#include\s+"(?P<path>[^"]+\.inc)"', expand_inc, text)


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

        raw = RAW_STRING_RE.match(text, i)
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            i = raw.end()
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

        raw = RAW_STRING_RE.match(body, i)
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            i = raw.end()
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


def decode_cpp_escape(s: str, i: int) -> tuple[str, int]:
    if i >= len(s):
        return "\\", i
    ch = s[i]
    simple = {
        "n": "\n",
        "r": "\r",
        "t": "\t",
        '"': '"',
        "'": "'",
        "\\": "\\",
        "0": "\0",
    }
    if ch in simple:
        return simple[ch], i + 1
    if ch == "x":
        j = i + 1
        while j < len(s) and s[j] in "0123456789abcdefABCDEF":
            j += 1
        if j > i + 1:
            return chr(int(s[i + 1 : j], 16)), j
    if ch == "u" and i + 4 < len(s):
        digits = s[i + 1 : i + 5]
        if all(c in "0123456789abcdefABCDEF" for c in digits):
            return chr(int(digits, 16)), i + 5
    if ch == "U" and i + 8 < len(s):
        digits = s[i + 1 : i + 9]
        if all(c in "0123456789abcdefABCDEF" for c in digits):
            return chr(int(digits, 16)), i + 9
    return ch, i + 1


def parse_cpp_strings(entry: str) -> list[str]:
    values: list[str] = []
    i = 0
    while i < len(entry):
        raw = RAW_STRING_RE.match(entry, i)
        if raw:
            raw_end = ")" + raw.group("delim") + '"'
            content_start = raw.end()
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
        while i < len(entry):
            ch = entry[i]
            if ch == "\\":
                decoded, i = decode_cpp_escape(entry, i + 1)
                chars.append(decoded)
                continue
            if ch == '"':
                break
            chars.append(ch)
            i += 1
        values.append("".join(chars))
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


def function_body(source: str, signature: str) -> str:
    marker = source.find(signature)
    if marker < 0:
        raise RuntimeError(f"Function not found: {signature}")

    start = source.find("{", marker)
    if start < 0:
        raise RuntimeError(f"Function body not found: {signature}")

    depth = 0
    for i in range(start, len(source)):
        char = source[i]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start + 1 : i]
    raise RuntimeError(f"Function body not closed: {signature}")


def covered_count(rows: list[list[str]], column_index: int) -> int:
    return sum(1 for row in rows if len(row) > column_index and bool(row[column_index]))


def print_coverage_report(exact_rows: list[list[str]], object_rows: list[list[str]]) -> None:
    exact_total = len(exact_rows)
    object_total = len(object_rows)
    print("[INFO] Coverage:")
    for idx, (_name, code) in enumerate(LANGUAGE_COLUMNS, start=1):
        print(
            f"[INFO]   {code}: "
            f"exact {covered_count(exact_rows, idx)}/{exact_total}, "
            f"object {covered_count(object_rows, idx)}/{object_total}"
        )

    print("[INFO] Fallbacks:")
    print("[INFO]   en-GB -> en, en-US -> en")
    print("[INFO]   es-419 -> es, fr-CA -> fr, pt-BR -> pt")
    print("[INFO]   zh-Hant -> zh-Hans (ChineseTraditional not selectable)")

    legacy_dynamic = "ja/de/es/fr/it/nl/pt/ru/zh-Hans/ko"
    print("[INFO] Dynamic text coverage:")
    print(f"[INFO]   instance dialog labels: {legacy_dynamic}; other languages fallback to English")
    print(f"[INFO]   special dynamic labels ((none), Direct mode, native/camera): {legacy_dynamic}; other languages fallback to English")
    print("[INFO]   slot/screen prefixes: catalog-backed for all audited base languages")
    print("[INFO]   LAN warnings: catalog-backed for all audited base languages")


def main() -> int:
    enum_values = parse_menu_lang_values()
    source = all_source_text()
    chinese_traditional_unselectable = (
        "lang == MenuLangId::ChineseTraditional" in source
        and re.search(r"if\s*\(\s*lang\s*==\s*MenuLangId::ChineseTraditional\s*\)\s*continue\s*;", source)
    ) or re.search(
        r"\{\s*MenuLangId::ChineseTraditional\b[^{};]*MenuLangId::ChineseSimplified\s*,\s*false\s*,",
        source,
        re.S,
    )

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
    require(chinese_traditional_unselectable, "ChineseTraditional is not selectable")
    require(
        "QHash<QString, const Translation*>" in source
        and "QHash<QString, const ObjectTextTranslation*>" in source
        and "class TranslationCatalog" in source,
        "Translation catalog is indexed by QHash",
    )
    require(
        "struct TranslationValue" in source
        and "std::initializer_list<TranslationValue> values" in source
        and "{MenuLangId::Japanese," in source
        and "const char* ja;" not in source,
        "Translation data uses language-tagged key/value rows",
    )
    require(
        "kTranslations" not in function_body(source, "QString TranslateExact(const QString& text)")
        and "kObjectTextTranslations" not in function_body(
            source,
            "QString TranslateByObjectName(const QWidget* widget, const QString& text)",
        ),
        "Public translate functions do not scan source arrays",
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
    print_coverage_report(exact_rows, object_rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
