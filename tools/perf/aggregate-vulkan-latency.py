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
# target_value_ns means different things per mode: an absolute presentation
# timestamp, or a minimum previous-image visible duration. Mixing the two in one
# statistic would be meaningless, so the mode is carried into the run label.
TARGET_MODE_NAMES = {0: "none", 1: "absolute", 2: "relative"}


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
        self.target_mode = 0
        self.target_active = 0
        self.problems: list[str] = []
        self.bounded_wait = 0
        self.bounded_wait_attempted = 0
        self.wait_timeouts = 0
        self.queue_full = 0
        self.queue_recoveries = 0
        self.queue_size = 0
        self.wait_timeouts_at_warmup = 0
        self._load(warmup)

    def _load(self, warmup: int) -> None:
        with self.path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))

        previous_present_end: int | None = None
        for index, row in enumerate(rows):
            if index < warmup:
                self.dropped_warmup += 1
                previous_present_end = as_int(row, "present_end_time_us")
                # wait_timeout_count is a running total for the whole run, so
                # the value standing at the warmup boundary has to be subtracted
                # out. Using the final value alone would charge warm-up timeouts
                # to the measured window.
                self.wait_timeouts_at_warmup = as_int(row, "wait_timeout_count")
                continue

            self.samples += 1
            self.run_id = row.get("run_id") or self.run_id
            self.policy = as_int(row, "policy", self.policy)
            self.authority = as_int(row, "authority", self.authority)
            self.reflex_mode = as_int(row, "reflex_mode", self.reflex_mode)
            applied = as_int(row, "target_scheduling")
            self.target_active += applied
            self.bounded_wait += as_int(row, "bounded_wait")
            self.bounded_wait_attempted += as_int(row, "bounded_wait_attempted")

            # Only a row that actually applied a target may name the run's mode.
            # A row claiming a mode without having applied one is a producer
            # bug, not a data point, so it is counted as a problem instead.
            row_mode = as_int(row, "target_mode")
            if applied and row_mode != 0:
                self.target_mode = row_mode
            self._check_row(row, index, applied, row_mode)

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

    def _check_row(self, row: dict[str, str], index: int, applied: int, mode: int) -> None:
        """Validate one row against what the capture is supposed to guarantee.

        A measurement tool has to be truthful when things go wrong, not only
        when they go right. These are cheap checks that catch a producer whose
        columns have drifted apart -- for example recording the resolver's
        permission in `target_scheduling` while `target_mode` still holds a
        stale value from an earlier present.
        """
        def flag(message: str) -> None:
            self.problems.append(f"{self.path.name}:{index}: {message}")

        if not applied and mode != 0:
            flag(f"target_mode={mode} on a row that applied no target")
        value = as_int(row, "target_value_ns")
        if applied and value == 0:
            flag("target_scheduling=1 with target_value_ns=0")
        if not applied and value != 0:
            flag(f"target_value_ns={value} on a row that applied no target")

        # Relative rows carry the inputs their duration was computed from, so
        # the cadence can be re-derived here rather than trusted.
        if not (applied and mode == 2):
            return
        quanta = as_int(row, "relative_quanta")
        interval = as_int(row, "target_generation_refresh_interval_ns")
        after = as_int(row, "relative_accumulator_after_ns")
        if quanta and interval:
            expected = quanta * interval
            if expected != value:
                flag(
                    f"target_value_ns={value} != relative_quanta({quanta})"
                    f" * refresh_interval({interval}) = {expected}"
                )
            if after >= interval:
                flag(
                    f"relative_accumulator_after_ns={after} >= refresh interval"
                    f" {interval}; the carried fraction must stay below one refresh"
                )

    @property
    def mode(self) -> str:
        policy = POLICY_NAMES.get(self.policy, f"policy{self.policy}")
        reflex = REFLEX_NAMES.get(self.reflex_mode, f"reflex{self.reflex_mode}")
        target = TARGET_MODE_NAMES.get(self.target_mode, f"mode{self.target_mode}")
        return f"{policy}/Reflex{reflex}/{target}"

    @property
    def target_active_ratio(self) -> float:
        return (self.target_active / self.samples) if self.samples else float("nan")

    @property
    def wait_timeouts_in_window(self) -> int:
        """Timeouts inside the measured window, excluding warm-up."""
        return max(0, self.wait_timeouts - self.wait_timeouts_at_warmup)

    @property
    def wait_timeout_rate(self) -> float:
        """Timeouts per attempted wait.

        The runbook threshold is a rate over waits that actually ran, not over
        frames: a frame with nothing to wait on never had the chance to time
        out, and including it would understate the rate.
        """
        if not self.bounded_wait_attempted:
            return float("nan")
        return self.wait_timeouts_in_window / self.bounded_wait_attempted

    def summary_row(self) -> dict[str, object]:
        return {
            "run_id": self.run_id,
            "file": self.path.name,
            "mode": self.mode,
            "policy": POLICY_NAMES.get(self.policy, self.policy),
            "authority": AUTHORITY_NAMES.get(self.authority, self.authority),
            "reflex_mode": REFLEX_NAMES.get(self.reflex_mode, self.reflex_mode),
            "target_mode": TARGET_MODE_NAMES.get(self.target_mode, self.target_mode),
            "invalid_rows": len(self.problems),
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
            # Allowed vs actually called. The gap is normal -- a frame with
            # nothing to wait on is permitted but does not wait -- so reporting
            # only the permission would overstate how often the wait ran.
            "bounded_wait_allowed_ratio": round(
                (self.bounded_wait / self.samples) if self.samples else float("nan"), 4
            ),
            "bounded_wait_attempted_ratio": round(
                (self.bounded_wait_attempted / self.samples)
                if self.samples
                else float("nan"),
                4,
            ),
            "wait_timeout_count": self.wait_timeouts,
            "wait_timeouts_in_window": self.wait_timeouts_in_window,
            # The runbook's "< 1%" threshold, computed the way the runbook
            # defines it: timeouts per wait that actually ran.
            "wait_timeout_rate": round(self.wait_timeout_rate, 6),
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

    # A run whose capture contradicts itself is not a measurement. Report it
    # loudly and fail, rather than quietly averaging numbers that cannot be
    # trusted: the whole point of the target columns is deciding whether a
    # policy actually scheduled.
    problems = [problem for run in runs for problem in run.problems]
    if problems:
        print(
            f"\nINVALID: {len(problems)} contradictory rows across "
            f"{sum(1 for run in runs if run.problems)} run(s)",
            file=sys.stderr,
        )
        for problem in problems[:20]:
            print(f"  {problem}", file=sys.stderr)
        if len(problems) > 20:
            print(f"  ... and {len(problems) - 20} more", file=sys.stderr)
        print(
            "These rows disagree about whether a target was applied. Treat the "
            "affected runs as INVALID rather than comparing them.",
            file=sys.stderr,
        )
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
