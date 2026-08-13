#!/usr/bin/env python3
"""Small CLI regression tests for invalid latency-run output hygiene."""

from __future__ import annotations

import csv
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
AGGREGATOR = ROOT / "tools" / "perf" / "aggregate-vulkan-latency.py"


def write_capture(path: Path, measured_generation: int) -> None:
    rows = [
        {
            "run_id": "synthetic",
            "present_end_time_us": "100",
            "swapchain_generation": "1",
            "target_scheduling": "0",
            "target_mode": "0",
            "target_value_ns": "0",
        },
        {
            "run_id": "synthetic",
            "present_end_time_us": "200",
            "swapchain_generation": str(measured_generation),
            "target_scheduling": "0",
            "target_mode": "0",
            "target_value_ns": "0",
        },
        {
            "run_id": "synthetic",
            "present_end_time_us": "300",
            "swapchain_generation": str(measured_generation),
            "target_scheduling": "0",
            "target_mode": "0",
            "target_value_ns": "0",
        },
    ]
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def run(capture: Path, summary: Path) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            sys.executable,
            str(AGGREGATOR),
            "--warmup",
            "1",
            "--out",
            str(summary),
            str(capture),
        ],
        cwd=ROOT,
        text=True,
        capture_output=True,
        check=False,
    )


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="melonprime-latency-test-") as raw_dir:
        directory = Path(raw_dir)
        invalid_capture = directory / "generation-boundary.csv"
        invalid_summary = directory / "invalid-summary.csv"
        write_capture(invalid_capture, measured_generation=2)
        invalid = run(invalid_capture, invalid_summary)

        if invalid.returncode != 1:
            raise AssertionError(f"invalid run exit={invalid.returncode}\n{invalid.stderr}")
        if invalid.stdout:
            raise AssertionError("invalid run emitted a normal summary on stdout")
        if invalid_summary.exists():
            raise AssertionError("invalid run created summary.csv")
        if "# per-mode" in invalid.stderr:
            raise AssertionError("invalid run emitted per-mode output")
        if "warm-up baseline invalid" not in invalid.stderr:
            raise AssertionError("warm-up generation diagnostic missing")

        valid_capture = directory / "same-generation.csv"
        valid_summary = directory / "valid-summary.csv"
        write_capture(valid_capture, measured_generation=1)
        valid = run(valid_capture, valid_summary)
        if valid.returncode != 0:
            raise AssertionError(f"valid run exit={valid.returncode}\n{valid.stderr}")
        if not valid.stdout.startswith("run_id,"):
            raise AssertionError("valid run did not emit the normal summary")
        if not valid_summary.exists():
            raise AssertionError("valid run did not create summary.csv")

    print("aggregate Vulkan latency tests PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
