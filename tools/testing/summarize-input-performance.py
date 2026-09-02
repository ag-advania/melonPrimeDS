#!/usr/bin/env python3
"""Summarize developer input telemetry and optionally enforce its budgets.

The application emits one generic input-metric line per performance report and
one Windows Raw Input block when ``MELONPRIME_RAW_INPUT_PERF=1``.  This parser
keeps the report text as the source of truth: it does not infer hardware or
runtime behavior from a source audit, and it refuses to claim a budget when a
required metric is absent.

Examples:
  python tools/testing/summarize-input-performance.py run.stderr
  python tools/testing/summarize-input-performance.py --mode controller \
      --min-input-samples 1000 --json-out input-summary.json run.stderr
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import sys
from typing import Any


INPUT_RE = re.compile(
    r"^\[MelonPrimePerf\] input_metric_us "
    r"(?P<body>.*)$"
)
INPUT_INSTANCE_RE = re.compile(r"(?:^|\s)instance_id=(?P<id>\d+)(?:\s|$)")
INPUT_METRIC_RE = re.compile(
    r"(?P<name>[a-z0-9_]+)\[c=(?P<c>\d+) "
    r"p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+)\]"
)
RAW_STAGE_RE = re.compile(
    r"^\[MelonPrimeRawPerf\] stage_us "
    r"snapshot\[p50=(?P<snapshot_p50>[0-9.]+) "
    r"p95=(?P<snapshot_p95>[0-9.]+) p99=(?P<snapshot_p99>[0-9.]+) "
    r"max=(?P<snapshot_max>[0-9.]+)\] "
    r"late_latch\[p50=(?P<late_p50>[0-9.]+) "
    r"p95=(?P<late_p95>[0-9.]+) p99=(?P<late_p99>[0-9.]+) "
    r"max=(?P<late_max>[0-9.]+)\] "
    r"deferred_drain\[p50=(?P<deferred_p50>[0-9.]+) "
    r"p95=(?P<deferred_p95>[0-9.]+) p99=(?P<deferred_p99>[0-9.]+) "
    r"max=(?P<deferred_max>[0-9.]+)\] "
    r"lock_wait_ns snapshot=(?P<snapshot_wait>\d+) "
    r"late=(?P<late_wait>\d+) deferred=(?P<deferred_wait>\d+) "
    r"hidden=(?P<hidden_wait>\d+) native=(?P<native_wait>\d+) \| "
    r"raw_batch calls=(?P<batch_calls>\d+) "
    r"nonempty=(?P<batch_nonempty>\d+) empty=(?P<batch_empty>\d+) "
    r"events=(?P<batch_events>\d+) "
    r"late_delta_claims=(?P<late_claims>\d+) "
    r"post_draw_events=(?P<post_draw>\d+)$"
)
RAW_LOCK_RE = re.compile(
    r"^\[MelonPrimeRawPerf\] lock_planes "
    r"(?P<body>.*)$"
)
KEY_VALUE_RE = re.compile(r"(?P<name>[a-z0-9_]+)=(?P<value>\d+)")

INPUT_METRICS = (
    "input_total",
    "joystick_lock_wait",
    "joystick_sample",
    "joystick_project",
    "joystick_sdl_update",
    "joystick_process_mutex_wait",
    "joystick_process_mutex_hold",
)


class SummaryError(RuntimeError):
    """Raised when a telemetry artifact cannot support a safe summary."""


def parse_float(value: str) -> float:
    try:
        parsed = float(value)
    except ValueError as error:
        raise SummaryError(f"invalid telemetry number: {value!r}") from error
    if not math.isfinite(parsed) or parsed < 0.0:
        raise SummaryError(f"telemetry number is not a finite non-negative value: {value!r}")
    return parsed


def parse_input_body(body: str) -> dict[str, dict[str, float | int]]:
    result: dict[str, dict[str, float | int]] = {}
    for match in INPUT_METRIC_RE.finditer(body):
        result[match.group("name")] = {
            "calls": int(match.group("c")),
            "p50_us": parse_float(match.group("p50")),
            "p95_us": parse_float(match.group("p95")),
            "p99_us": parse_float(match.group("p99")),
            "max_us": parse_float(match.group("max")),
        }
    return result


def parse_input_instance_id(body: str) -> int | None:
    match = INPUT_INSTANCE_RE.search(body)
    return int(match.group("id")) if match else None


def parse_raw_stage(match: re.Match[str]) -> dict[str, Any]:
    values: dict[str, Any] = {}
    for key, value in match.groupdict().items():
        if key.endswith(("_p50", "_p95", "_p99", "_max")):
            values[key] = parse_float(value)
        else:
            values[key] = int(value)
    return values


def parse_log(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise SummaryError(f"telemetry log does not exist: {path}")

    input_reports: list[dict[str, dict[str, float | int]]] = []
    latest_input_by_instance: dict[str, dict[str, dict[str, float | int]]] = {}
    raw_reports: list[dict[str, Any]] = []
    lock_reports: list[dict[str, int]] = []
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        input_match = INPUT_RE.match(line)
        if input_match:
            parsed = parse_input_body(input_match.group("body"))
            if parsed:
                input_reports.append(parsed)
                instance_id = parse_input_instance_id(input_match.group("body"))
                instance_key = str(instance_id) if instance_id is not None else "unbound"
                latest_input_by_instance[instance_key] = parsed
            continue
        raw_match = RAW_STAGE_RE.match(line)
        if raw_match:
            raw_reports.append(parse_raw_stage(raw_match))
            continue
        lock_match = RAW_LOCK_RE.match(line)
        if lock_match:
            lock_reports.append({
                key: int(value)
                for key, value in KEY_VALUE_RE.findall(lock_match.group("body"))
            })

    if not input_reports and not raw_reports:
        raise SummaryError(
            f"{path} contains neither [MelonPrimePerf] input_metric_us nor "
            "[MelonPrimeRawPerf] stage_us telemetry"
        )

    latest_input = input_reports[-1] if input_reports else {}
    latest_raw = raw_reports[-1] if raw_reports else None
    latest_locks = lock_reports[-1] if lock_reports else None
    return {
        "path": str(path),
        "input_report_count": len(input_reports),
        "input_instance_ids": [
            int(key) for key in sorted(
                (key for key in latest_input_by_instance if key != "unbound"),
                key=int,
            )
        ],
        "latest_input_by_instance": latest_input_by_instance,
        "raw_report_count": len(raw_reports),
        "latest_input": latest_input,
        "latest_raw": latest_raw,
        "latest_lock_planes": latest_locks,
    }


def require_input_metric(
    summary: dict[str, Any], name: str, minimum_calls: int
) -> dict[str, float | int]:
    metric = summary["latest_input"].get(name)
    if metric is None:
        raise SummaryError(f"latest input report is missing metric {name}")
    if int(metric["calls"]) < minimum_calls:
        raise SummaryError(
            f"{name} has {metric['calls']} calls; expected at least {minimum_calls}"
        )
    return metric


def enforce(summary: dict[str, Any], mode: str, minimum_calls: int) -> None:
    reports_by_instance = summary.get("latest_input_by_instance") or {
        "unbound": summary["latest_input"]
    }
    for instance_id, input_report in reports_by_instance.items():
        input_summary = {"latest_input": input_report}
        input_total = require_input_metric(input_summary, "input_total", minimum_calls)
        suffix = "" if len(reports_by_instance) == 1 else f" for instance {instance_id}"
        if float(input_total["p99_us"]) >= 100.0:
            raise SummaryError(
                f"input_total p99 exceeds 100 us{suffix}: {input_total['p99_us']}"
            )
        if float(input_total["max_us"]) >= 250.0:
            raise SummaryError(
                f"input_total max exceeds 250 us{suffix}: {input_total['max_us']}"
            )

        median_budget = {"keyboard": 10.0, "controller": 30.0}.get(mode)
        if median_budget is not None and float(input_total["p50_us"]) >= median_budget:
            raise SummaryError(
                f"{mode} input_total p50 exceeds {median_budget:g} us{suffix}: "
                f"{input_total['p50_us']}"
            )

    if mode == "raw":
        raw = summary.get("latest_raw")
        if raw is None:
            raise SummaryError("raw mode requires [MelonPrimeRawPerf] stage telemetry")
        if float(raw["snapshot_p50"]) + float(raw["late_p50"]) >= 40.0:
            raise SummaryError(
                "Raw snapshot+late-latch p50 exceeds 40 us: "
                f"{float(raw['snapshot_p50']) + float(raw['late_p50']):.1f}"
            )


def markdown(summary: dict[str, Any]) -> str:
    lines = [
        "# Input performance summary",
        "",
        f"Source: `{summary['path']}`",
    ]
    reports_by_instance = summary.get("latest_input_by_instance") or {
        "unbound": summary["latest_input"]
    }
    instance_keys = sorted(
        reports_by_instance,
        key=lambda key: (key == "unbound", int(key) if key != "unbound" else 0),
    )
    for instance_key in instance_keys:
        lines.extend([
            "",
            f"## Input instance `{instance_key}`",
            "",
            "| Metric | Calls | p50 (us) | p95 (us) | p99 (us) | Max (us) |",
            "|---|---:|---:|---:|---:|---:|",
        ])
        for name in INPUT_METRICS:
            metric = reports_by_instance[instance_key].get(name)
            if metric is None:
                continue
            lines.append(
                f"| `{name}` | {metric['calls']} | {metric['p50_us']:.1f} | "
                f"{metric['p95_us']:.1f} | {metric['p99_us']:.1f} | "
                f"{metric['max_us']:.1f} |"
            )
    raw = summary.get("latest_raw")
    if raw is not None:
        lines.extend([
            "",
            "## Windows Raw Input",
            "",
            f"snapshot p50/p95/p99/max: {raw['snapshot_p50']:.1f}/"
            f"{raw['snapshot_p95']:.1f}/{raw['snapshot_p99']:.1f}/"
            f"{raw['snapshot_max']:.1f} us",
            f"late latch p50/p95/p99/max: {raw['late_p50']:.1f}/"
            f"{raw['late_p95']:.1f}/{raw['late_p99']:.1f}/"
            f"{raw['late_max']:.1f} us",
            f"batch calls/nonempty/empty/events: {raw['batch_calls']}/"
            f"{raw['batch_nonempty']}/{raw['batch_empty']}/{raw['batch_events']}",
        ])
    locks = summary.get("latest_lock_planes")
    if locks is not None:
        lines.extend([
            "",
            "Raw lock planes (report-window totals):",
            "",
            f"subscription acquisitions/wait/hold: "
            f"{locks.get('subscription_mutex_acq', 0)}/"
            f"{locks.get('subscription_mutex_wait_ns', 0)}/"
            f"{locks.get('subscription_mutex_hold_ns', 0)} ns",
            f"frame acquisitions/wait/hold: {locks.get('frame_mutex_acq', 0)}/"
            f"{locks.get('frame_mutex_wait_ns', 0)}/"
            f"{locks.get('frame_mutex_hold_ns', 0)} ns",
        ])
    return "\n".join(lines) + "\n"


def self_test() -> None:
    import tempfile

    text = "\n".join([
        "[MelonPrimePerf] input_metric_us instance_id=0 input_total[c=1001 p50=4.0 p95=8.0 p99=12.0 max=20.0] joystick_lock_wait[c=1001 p50=0.1 p95=0.2 p99=0.4 max=1.0] joystick_sample[c=1001 p50=5.0 p95=7.0 p99=9.0 max=15.0] joystick_project[c=1001 p50=1.0 p95=2.0 p99=3.0 max=5.0] joystick_sdl_update[c=1001 p50=0.5 p95=0.8 p99=1.0 max=2.0] joystick_process_mutex_wait[c=1001 p50=0.0 p95=0.1 p99=0.2 max=0.5] joystick_process_mutex_hold[c=1001 p50=0.2 p95=0.4 p99=0.6 max=1.0]",
        "[MelonPrimeRawPerf] stage_us snapshot[p50=4.0 p95=6.0 p99=8.0 max=12.0] late_latch[p50=3.0 p95=5.0 p99=7.0 max=10.0] deferred_drain[p50=20.0 p95=25.0 p99=30.0 max=40.0] lock_wait_ns snapshot=10 late=20 deferred=30 hidden=40 native=50 | raw_batch calls=1001 nonempty=10 empty=991 events=20 late_delta_claims=5 post_draw_events=10",
        "[MelonPrimeRawPerf] lock_planes subscription_mutex_acq=2 subscription_mutex_wait_ns=3 subscription_mutex_hold_ns=4 subscription_mutex_max_wait_ns=5 frame_mutex_acq=1001 frame_mutex_wait_ns=6 frame_mutex_hold_ns=7 frame_mutex_max_wait_ns=8",
    ]) + "\n"
    with tempfile.TemporaryDirectory(prefix="melonprime-input-perf-") as directory:
        path = Path(directory) / "telemetry.log"
        path.write_text(text, encoding="utf-8")
        summary = parse_log(path)
        enforce(summary, "raw", 1000)
        if summary["input_instance_ids"] != [0]:
            raise SummaryError("self-test did not preserve input instance identity")
        if summary["latest_input_by_instance"]["0"]["input_total"]["calls"] != 1001:
            raise SummaryError("self-test did not group input metrics by instance")
        if "## Input instance `0`" not in markdown(summary):
            raise SummaryError("self-test did not render input instance identity")
        if summary["latest_raw"]["batch_empty"] != 991:
            raise SummaryError("self-test did not parse raw batch counts")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="*", type=Path)
    parser.add_argument("--mode", choices=("all", "keyboard", "controller", "raw"), default="all")
    parser.add_argument("--min-input-samples", type=int, default=0)
    parser.add_argument("--check-budget", action="store_true")
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    try:
        if args.self_test:
            self_test()
            print("input performance summarizer self-test: PASS")
            return 0
        if not args.logs:
            parser.error("at least one telemetry log is required unless --self-test is used")
        if args.min_input_samples < 0:
            raise SummaryError("--min-input-samples must be non-negative")
        summaries = [parse_log(path) for path in args.logs]
        if args.check_budget:
            for summary in summaries:
                enforce(summary, args.mode, args.min_input_samples)
        output: dict[str, Any] = {
            "schema_version": 2,
            "mode": args.mode,
            "minimum_input_samples": args.min_input_samples,
            "budget_checked": args.check_budget,
            "runs": summaries,
        }
        rendered_json = json.dumps(output, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            args.json_out.write_text(rendered_json, encoding="utf-8")
        if args.markdown_out:
            if len(summaries) != 1:
                raise SummaryError("--markdown-out requires exactly one telemetry log")
            args.markdown_out.write_text(markdown(summaries[0]), encoding="utf-8")
        if not args.json_out and not args.markdown_out:
            print(rendered_json, end="")
        return 0
    except SummaryError as error:
        print(f"input performance summary failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
