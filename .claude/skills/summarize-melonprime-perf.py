#!/usr/bin/env python3
"""Parse MelonPrimePerf stderr lines and print rolling / shutdown summaries.

Usage:
  MELONPRIME_PERF=1 ./melonPrimeDS 2>&1 | tee perf.log
  python3 .claude/skills/summarize-melonprime-perf.py perf.log

Reads lines prefixed with [MelonPrimePerf]. Supports 1 Hz window lines and the
shutdown histogram block emitted when the emu thread exits.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from typing import Iterable, TextIO


WINDOW_RE = re.compile(
    r"\[MelonPrimePerf\] frame_ms p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+) n=(?P<n>[0-9]+)"
)

SHUTDOWN_RE = re.compile(
    r"\[MelonPrimePerf\] shutdown summary: frames=(?P<frames>[0-9]+) "
    r"frame_ms p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+)"
)

HIST_RE = re.compile(
    r"^\s+(?P<lo>[0-9.]+)-(?P<hi>[0-9.]+) ms: (?P<count>[0-9]+)$"
)


@dataclass
class WindowSample:
    p50: float
    p95: float
    p99: float
    max_ms: float
    n: int


@dataclass
class Report:
    windows: list[WindowSample] = field(default_factory=list)
    shutdown: WindowSample | None = None
    histogram: list[tuple[float, float, int]] = field(default_factory=list)
    hist_overflow: int = 0


def parse_lines(lines: Iterable[str]) -> Report:
    report = Report()
    in_hist = False

    for line in lines:
        line = line.rstrip("\n")
        if line.startswith("[MelonPrimePerf] histogram"):
            in_hist = True
            continue

        m = WINDOW_RE.search(line)
        if m:
            report.windows.append(
                WindowSample(
                    p50=float(m.group("p50")),
                    p95=float(m.group("p95")),
                    p99=float(m.group("p99")),
                    max_ms=float(m.group("max")),
                    n=int(m.group("n")),
                )
            )
            continue

        m = SHUTDOWN_RE.search(line)
        if m:
            report.shutdown = WindowSample(
                p50=float(m.group("p50")),
                p95=float(m.group("p95")),
                p99=float(m.group("p99")),
                max_ms=float(m.group("max")),
                n=int(m.group("frames")),
            )
            in_hist = True
            continue

        if in_hist:
            if line.startswith("  >="):
                parts = line.split(":")
                if len(parts) == 2:
                    report.hist_overflow = int(parts[1].strip())
                continue
            m = HIST_RE.match(line)
            if m:
                report.histogram.append(
                    (float(m.group("lo")), float(m.group("hi")), int(m.group("count")))
                )

    return report


def print_report(report: Report, out: TextIO) -> None:
    if not report.windows and not report.shutdown:
        print("No [MelonPrimePerf] lines found.", file=out)
        return

    if report.windows:
        print(f"1 Hz windows: {len(report.windows)}", file=out)
        last = report.windows[-1]
        print(
            f"  last window: p50={last.p50:.3f} p95={last.p95:.3f} "
            f"p99={last.p99:.3f} max={last.max_ms:.3f} n={last.n}",
            file=out,
        )
        if len(report.windows) > 1:
            p50s = [w.p50 for w in report.windows]
            p99s = [w.p99 for w in report.windows]
            print(
                f"  window p50 range: {min(p50s):.3f} .. {max(p50s):.3f}",
                file=out,
            )
            print(
                f"  window p99 range: {min(p99s):.3f} .. {max(p99s):.3f}",
                file=out,
            )

    if report.shutdown:
        s = report.shutdown
        print(
            f"shutdown ({s.n} frames): p50={s.p50:.3f} p95={s.p95:.3f} "
            f"p99={s.p99:.3f} max={s.max_ms:.3f}",
            file=out,
        )

    if report.histogram:
        total = sum(c for _, _, c in report.histogram) + report.hist_overflow
        print(f"histogram buckets: {len(report.histogram)} (+overflow={report.hist_overflow}), total={total}", file=out)
        for lo, hi, count in report.histogram:
            bar = "#" * min(60, count // max(1, total // 60 or 1))
            print(f"  {lo:5.1f}-{hi:5.1f} ms: {count:6d} {bar}", file=out)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "log",
        nargs="?",
        help="perf log file (default: stdin)",
    )
    args = parser.parse_args()

    if args.log:
        with open(args.log, encoding="utf-8", errors="replace") as f:
            report = parse_lines(f)
    else:
        report = parse_lines(sys.stdin)

    print_report(report, sys.stdout)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
