#!/usr/bin/env python3
"""Merge hand-reviewed extra-language fields into loc_chunk_4_full.json."""

from __future__ import annotations

import json
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
EXTRA_LANGS = ["ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"]


def main() -> None:
    from loc_chunk_4_extra_data import TRANSLATIONS

    src = SCRIPTS / "loc_chunk_4_full.json"
    rows = json.loads(src.read_text(encoding="utf-8"))
    missing: list[str] = []

    for row in rows:
        en = row["en"]
        extra = TRANSLATIONS.get(en)
        if extra is None:
            missing.append(en)
            continue
        for lang in EXTRA_LANGS:
            row[lang] = extra[lang]

    if missing:
        raise SystemExit(f"Missing translations for {len(missing)} entries:\n" + "\n".join(missing))

    src.write_text(json.dumps(rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(len(rows))


if __name__ == "__main__":
    main()
