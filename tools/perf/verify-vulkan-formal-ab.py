#!/usr/bin/env python3
"""Verify the fixed-condition Vulkan Formal Phase 3 evidence set."""

from __future__ import annotations

import argparse
import csv
import re
import sys
from pathlib import Path


EXPECTED = {
    "A0": (0, 0, 2),
    "A1": (1, 0, 2),
    "A2": (2, 0, 2),
    "A3": (3, 0, 1000361000),
    "B1": (2, 1, 2),
    "B2": (2, 2, 2),
    "C0": (2, 0, 0),
}
RUN_RE = re.compile(r"^20260813_NV_(A0|A1|A2|A3|B1|B2|C0)_R[012]$")
BAD_LOG = re.compile(r"VUID-|SYNC-HAZARD|DEVICE_LOST")


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", type=Path, help="formal-ab evidence directory")
    parser.add_argument("--warmup", type=int, default=600)
    parser.add_argument("--minimum-rows", type=int, default=10600)
    args = parser.parse_args()

    root = args.root
    runs = root / "runs" / "formal"
    errors: list[str] = []
    paths = sorted(runs.glob("20260813_NV_*.csv"))
    if len(paths) != 21:
        fail(errors, f"expected 21 CSV files, found {len(paths)}")

    counts = {mode: 0 for mode in EXPECTED}
    rows_by_run: dict[str, list[dict[str, str]]] = {}
    for path in paths:
        match = RUN_RE.fullmatch(path.stem)
        if not match:
            fail(errors, f"unexpected CSV name: {path.name}")
            continue
        mode = match.group(1)
        counts[mode] += 1
        with path.open(newline="", encoding="utf-8") as handle:
            rows = list(csv.DictReader(handle))
        run_id = path.stem
        rows_by_run[run_id] = rows
        if len(rows) < args.minimum_rows:
            fail(errors, f"{run_id}: rows={len(rows)} < {args.minimum_rows}")
        if {row.get("run_id") for row in rows} != {run_id}:
            fail(errors, f"{run_id}: run_id column mismatch")
        policy, reflex, present_mode = EXPECTED[mode]
        if {int(row["policy"]) for row in rows} != {policy}:
            fail(errors, f"{run_id}: policy mismatch")
        if {int(row["reflex_mode"]) for row in rows} != {reflex}:
            fail(errors, f"{run_id}: reflex mismatch")
        if {int(row["present_mode"]) for row in rows} != {present_mode}:
            fail(errors, f"{run_id}: present mode mismatch")

        measured = rows[args.warmup :]
        if not measured:
            fail(errors, f"{run_id}: no measured rows")
            continue
        generations = {row.get("swapchain_generation") for row in measured}
        if len(generations) != 1:
            fail(errors, f"{run_id}: measured generations={sorted(generations)}")
        if mode in {"A2", "A3"}:
            active_ratio = sum(int(row["target_scheduling"]) for row in measured) / len(measured)
            if active_ratio < 0.95:
                fail(errors, f"{run_id}: target active ratio={active_ratio:.6f}")

        for suffix in (".metadata.txt", ".out.log", ".err.log"):
            sibling = runs / f"{run_id}{suffix}"
            if not sibling.is_file():
                fail(errors, f"{run_id}: missing {suffix}")
        metadata = (runs / f"{run_id}.metadata.txt").read_text(encoding="utf-8")
        for required in ("process_exit_code=0", "config_restore=PASS", "layer_settings_restore=PASS"):
            if required not in metadata:
                fail(errors, f"{run_id}: missing metadata {required}")
        output = (runs / f"{run_id}.out.log").read_text(encoding="utf-8", errors="replace")
        output += (runs / f"{run_id}.err.log").read_text(encoding="utf-8", errors="replace")
        if BAD_LOG.search(output):
            fail(errors, f"{run_id}: validation/device-loss marker in log")

    for mode, count in counts.items():
        if count != 3:
            fail(errors, f"{mode}: expected 3 runs, found {count}")

    summary_path = root / "summary.csv"
    if not summary_path.is_file():
        fail(errors, "summary.csv is missing")
    else:
        with summary_path.open(newline="", encoding="utf-8") as handle:
            summary = list(csv.DictReader(handle))
        if len(summary) != 21:
            fail(errors, f"summary rows={len(summary)}")
        for row in summary:
            run_id = row["run_id"]
            if int(row["invalid_rows"]) != 0:
                fail(errors, f"{run_id}: invalid_rows={row['invalid_rows']}")
            if int(row["swapchain_recreations_in_window"]) != 0:
                fail(errors, f"{run_id}: measured generation recreation")
            if int(row["timing_queue_full_count"]) != 0 or int(row["timing_queue_recovery_count"]) != 0:
                fail(errors, f"{run_id}: timing queue pressure counters are non-zero")
            timeout = row["wait_timeout_rate"]
            if timeout.lower() != "nan" and float(timeout) >= 0.01:
                fail(errors, f"{run_id}: wait_timeout_rate={timeout}")

    if errors:
        print("FORMAL A/B VERIFICATION FAIL")
        for error in errors:
            print(f"- {error}")
        return 1

    print("FORMAL A/B VERIFICATION PASS")
    print(f"csv_files={len(paths)}")
    print("runs_per_mode=3")
    print("minimum_rows=10600")
    print("invalid_rows=0")
    print("measured_generation_changes=0")
    print("a2_a3_target_active_at_least=95%")
    print("queue_full_and_recovery=0")
    print("wait_timeout_rate_lt=1%")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
