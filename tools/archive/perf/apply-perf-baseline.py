#!/usr/bin/env python3
"""Apply MelonPrime perf baseline summaries to the V6 and V5 markdown tables.

Inputs are the .summary.txt files produced by collect-perf-baseline.{sh,ps1}.
The script updates only the platforms provided on the command line.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_V6 = REPO_ROOT / "docs/archive/plans/refactoring/melonprime-full-refactor-plan-v6.md"
DEFAULT_V5 = REPO_ROOT / "docs/archive/plans/refactoring/melonprime-full-refactor-plan-v5.md"


@dataclass(frozen=True)
class PlatformSummary:
    v6_name: str
    v5_name: str
    v6_frame_row: str
    v5_frame_row: str
    counters: dict[str, str]


PLATFORMS = {
    "macos": ("macOS", "macOS", 1),
    "windows": ("Windows", "Windows", 2),
    "linux": ("Linux VM", "Linux VM", 3),
}

COUNTER_ROWS = {
    "`Patches_Apply(OutOfGameFrame)` / min": "`Patches_Apply(OutOfGameFrame)` / 分",
    "`OsdColor_ApplyOnce` write / min": "`OsdColor_ApplyOnce` 実 write / 分",
    "HUD dirty area avg px/frame": "HUD dirty 面積 平均 px",
    "GL upload bytes/frame": "GL upload バイト / frame",
    "DR3 hash skip/frame": "DR3 hash skip 率",
    "`CustomHud_Render` p50/p99 us": "`CustomHud_Render` 所要 p50/p99",
}


def read_summary(path: Path, key: str) -> PlatformSummary:
    text = path.read_text(encoding="utf-8", errors="replace")
    v6_name, v5_name, _ = PLATFORMS[key]

    v6_frame = find_next_table_row(text, "V6 §8 frame row:")
    v5_frame = find_next_table_row(text, "V5 Phase 0 frame row:")
    counters = parse_counter_cells(text)

    return PlatformSummary(
        v6_name=v6_name,
        v5_name=v5_name,
        v6_frame_row=replace_platform_cell(v6_frame, v6_name),
        v5_frame_row=replace_platform_cell(v5_frame, v5_name),
        counters=counters,
    )


def find_next_table_row(text: str, marker: str) -> str:
    lines = text.splitlines()
    for idx, line in enumerate(lines):
        if line.strip() != marker:
            continue
        for candidate in lines[idx + 1:]:
            if candidate.startswith("| "):
                return candidate
    raise ValueError(f"missing table row after marker: {marker}")


def replace_platform_cell(row: str, platform: str) -> str:
    cells = split_row(row)
    if len(cells) < 2:
        raise ValueError(f"invalid markdown row: {row}")
    cells[0] = platform
    return build_row(cells)


def parse_counter_cells(text: str) -> dict[str, str]:
    counters: dict[str, str] = {}
    in_block = False
    for line in text.splitlines():
        stripped = line.strip()
        if stripped == "V6 §8 counter cells for this platform:":
            in_block = True
            continue
        if in_block and not stripped:
            break
        if not in_block or not stripped.startswith("- "):
            continue
        body = stripped[2:]
        if ": " not in body:
            continue
        label, value = body.split(": ", 1)
        if label in COUNTER_ROWS:
            counters[COUNTER_ROWS[label]] = value.strip()

    missing = sorted(set(COUNTER_ROWS.values()) - set(counters))
    if missing:
        raise ValueError(f"summary is missing counter cells: {', '.join(missing)}")
    return counters


def split_row(row: str) -> list[str]:
    return [cell.strip() for cell in row.strip().strip("|").split("|")]


def build_row(cells: list[str]) -> str:
    return "| " + " | ".join(cells) + " |"


def replace_platform_row(text: str, platform: str, new_row: str, preserve_note: bool) -> str:
    pattern = re.compile(rf"^\| {re.escape(platform)} \|.*$", re.MULTILINE)
    new_cells = split_row(new_row)

    def repl(match: re.Match[str]) -> str:
        old_cells = split_row(match.group(0))
        merged = list(new_cells)
        if (
            preserve_note
            and old_cells
            and merged
            and len(old_cells) == len(merged)
            and not merged[-1]
            and old_cells[-1]
        ):
            merged[-1] = old_cells[-1]
        return build_row(merged)

    text, count = pattern.subn(repl, text, count=1)
    if count != 1:
        raise ValueError(f"could not replace row for platform: {platform}")
    return text


def replace_counter_cells(text: str, summaries: dict[str, PlatformSummary]) -> str:
    out_lines: list[str] = []
    for line in text.splitlines():
        if not line.startswith("| "):
            out_lines.append(line)
            continue

        cells = split_row(line)
        if not cells or cells[0] not in COUNTER_ROWS.values():
            out_lines.append(line)
            continue

        for key, summary in summaries.items():
            _, _, cell_index = PLATFORMS[key]
            if len(cells) <= cell_index:
                raise ValueError(f"counter row has too few cells: {line}")
            cells[cell_index] = summary.counters[cells[0]]
        out_lines.append(build_row(cells))

    trailing_newline = "\n" if text.endswith("\n") else ""
    return "\n".join(out_lines) + trailing_newline


def apply_summaries(v6_path: Path, v5_path: Path, summaries: dict[str, PlatformSummary]) -> None:
    v6 = v6_path.read_text(encoding="utf-8")
    v5 = v5_path.read_text(encoding="utf-8")

    for key, summary in summaries.items():
        v6 = replace_platform_row(v6, summary.v6_name, summary.v6_frame_row, preserve_note=True)
        v5 = replace_platform_row(v5, summary.v5_name, summary.v5_frame_row, preserve_note=False)
    v6 = replace_counter_cells(v6, summaries)

    v6_path.write_text(v6, encoding="utf-8")
    v5_path.write_text(v5, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--macos", type=Path, help="macOS .summary.txt")
    parser.add_argument("--windows", type=Path, help="Windows .summary.txt")
    parser.add_argument("--linux", type=Path, help="Linux VM .summary.txt")
    parser.add_argument("--v6", type=Path, default=DEFAULT_V6)
    parser.add_argument("--v5", type=Path, default=DEFAULT_V5)
    args = parser.parse_args()

    summaries: dict[str, PlatformSummary] = {}
    for key in ("macos", "windows", "linux"):
        path = getattr(args, key)
        if path:
            summaries[key] = read_summary(path, key)

    if not summaries:
        parser.error("provide at least one of --macos, --windows, or --linux")

    apply_summaries(args.v6, args.v5, summaries)
    print("Updated:")
    print(f"  {args.v6}")
    print(f"  {args.v5}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
