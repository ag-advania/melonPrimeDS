#!/usr/bin/env python3
"""Summarize VulkanPerf/DX12Perf 1 Hz stage telemetry after warmup.

Example:
  python tools/perf/summarize-renderer-perf.py perf.log --last-windows 300

The renderer emits one percentile row per CPU metric and one counter row per
reporting window. Selecting the last N rows for each metric makes a timed run
with a warmup prefix reproducible without rewriting the raw log.
"""

from __future__ import annotations

import argparse
import re
import statistics
from collections import defaultdict
from pathlib import Path


CPU_RE = re.compile(
    r"^\[(?P<backend>VulkanPerf|DX12Perf)\] cpu scale=(?P<scale>[0-9]+) "
    r"name=(?P<name>[a-z0-9_]+) p50_us=(?P<p50>[0-9.]+) "
    r"p95_us=(?P<p95>[0-9.]+) p99_us=(?P<p99>[0-9.]+) "
    r"max_us=(?P<max>[0-9.]+) n=(?P<n>[0-9]+)$"
)
COUNTER_RE = re.compile(
    r"^\[(?P<backend>VulkanPerf|DX12Perf)\] counters "
    r"scale=(?P<scale>[0-9]+) (?P<body>.*)$"
)
VALUE_RE = re.compile(r"(?P<name>[a-zA-Z0-9_]+)=(?P<value>[0-9]+)")
FRAME_RE = re.compile(
    r"^\[MelonPrimePerf\] frame_ms p50=(?P<p50>[0-9.]+) "
    r"p95=(?P<p95>[0-9.]+) p99=(?P<p99>[0-9.]+) "
    r"max=(?P<max>[0-9.]+) n=(?P<n>[0-9]+)"
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("log", type=Path)
    parser.add_argument("--last-windows", type=int, default=300)
    args = parser.parse_args()
    if args.last_windows <= 0:
        parser.error("--last-windows must be positive")

    metrics: dict[str, list[dict[str, float | int]]] = defaultdict(list)
    counters: list[dict[str, int]] = []
    frame_windows: list[dict[str, float | int]] = []
    backends: set[str] = set()
    scales: set[int] = set()

    with args.log.open("r", encoding="utf-8", errors="replace") as source:
        for raw_line in source:
            line = raw_line.rstrip("\r\n")
            if match := CPU_RE.match(line):
                backends.add(match.group("backend").removesuffix("Perf"))
                scales.add(int(match.group("scale")))
                metrics[match.group("name")].append({
                    "p50": float(match.group("p50")),
                    "p95": float(match.group("p95")),
                    "p99": float(match.group("p99")),
                    "max": float(match.group("max")),
                    "n": int(match.group("n")),
                })
            elif match := COUNTER_RE.match(line):
                backends.add(match.group("backend").removesuffix("Perf"))
                scales.add(int(match.group("scale")))
                counters.append({
                    item.group("name"): int(item.group("value"))
                    for item in VALUE_RE.finditer(match.group("body"))
                })
            elif match := FRAME_RE.match(line):
                frame_windows.append({
                    "p50": float(match.group("p50")),
                    "p95": float(match.group("p95")),
                    "p99": float(match.group("p99")),
                    "max": float(match.group("max")),
                    "n": int(match.group("n")),
                })

    if len(backends) != 1 or len(scales) != 1 or not metrics:
        raise SystemExit(
            f"expected one renderer and scale with CPU telemetry; "
            f"found backends={sorted(backends)} scales={sorted(scales)}"
        )

    backend = next(iter(backends))
    scale = next(iter(scales))
    print(f"backend={backend} scale={scale} requested_windows={args.last_windows}")
    selected_frames = frame_windows[-args.last_windows :]
    if selected_frames:
        print(
            "frame_ms "
            f"windows={len(selected_frames)} "
            f"frames={sum(int(row['n']) for row in selected_frames)} "
            f"median_p50={statistics.median(float(row['p50']) for row in selected_frames):.3f} "
            f"median_p95={statistics.median(float(row['p95']) for row in selected_frames):.3f} "
            f"median_p99={statistics.median(float(row['p99']) for row in selected_frames):.3f} "
            f"median_max={statistics.median(float(row['max']) for row in selected_frames):.3f}"
        )
    print("metric windows samples median_p50_us median_p95_us median_p99_us median_max_us")
    for name in sorted(metrics):
        rows = metrics[name][-args.last_windows :]
        print(
            f"{name} {len(rows)} {sum(int(row['n']) for row in rows)} "
            f"{statistics.median(float(row['p50']) for row in rows):.3f} "
            f"{statistics.median(float(row['p95']) for row in rows):.3f} "
            f"{statistics.median(float(row['p99']) for row in rows):.3f} "
            f"{statistics.median(float(row['max']) for row in rows):.3f}"
        )

    selected_counters = counters[-args.last_windows :]
    totals: dict[str, int] = defaultdict(int)
    for row in selected_counters:
        for name, value in row.items():
            totals[name] += value
    frames = totals.get("frames", 0)
    print(f"counter_windows={len(selected_counters)}")
    for name in sorted(totals):
        per_frame = totals[name] / frames if frames else 0.0
        print(f"counter {name} total={totals[name]} per_frame={per_frame:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
