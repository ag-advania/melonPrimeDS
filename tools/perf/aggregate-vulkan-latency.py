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
TIMING_BACKEND_NAMES = {0: "none", 1: "EXT", 2: "GOOGLE"}


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
        self.timing_backend = 0
        self.last_google_desired_time = 0
        self.present_margins_ms: list[float] = []
        self.target_active = 0
        self.problems: list[str] = []
        self.bounded_wait = 0
        self.bounded_wait_attempted = 0
        self.wait_timeouts = 0
        self.queue_full = 0
        self.queue_recoveries = 0
        self.queue_size = 0
        self.wait_timeouts_at_warmup = 0
        self.warmup_generation: int | None = None
        self.swapchain_generation: int | None = None
        self.swapchain_recreations_in_window = 0
        self._load(warmup)

    def _load(self, warmup: int) -> None:
        with self.path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))

        if rows and "swapchain_generation" not in rows[0]:
            self.problems.append(
                f"{self.path.name}: missing required swapchain_generation column"
            )

        previous_present_end: int | None = None
        for index, row in enumerate(rows):
            generation = self._read_swapchain_generation(row, index)
            if index < warmup:
                self.dropped_warmup += 1
                previous_present_end = as_int(row, "present_end_time_us")
                self.warmup_generation = generation
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

            # These lifecycle counters are cumulative only within a swapchain.
            # A recreation resets them to zero, so subtracting the warm-up value
            # from the final row would silently undercount an A/B window that
            # crossed that boundary. Reject the run and collect it again without
            # a recreation instead of presenting a plausible but false total.
            if generation is not None:
                if self.swapchain_generation is None:
                    self.swapchain_generation = generation
                    if (
                        self.warmup_generation is not None
                        and generation != self.warmup_generation
                    ):
                        self.problems.append(
                            f"{self.path.name}:{index}: swapchain_generation changed "
                            f"at the warm-up boundary from {self.warmup_generation} "
                            f"to {generation}; warm-up baseline invalid"
                        )
                elif generation != self.swapchain_generation:
                    self.swapchain_recreations_in_window += 1
                    self.problems.append(
                        f"{self.path.name}:{index}: swapchain_generation changed "
                        f"from {self.swapchain_generation} to {generation} "
                        "inside the measured window; lifecycle counters reset"
                    )
                    self.swapchain_generation = generation

            # Only a row that actually applied a target may name the run's mode.
            # A row claiming a mode without having applied one is a producer
            # bug, not a data point, so it is counted as a problem instead.
            row_mode = as_int(row, "target_mode")
            row_backend = as_int(row, "target_backend")
            if row_backend != 0:
                self.timing_backend = row_backend
            if applied and row_mode != 0:
                self.target_mode = row_mode
            self._check_row(row, index, applied, row_mode, row_backend)

            margin_ns = as_int(row, "feedback_present_margin_ns")
            if row_backend == 2 and margin_ns > 0:
                self.present_margins_ms.append(margin_ns / 1_000_000.0)

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

    def _read_swapchain_generation(
        self, row: dict[str, str], index: int
    ) -> int | None:
        if "swapchain_generation" not in row:
            return None
        raw = row.get("swapchain_generation")
        if raw is None or not raw.strip():
            self.problems.append(
                f"{self.path.name}:{index}: missing swapchain_generation value"
            )
            return None
        try:
            generation = int(raw)
        except ValueError:
            self.problems.append(
                f"{self.path.name}:{index}: invalid swapchain_generation={raw!r}"
            )
            return None
        if generation <= 0:
            self.problems.append(
                f"{self.path.name}:{index}: swapchain_generation={generation} is not positive"
            )
        return generation

    def _check_row(
        self, row: dict[str, str], index: int, applied: int, mode: int, backend: int
    ) -> None:
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
        if applied and backend == 0:
            flag("target_scheduling=1 with target_backend=none")
        if not applied and value != 0:
            flag(f"target_value_ns={value} on a row that applied no target")

        if backend == 2:
            if mode not in (0, 1):
                flag(f"GOOGLE backend cannot use target_mode={mode}")
            if as_int(row, "feedback_stage_time_ns") != 0:
                flag("GOOGLE backend synthesized an EXT feedback_stage_time_ns")
            desired = as_int(row, "target_value_ns") if applied else 0
            if desired != 0:
                if self.last_google_desired_time and desired < self.last_google_desired_time:
                    flag("GOOGLE desiredPresentTime moved backwards")
                self.last_google_desired_time = desired

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
        backend = TIMING_BACKEND_NAMES.get(
            self.timing_backend, f"backend{self.timing_backend}"
        )
        return f"{policy}/Reflex{reflex}/{backend}/{target}"

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
            "target_backend": TIMING_BACKEND_NAMES.get(
                self.timing_backend, self.timing_backend
            ),
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
            "present_margin_p50_ms": round(
                percentile(self.present_margins_ms, 0.50), 4
            ),
            "present_margin_p95_ms": round(
                percentile(self.present_margins_ms, 0.95), 4
            ),
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
            "swapchain_generation": self.swapchain_generation or 0,
            "swapchain_recreations_in_window": self.swapchain_recreations_in_window,
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


def remove_stale_output(path: Path, inputs: list[Path]) -> bool:
    """Remove a previous summary before this invocation can be mistaken for it."""
    resolved = path.resolve()
    if any(input_path.resolve() == resolved for input_path in inputs):
        print(
            f"--out must not overwrite an input capture: {path}",
            file=sys.stderr,
        )
        return False

    existed = path.exists()
    try:
        path.unlink(missing_ok=True)
    except OSError as exc:
        print(f"could not remove stale output {path}: {exc}", file=sys.stderr)
        return False
    if existed:
        print(f"removed stale summary output: {path}", file=sys.stderr)
    return True


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
    out_path = Path(args.out) if args.out else None
    if out_path is not None and not remove_stale_output(out_path, files):
        return 2
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

    # A run whose capture contradicts itself is not a measurement. Decide this
    # before producing any normal summary or per-mode output: a consumer must
    # never mistake a file left behind by an INVALID run for an approved A/B
    # result. Diagnostics stay on stderr so the stdout CSV remains empty.
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
            "These rows contradict the capture contract. Treat the affected "
            "runs as INVALID rather than comparing them.",
            file=sys.stderr,
        )
        return 1

    rows = [run.summary_row() for run in runs]
    fieldnames = list(rows[0].keys())

    writer = csv.DictWriter(sys.stdout, fieldnames=fieldnames)
    writer.writeheader()
    writer.writerows(rows)

    if out_path is not None:
        with out_path.open("w", newline="", encoding="utf-8") as handle:
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
