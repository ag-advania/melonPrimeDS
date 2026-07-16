#!/usr/bin/env python3
"""Complete zh-Hant entries from zh-Hans entries.

This script updates:
  src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeTranslations.inc
  src/frontend/qt_sdl/MelonPrimeLocalization/MelonPrimeObjectTranslations.inc
  src/frontend/qt_sdl/MelonPrimeLocalizationMelondsDialogs.inc

By default it inserts {MenuLangId::ChineseTraditional, "..."} immediately after
{MenuLangId::ChineseSimplified, "..."} when the current translation block does
not already contain a ChineseTraditional entry. Pass --regenerate-existing to
also recompute and overwrite ChineseTraditional entries that already exist
(useful after changing the OpenCC config or the terminology dictionary below).

OpenCC is required for conversion quality. This uses the "s2twp" config
(Simplified -> Traditional, Taiwan standard, with phrase substitution) rather
than plain "s2t" (character-only conversion) or "s2tw" (character variants
only, no vocabulary substitution): neither of those two rewrites
mainland-specific vocabulary (e.g. 設置/文件/鼠標/屏幕/網絡) to the Taiwan-UI
equivalent (設定/檔案/滑鼠/螢幕/網路), even though the characters themselves are
already valid Traditional forms. ZH_HANT_TERMINOLOGY_FIXES below is applied on
top as a deterministic safety net for the handful of terms s2twp's phrase
dictionary still leaves as mainland vocabulary (攝像頭, 以太網).
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
TRADITIONAL_LINE_RE = re.compile(
    r'^(?P<indent>\s*)\{MenuLangId::ChineseTraditional,\s*(?P<literal>"(?:\\.|[^"\\])*")\},(?P<trailing>\s*)$'
)
LANG_LINE_RE = re.compile(r'^\s*\{MenuLangId::[A-Za-z0-9_]+,')

# Terminology fixes applied after OpenCC conversion, to correct mainland-only
# vocabulary that "s2tw" (and especially plain "s2t") can still leave behind.
# Ordered so longer/more specific keys are replaced before shorter ones that
# could otherwise be a substring of an already-fixed run.
# See docs/archive/ for the audit that produced this list; keep it in
# sync with ZH_HANT_TERMINOLOGY_HINTS in
# tools/ci/audits/localization/audit-melonprime-localization.py.
ZH_HANT_TERMINOLOGY_FIXES: list[tuple[str, str]] = [
    ("以太網", "乙太網路"),
    ("攝像頭", "相機"),
    ("默認", "預設"),
    ("設置", "設定"),
    ("文件", "檔案"),
    ("加載", "載入"),
    ("連接", "連線"),
    ("網絡", "網路"),
    ("鼠標", "滑鼠"),
    ("屏幕", "螢幕"),
    ("圖標", "圖示"),
    # OpenCC's TWPhrases dictionary converts this inconsistently depending on
    # surrounding segmentation; force the Taiwan-standard spelling everywhere.
    ("布局", "佈局"),
    # In this catalog "質量" only ever means "(video) quality" (VIDEO QUALITY /
    # Video quality: Low|High|High2), never physical mass, so a blanket
    # replacement to the Taiwan-standard "畫質" is safe for this dataset.
    ("質量", "畫質"),
    ("爲", "為"),
]


def load_opencc():
    try:
        from opencc import OpenCC  # type: ignore
    except Exception as exc:
        raise SystemExit(
            "OpenCC is required for reliable zh-Hans -> zh-Hant conversion.\n"
            "Install it with:\n"
            "  python3 -m pip install opencc-python-reimplemented\n"
        ) from exc

    # "s2twp" applies Taiwan-standard phrase/vocabulary conversion (TWPhrases)
    # on top of the base Simplified->Traditional character mapping. Plain "s2t"
    # and "s2tw" are character/variant-only and leave mainland vocabulary
    # (設置/文件/鼠標/屏幕/網絡/...) untouched.
    return OpenCC("s2twp")


def apply_terminology_fixes(text: str) -> str:
    for mainland, taiwan in ZH_HANT_TERMINOLOGY_FIXES:
        text = text.replace(mainland, taiwan)
    return text


def cpp_string_to_py(literal: str) -> str:
    # C++ normal string literals used in the translation table are compatible
    # enough with Python string literal parsing for the current data.
    return ast.literal_eval(literal)


def py_to_cpp_string(text: str) -> str:
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"'


def next_language_line_traditional_index(lines: list[str], index: int) -> int | None:
    # Scan forward until the next MenuLangId line or the end of the values block.
    # Current table convention is ChineseSimplified -> ChineseTraditional -> Korean.
    for j in range(index + 1, min(index + 8, len(lines))):
        line = lines[j]
        if "MenuLangId::ChineseTraditional" in line:
            return j
        if LANG_LINE_RE.match(line):
            return None
        if line.strip() == "}":
            return None
    return None


def process_file(path: Path, converter, regenerate_existing: bool) -> tuple[int, int]:
    text = path.read_text(encoding="utf-8")
    lines = text.splitlines(keepends=True)
    out: list[str] = []
    inserted = 0
    existing = 0
    regenerated = 0

    i = 0
    while i < len(lines):
        line = lines[i]
        out.append(line)
        match = SIMPLIFIED_LINE_RE.match(line.rstrip("\n"))
        if not match:
            i += 1
            continue

        simplified = cpp_string_to_py(match.group("literal"))
        traditional = apply_terminology_fixes(converter.convert(simplified))

        traditional_idx = next_language_line_traditional_index(lines, i)
        if traditional_idx is not None:
            existing += 1
            if regenerate_existing:
                trad_match = TRADITIONAL_LINE_RE.match(lines[traditional_idx].rstrip("\n"))
                if trad_match:
                    current = cpp_string_to_py(trad_match.group("literal"))
                    if current != traditional:
                        new_line = (
                            f'{trad_match.group("indent")}{{MenuLangId::ChineseTraditional, '
                            f'{py_to_cpp_string(traditional)}}},{trad_match.group("trailing")}'
                        )
                        if lines[traditional_idx].endswith("\n"):
                            new_line += "\n"
                        lines[traditional_idx] = new_line
                        regenerated += 1
            i += 1
            continue

        out.append(
            f'{match.group("indent")}{{MenuLangId::ChineseTraditional, '
            f'{py_to_cpp_string(traditional)}}},{match.group("trailing")}'
            + ("\n" if line.endswith("\n") else "")
        )
        inserted += 1
        i += 1

    if inserted or regenerated:
        path.write_text("".join(out), encoding="utf-8")

    if regenerated:
        print(f"[INFO] {path}: regenerated={regenerated}", file=sys.stderr)

    return inserted, existing


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path("."), help="Repository root")
    parser.add_argument(
        "--regenerate-existing",
        action="store_true",
        help="Recompute and overwrite ChineseTraditional entries that already exist",
    )
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
        inserted, existing = process_file(path, converter, args.regenerate_existing)
        total_inserted += inserted
        total_existing += existing
        print(f"[INFO] {path.relative_to(repo)}: inserted={inserted}, existing={existing}")

    print(f"[PASS] zh-Hant completion finished: inserted={total_inserted}, existing={total_existing}")
    if total_inserted:
        print("[NEXT] Run: python3 tools/ci/audits/localization/audit-melonprime-localization.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
