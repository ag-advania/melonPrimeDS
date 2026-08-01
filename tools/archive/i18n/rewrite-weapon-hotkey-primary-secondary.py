#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""One-shot rewrite: Next/Previous Weapon hotkey labels -> Primary/Secondary."""
from __future__ import annotations

import re
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
PATH = REPO / "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsHotkeys.inc"


def extract_row(text: str, key: str) -> tuple[int, int, str]:
    needle = '"' + key + '"'
    pos = text.find(needle)
    if pos < 0:
        raise SystemExit("missing key: " + key)
    start = text.rfind("    {", 0, pos)
    depth = 0
    ins = False
    esc = False
    for i in range(start, len(text)):
        ch = text[i]
        if ins:
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == '"':
                ins = False
        else:
            if ch == '"':
                ins = True
            elif ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    end = i + 1
                    while end < len(text) and text[end] in " \t":
                        end += 1
                    if end < len(text) and text[end] == ",":
                        end += 1
                    if end < len(text) and text[end] == "\n":
                        end += 1
                    return start, end, text[start:end]
    raise SystemExit("unterminated: " + key)


def transform_row(row: str, old_key: str, new_key: str, suffix_en: str) -> str:
    row = row.replace('"' + old_key + '"', '"' + new_key + '"', 1)

    def add_suffix(m: re.Match[str]) -> str:
        lang = m.group(1)
        val = m.group(2)
        if val.endswith(suffix_en) or val.endswith("（プライマリ）") or val.endswith("（セカンダリ）"):
            return m.group(0)
        if lang == "Japanese":
            jp = "（プライマリ）" if "Primary" in suffix_en else "（セカンダリ）"
            return '{MenuLangId::Japanese, "' + val + jp + '"},'
        return "{MenuLangId::" + lang + ', "' + val + suffix_en + '"},'

    return re.sub(
        r'\{MenuLangId::([A-Za-z0-9_]+),\s*"((?:\\.|[^"\\])*)"\},',
        add_suffix,
        row,
    )


def main() -> int:
    text = PATH.read_text(encoding="utf-8")

    next_old = "[Metroid] (J) Next Weapon in the sorted order"
    prev_old = "[Metroid] (K) Previous Weapon in the sorted order"
    next_pri = "[Metroid] (J) Next Weapon (Primary)"
    next_sec = "[Metroid] (Mouse Wheel Down) Next Weapon (Secondary)"
    prev_pri = "[Metroid] (K) Previous Weapon (Primary)"
    prev_sec = "[Metroid] (Mouse Wheel Up) Previous Weapon (Secondary)"

    ns, ne, nrow = extract_row(text, next_old)
    ps, pe, prow = extract_row(text, prev_old)

    next_primary = transform_row(nrow, next_old, next_pri, " (Primary)")
    next_secondary = transform_row(nrow, next_old, next_sec, " (Secondary)")
    prev_primary = transform_row(prow, prev_old, prev_pri, " (Primary)")
    prev_secondary = transform_row(prow, prev_old, prev_sec, " (Secondary)")

    # Replace later span first so earlier offsets stay valid.
    text = text[:ps] + prev_primary + prev_secondary + text[pe:]
    text = text[:ns] + next_primary + next_secondary + text[ne:]

    PATH.write_text(text, encoding="utf-8", newline="\n")
    checks = {
        "next_pri": next_pri in text,
        "next_sec": next_sec in text,
        "prev_pri": prev_pri in text,
        "prev_sec": prev_sec in text,
        "old_next_gone": next_old not in text,
        "old_prev_gone": prev_old not in text,
    }
    print(checks)
    if not all(checks.values()):
        raise SystemExit(1)
    print("[PASS] rewrote Next/Previous Weapon Primary/Secondary translations")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
