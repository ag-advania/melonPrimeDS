#!/usr/bin/env python3
"""Complete zh-Hant entries from zh-Hans entries.

This script updates:
  src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc
  src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc

It inserts {MenuLangId::ChineseTraditional, "..."} immediately after
{MenuLangId::ChineseSimplified, "..."} when the current translation block does
not already contain a ChineseTraditional entry.

OpenCC is required for conversion quality.
"""

from __future__ import annotations

import argparse
import ast
import re
import sys
from pathlib import Path


SIMPLIFIED_LINE_RE = re.compile(
    r'^(?P<indent>\s*)\{MenuLangId::ChineseSimplified,\s*(?P<literal>"(?:\\.|[^"\\])*")\},(?P<trailing>\s*)$'
)
LANG_LINE_RE = re.compile(r'^\s*\{MenuLangId::[A-Za-z0-9_]+,')


def load_opencc():
    try:
        from opencc import OpenCC  # type: ignore
    except Exception as exc:
        raise SystemExit(
            "OpenCC is required for reliable zh-Hans -> zh-Hant conversion.\n"
            "Install it with:\n"
            "  python3 -m pip install opencc-python-reimplemented\n"
        ) from exc

    # opencc-python-reimplemented accepts 's2t'.
    return OpenCC("s2t")


def cpp_string_to_py(literal: str) -> str:
    # C++ normal string literals used in the translation table are compatible
    # enough with Python string literal parsing for the current data.
    return ast.literal_eval(literal)


def py_to_cpp_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def next_language_line_has_traditional(lines: list[str], index: int) -> bool:
    # Scan forward until the next MenuLangId line or the end of the values block.
    # Current table convention is ChineseSimplified -> ChineseTraditional -> Korean.
    for j in range(index + 1, min(index + 8, len(lines))):
        line = lines[j]
        if "MenuLangId::ChineseTraditional" in line:
            return True
        if LANG_LINE_RE.match(line):
            return False
        if line.strip() == "}":
            return False
    return False


def process_file(path: Path, converter) -> tuple[int, int]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    inserted = 0
    existing = 0

    for i, line in enumerate(lines):
        out.append(line)
        match = SIMPLIFIED_LINE_RE.match(line.rstrip("\n"))
        if not match:
            continue

        if next_language_line_has_traditional(lines, i):
            existing += 1
            continue

        simplified = cpp_string_to_py(match.group("literal"))
        traditional = converter.convert(simplified)
        out.append(
            f'{match.group("indent")}{{MenuLangId::ChineseTraditional, '
            f'{py_to_cpp_string(traditional)}}},{match.group("trailing")}'
            + ("\n" if line.endswith("\n") else "")
        )
        inserted += 1

    if inserted:
        path.write_text("".join(out), encoding="utf-8")

    return inserted, existing


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root")
    args = parser.parse_args()

    repo = args.repo.resolve()
    files = [
        repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc",
        repo / "src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc",
        # Nested include pulled in by MelonPrimeTranslations.inc (melonDS dialog
        # strings); lives in qt_sdl/ root, not the MelonPrimeLocalization/ subdir.
        repo / "src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc",
    ]

    missing = [path for path in files if not path.exists()]
    if missing:
        for path in missing:
            print(f"[FAIL] Missing file: {path}", file=sys.stderr)
        return 1

    converter = load_opencc()

    total_inserted = 0
    total_existing = 0
    for path in files:
        inserted, existing = process_file(path, converter)
        total_inserted += inserted
        total_existing += existing
        print(f"[INFO] {path.relative_to(repo)}: inserted={inserted}, existing={existing}")

    print(f"[PASS] zh-Hant completion finished: inserted={total_inserted}, existing={total_existing}")
    if total_inserted:
        print("[NEXT] Run: python3 .claude/skills/audit-melonprime-localization.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
