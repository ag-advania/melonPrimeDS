#!/usr/bin/env python3
# -*- coding: utf-8 -*-
from pathlib import Path

PATH = Path(__file__).resolve().parents[3] / (
    "src/frontend/qt_sdl/MelonPrimeLocalization/inc/MelonPrimeTranslationsHotkeys.inc"
)

REPLACEMENTS = [
    ('"[Metroid] Next Weapon (Primary)"',
     '"[Metroid] (J) Next Weapon (Primary)"'),
    ('"[Metroid] Next Weapon (Secondary)"',
     '"[Metroid] (Mouse Wheel Down) Next Weapon (Secondary)"'),
    ('"[Metroid] Previous Weapon (Primary)"',
     '"[Metroid] (K) Previous Weapon (Primary)"'),
    ('"[Metroid] Previous Weapon (Secondary)"',
     '"[Metroid] (Mouse Wheel Up) Previous Weapon (Secondary)"'),
]


def main() -> int:
    text = PATH.read_text(encoding="utf-8")
    for old, new in REPLACEMENTS:
        if old not in text:
            raise SystemExit("missing catalog key: " + old)
        text = text.replace(old, new, 1)
    PATH.write_text(text, encoding="utf-8", newline="\n")
    for _, new in REPLACEMENTS:
        key = new.strip('"')
        print(("OK" if key in text else "MISSING"), key)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
