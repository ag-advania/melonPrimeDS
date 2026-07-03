#!/usr/bin/env python3
"""Compare two MelonPrime perf logs/summaries for V6 S24 reproducibility.

Passes when p50 and p99 differ by no more than the configured relative
threshold (default: 10%). Inputs may be raw perf logs or .summary.txt files.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass
from pathlib import Path


SHUTDOWN_RE = re.compile(
    r"\[MelonPrimePerf\] shutdown summary: frames=[0-9]+ "
    r"frame_ms p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+)"
)

V6_ROW_MARKER = "V6 §8 frame row:"


@dataclass(frozen=True)
class FrameStats:
    p50: float
    p99: float
    source: str


def parse_stats(path: Path) -> FrameStats:
    text = path.read_text(encoding="utf-8", errors="replace")

    if match := SHUTDOWN_RE.search(text):
        return FrameStats(
            p50=float(match.group("p50")),
            p99=float(match.group("p99")),
            source="shutdown",
        )

    lines = text.splitlines()
    for idx, line in enumerate(lines):
        if line.strip() != V6_ROW_MARKER:
            continue
        for candidate in lines[idx + 1:]:
            if not candidate.startswith("| "):
                continue
            cells = [cell.strip() for cell in candidate.strip().strip("|").split("|")]
            if len(cells) < 4:
                break
            return FrameStats(p50=float(cells[1]), p99=float(cells[3]), source="v6-row")

    raise ValueError(f"could not find shutdown summary or V6 frame row in {path}")


def relative_diff(a: float, b: float) -> float:
    denom = (abs(a) + abs(b)) / 2.0
    if denom == 0.0:
        return 0.0
    return abs(a - b) / denom


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("first", type=Path)
    parser.add_argument("second", type=Path)
    parser.add_argument("--threshold", type=float, default=0.10, help="relative threshold; default 0.10")
    args = parser.parse_args()

    first = parse_stats(args.first)
    second = parse_stats(args.second)
    p50_diff = relative_diff(first.p50, second.p50)
    p99_diff = relative_diff(first.p99, second.p99)

    print(f"first:  p50={first.p50:.3f} p99={first.p99:.3f} ({first.source})")
    print(f"second: p50={second.p50:.3f} p99={second.p99:.3f} ({second.source})")
    print(f"diff:   p50={p50_diff * 100.0:.2f}% p99={p99_diff * 100.0:.2f}%")

    if p50_diff <= args.threshold and p99_diff <= args.threshold:
        print(f"PASS: p50/p99 are within +/-{args.threshold * 100.0:.1f}%")
        return 0

    print(f"FAIL: p50/p99 exceed +/-{args.threshold * 100.0:.1f}%")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
