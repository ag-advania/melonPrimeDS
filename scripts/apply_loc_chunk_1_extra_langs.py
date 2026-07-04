#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Merge 14 extra language fields into loc_chunk_1_full.json."""
from __future__ import annotations

import json
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
SOURCE = SCRIPTS / "loc_chunk_1_full.json"
EXTRA = SCRIPTS / "loc_chunk_1_extra_langs.json"
LANGS = ["ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"]


def main() -> None:
    rows = json.loads(SOURCE.read_text(encoding="utf-8"))
    extra: dict[str, dict[str, str]] = json.loads(EXTRA.read_text(encoding="utf-8"))

    missing = []
    for row in rows:
        en = row["en"]
        if en not in extra:
            missing.append(en)
            continue
        for lang in LANGS:
            row[lang] = extra[en][lang]

    if missing:
        raise SystemExit(f"Missing translations for {len(missing)} entries:\n" + "\n".join(repr(m) for m in missing))

    out_rows = []
    for row in rows:
        ordered = {}
        for key in ["en", "ja", "de", "es", "fr", "it", "nl", "pt", "ru", "zh", "ko", *LANGS, "section"]:
            ordered[key] = row[key]
        out_rows.append(ordered)

    SOURCE.write_text(json.dumps(out_rows, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Updated {len(out_rows)} entries with {len(LANGS)} extra languages")


if __name__ == "__main__":
    main()
