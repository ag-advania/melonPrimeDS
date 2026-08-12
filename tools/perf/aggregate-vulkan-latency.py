#!/usr/bin/env python3
"""Aggregate Vulkan present-pacing A/B capture CSVs into per-run and per-mode stats.

Input is whatever `MELONPRIME_ENABLE_VULKAN_LATENCY_CAPTURE` builds write (see
docs/development/testing/vulkan-present-pacing-runbook.md). One CSV per run.

Two rules this implements deliberately:

  * Percentiles are computed per run first, then reported per mode. Merging
    every frame of every run into one pool lets a long run outvote a short one,
    which is how a mode "wins" by having been recorded for longer.
  * Warm-up frames are dropped before any statistic is computed. Target-time
    scheduling starts inactive by design -- it needs a feedback baseline -- so
    including bootstrap frames measures the bootstrap, not the policy.

Usage:
    python tools/perf/aggregate-vulkan-latency.py runs/*.csv
    python tools/perf/aggregate-vulkan-latency.py --warmup 600 --out summary.csv runs/
"""

from __future__ import annotations

import argparse
import csv
import statistics
import sys
from pathlib import Path


# Mirrors VulkanPresentPacingPolicy / VulkanPacingAuthority. Kept as plain maps
# so the script stays runnable without the build tree.
POLICY_NAMES = {
    0: "TelemetryOnly",
    1: "PresentWait",
    2: "JustInTime",
    3: "JustInTimeFifoLatestReady",
}
AUTHORITY_NAMES = {
    0: "GenericHost",
    1: "NvidiaReflex",
    2: "AmdAntiLag2",
    3: "GenericPresentTiming",
}
REFLEX_NAMES = {0: "Off", 1: "On", 2: "On+Boost"}


def percentile(values: list[float], fraction: float) -> float:
    """Nearest-rank percentile. Deterministic and free of interpolation debates."""
    if not values:
        return float("nan")
    ordered = sorted(values)
    index = max(0, min(len(ordered) - 1, int(round(fraction * (len(ordered) - 1)))))
    return ordered[index]


def as_int(row: dict[str, str], key: str, default: int = 0) -> int:
    try:
        return int(row[key])
    except (KeyError, TypeError, ValueError):
        return default


class RunStats:
    def __init__(self, path: Path, warmup: int) -> None:
        self.path = path
        self.run_id = path.stem
        self.samples = 0
        self.dropped_warmup = 0
        self.frame_times_ms: list[float] = []
        self.input_to_present_ms: list[float] = []
        self.policy = -1
        self.authority = -1
        self.reflex_mode = -1
        self.target_active = 0
        self.bounded_wait = 0
        self.wait_timeouts = 0
        self.queue_full = 0
        self.queue_recoveries = 0
        self.queue_size = 0
        self._load(warmup)

    def _load(self, warmup: int) -> None:
        with self.path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))

        previous_present_end: int | None = None
        for index, row in enumerate(rows):
            if index < warmup:
                self.dropped_warmup += 1
                previous_present_end = as_int(row, "present_end_time_us")
                continue

            self.samples += 1
            self.run_id = row.get("run_id") or self.run_id
            self.policy = as_int(row, "policy", self.policy)
            self.authority = as_int(row, "authority", self.authority)
            self.reflex_mode = as_int(row, "reflex_mode", self.reflex_mode)
            self.target_active += as_int(row, "target_scheduling")
            self.bounded_wait += as_int(row, "bounded_wait")

            # These are running counters in the capture, so the last row holds
            # the run total rather than a per-frame delta.
            self.wait_timeouts = as_int(row, "wait_timeout_count", self.wait_timeouts)
            self.queue_full = as_int(row, "timing_queue_full_count", self.queue_full)
            self.queue_recoveries = as_int(
                row, "timing_queue_recovery_count", self.queue_recoveries
            )
            self.queue_size = as_int(row, "timing_queue_size", self.queue_size)

            present_end = as_int(row, "present_end_time_us")
            if previous_present_end is not None and present_end > previous_present_end:
                self.frame_times_ms.append((present_end - previous_present_end) / 1000.0)
            previous_present_end = present_end

            # Input sampling to the end of the present call. This is a host
            # pipeline proxy: it stops at the API call, not at a lit pixel, so
            # it must never be reported as system or click-to-photon latency.
            input_us = as_int(row, "input_sample_time_us")
            if input_us and present_end > input_us:
                self.input_to_present_ms.append((present_end - input_us) / 1000.0)

    @property
    def mode(self) -> str:
        policy = POLICY_NAMES.get(self.policy, f"policy{self.policy}")
        reflex = REFLEX_NAMES.get(self.reflex_mode, f"reflex{self.reflex_mode}")
        return f"{policy}/Reflex{reflex}"

    @property
    def target_active_ratio(self) -> float:
        return (self.target_active / self.samples) if self.samples else float("nan")

    def summary_row(self) -> dict[str, object]:
        return {
            "run_id": self.run_id,
            "file": self.path.name,
            "mode": self.mode,
            "policy": POLICY_NAMES.get(self.policy, self.policy),
            "authority": AUTHORITY_NAMES.get(self.authority, self.authority),
            "reflex_mode": REFLEX_NAMES.get(self.reflex_mode, self.reflex_mode),
            "samples": self.samples,
            "warmup_dropped": self.dropped_warmup,
            "fps_mean": round(1000.0 / statistics.fmean(self.frame_times_ms), 3)
            if self.frame_times_ms
            else float("nan"),
            "ft_p50_ms": round(percentile(self.frame_times_ms, 0.50), 4),
            "ft_p95_ms": round(percentile(self.frame_times_ms, 0.95), 4),
            "ft_p99_ms": round(percentile(self.frame_times_ms, 0.99), 4),
            "ft_p999_ms": round(percentile(self.frame_times_ms, 0.999), 4),
            "pipeline_p50_ms": round(percentile(self.input_to_present_ms, 0.50), 4),
            "pipeline_p95_ms": round(percentile(self.input_to_present_ms, 0.95), 4),
            "pipeline_p99_ms": round(percentile(self.input_to_present_ms, 0.99), 4),
            "target_active_ratio": round(self.target_active_ratio, 4),
            "bounded_wait_ratio": round(
                (self.bounded_wait / self.samples) if self.samples else float("nan"), 4
            ),
            "wait_timeout_count": self.wait_timeouts,
            "timing_queue_size": self.queue_size,
            "timing_queue_full_count": self.queue_full,
            "timing_queue_recovery_count": self.queue_recoveries,
        }


def collect_inputs(paths: list[str]) -> list[Path]:
    files: list[Path] = []
    for raw in paths:
        path = Path(raw)
        if path.is_dir():
            files.extend(sorted(path.glob("**/*.csv")))
        elif path.exists():
            files.append(path)
        else:
            print(f"warning: no such path: {path}", file=sys.stderr)
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="CSV files or directories of them")
    parser.add_argument(
        "--warmup",
        type=int,
        default=600,
        help="frames dropped from the start of each run (default: 600)",
    )
    parser.add_argument("--out", help="write the per-run summary to this CSV")
    args = parser.parse_args()

    files = collect_inputs(args.inputs)
    if not files:
        print("no capture CSVs found", file=sys.stderr)
        return 2

    runs = [RunStats(path, args.warmup) for path in files]
    runs = [run for run in runs if run.samples > 0]
    if not runs:
        print(
            f"every run had {args.warmup} frames or fewer; lower --warmup",
            file=sys.stderr,
        )
        return 2

    rows = [run.summary_row() for run in runs]
    fieldnames = list(rows[0].keys())

    writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

    if args.out:
        with Path(args.out).open("w", newline="", encoding="utf-8") as handle:
            out = csv.DictWriter(handle, fieldnames=fieldnames)
            out.writeheader()
            out.writerows(rows)

    # Per mode: the spread of each run's percentile, never a re-pooled percentile.
    # A mode is only interesting if its runs agree with each other.
    print("\n# per-mode (each cell is the median of the per-run values)",
          file=sys.stderr)
    print(
        f"{'mode':<34} {'runs':>4} {'ft_p50':>8} {'ft_p95':>8} {'ft_p99':>8} "
        f"{'pipe_p50':>9} {'pipe_p95':>9} {'jit%':>6} {'qfull':>6}",
        file=sys.stderr,
    )
    modes: dict[str, list[RunStats]] = {}
    for run in runs:
        modes.setdefault(run.mode, []).append(run)

    for mode, group in sorted(modes.items()):
        def med(select) -> float:
            values = [v for v in (select(r) for r in group) if v == v]  # drop NaN
            return statistics.median(values) if values else float("nan")

        print(
            f"{mode:<34} {len(group):>4} "
            f"{med(lambda r: percentile(r.frame_times_ms, 0.50)):>8.3f} "
            f"{med(lambda r: percentile(r.frame_times_ms, 0.95)):>8.3f} "
            f"{med(lambda r: percentile(r.frame_times_ms, 0.99)):>8.3f} "
            f"{med(lambda r: percentile(r.input_to_present_ms, 0.50)):>9.3f} "
            f"{med(lambda r: percentile(r.input_to_present_ms, 0.95)):>9.3f} "
            f"{100.0 * med(lambda r: r.target_active_ratio):>5.1f}% "
            f"{max(r.queue_full for r in group):>6}",
            file=sys.stderr,
        )

    if len(runs) < 3:
        print(
            "\nnote: fewer than 3 runs loaded. A single run is not a result; "
            "the runbook requires at least 3 per mode in randomized order.",
            file=sys.stderr,
        )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
