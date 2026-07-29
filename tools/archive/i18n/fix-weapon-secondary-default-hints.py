#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Replace leftover (J)/(K) hints in Secondary weapon hotkey translations."""
from __future__ import annotations

import re
from pathlib import Path

PATH = Path(__file__).resolve().parents[3] / (
    "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsHotkeys.inc"
)


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


def rewrite_row(row: str, old_hint: str, new_hint: str) -> str:
    return row.replace(old_hint, new_hint)


def main() -> int:
    text = PATH.read_text(encoding="utf-8")
    next_sec = "[Metroid] (Mouse Wheel Down) Next Weapon (Secondary)"
    prev_sec = "[Metroid] (Mouse Wheel Up) Previous Weapon (Secondary)"

    ns, ne, nrow = extract_row(text, next_sec)
    ps, pe, prow = extract_row(text, prev_sec)

    nrow = rewrite_row(nrow, "(J)", "(Mouse Wheel Down)")
    prow = rewrite_row(prow, "(K)", "(Mouse Wheel Up)")

    text = text[:ps] + prow + text[pe:]
    text = text[:ns] + nrow + text[ne:]
    PATH.write_text(text, encoding="utf-8", newline="\n")

    # Spot-check Japanese rows
    assert "(Mouse Wheel Down)" in text
    assert "(Mouse Wheel Up)" in text
    assert '"[Metroid] (J) 並び順で次の武器（セカンダリ）"' not in text
    assert '"[Metroid] (K) 並び順で前の武器（セカンダリ）"' not in text
    print("[PASS] secondary default hints use mouse wheel")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
