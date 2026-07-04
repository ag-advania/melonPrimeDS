#!/usr/bin/env python3
"""Merge hand-reviewed extra languages into loc_chunk_2_full.json."""
import importlib.util
import json
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parent
LANGS = ["ar", "id", "uk", "el", "sv", "th", "cs", "da", "tr", "nb", "hu", "fi", "vi", "pl", "ro"]


def load_part(name: str):
    spec = importlib.util.spec_from_file_location(name, SCRIPTS / f"loc_chunk_2_extra_langs_{name}.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    part_num = name[-1]
    return getattr(mod, f"PART{part_num}")


def main() -> int:
    extra = []
    for part in ("part1", "part2", "part3", "part4"):
        chunk = load_part(part)
        extra.extend(chunk)

    src = SCRIPTS / "loc_chunk_2_full.json"
    entries = json.loads(src.read_text(encoding="utf-8"))

    if len(entries) != 192:
        raise SystemExit(f"Expected 192 entries, got {len(entries)}")
    if len(extra) != 192:
        raise SystemExit(f"Expected 192 translations, got {len(extra)}")

    out = []
    for entry, langs in zip(entries, extra):
        row = dict(entry)
        for key in LANGS:
            if key not in langs:
                raise SystemExit(f"Missing {key} for {entry['en']!r}")
            row[key] = langs[key]
        out.append(row)

    src.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(len(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
