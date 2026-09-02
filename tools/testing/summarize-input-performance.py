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
      --min-input-samples 1000 --check-budget \
      --json-out input-summary.json run.stderr
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
    r"(?P<name>[a-z0-9_]+)\[c=(?P<c>\d+)"
    r"(?: retained=(?P<retained>\d+))? "
    r"p50=(?P<p50>[0-9.]+) p95=(?P<p95>[0-9.]+) "
    r"p99=(?P<p99>[0-9.]+) max=(?P<max>[0-9.]+)"
    r"(?: retained_max=(?P<retained_max>[0-9.]+))?\]"
)
EXPLICIT_LATENCY_RE = re.compile(
    r"^\[MelonPrimePerf\] explicit_latency_us "
    r"(?P<body>.*)$"
)
EXPLICIT_LATENCY_METRIC_RE = re.compile(
    r"(?P<name>[a-z0-9_]+)=calls=(?P<calls>\d+) "
    r"retained=(?P<retained>\d+) p50=(?P<p50>[0-9.]+) "
    r"p95=(?P<p95>[0-9.]+) p99=(?P<p99>[0-9.]+) "
    r"max=(?P<max>[0-9.]+) retained_max=(?P<retained_max>[0-9.]+)"
)
EXPLICIT_LATENCY_LEGACY_METRIC_RE = re.compile(
    r"(?P<name>[a-z0-9_]+)=p50=(?P<p50>[0-9.]+) "
    r"p95=(?P<p95>[0-9.]+) p99=(?P<p99>[0-9.]+) "
    r"max=(?P<max>[0-9.]+) n=(?P<n>\d+)"
)
RAW_STAGE_RE = re.compile(
    r"^\[MelonPrimeRawPerf\] stage_us "
    r"snapshot\[calls=(?P<snapshot_calls>\d+) "
    r"retained=(?P<snapshot_retained>\d+) p50=(?P<snapshot_p50>[0-9.]+) "
    r"p95=(?P<snapshot_p95>[0-9.]+) p99=(?P<snapshot_p99>[0-9.]+) "
    r"max=(?P<snapshot_max>[0-9.]+) "
    r"retained_max=(?P<snapshot_retained_max>[0-9.]+)\] "
    r"late_latch\[calls=(?P<late_calls>\d+) "
    r"retained=(?P<late_retained>\d+) p50=(?P<late_p50>[0-9.]+) "
    r"p95=(?P<late_p95>[0-9.]+) p99=(?P<late_p99>[0-9.]+) "
    r"max=(?P<late_max>[0-9.]+) "
    r"retained_max=(?P<late_retained_max>[0-9.]+)\] "
    r"deferred_drain\[calls=(?P<deferred_calls>\d+) "
    r"retained=(?P<deferred_retained>\d+) p50=(?P<deferred_p50>[0-9.]+) "
    r"p95=(?P<deferred_p95>[0-9.]+) p99=(?P<deferred_p99>[0-9.]+) "
    r"max=(?P<deferred_max>[0-9.]+) "
    r"retained_max=(?P<deferred_retained_max>[0-9.]+)\] "
    r"lock_wait_ns snapshot=(?P<snapshot_wait>\d+) "
    r"late=(?P<late_wait>\d+) deferred=(?P<deferred_wait>\d+) "
    r"hidden=(?P<hidden_wait>\d+) native=(?P<native_wait>\d+) \| "
    r"raw_batch calls=(?P<batch_calls>\d+) "
    r"nonempty=(?P<batch_nonempty>\d+) empty=(?P<batch_empty>\d+) "
    r"events=(?P<batch_events>\d+) "
    r"late_delta_claims=(?P<late_claims>\d+) "
    r"post_draw_events=(?P<post_draw>\d+)$"
)
RAW_STAGE_LEGACY_RE = re.compile(
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
GENERIC_CAPTURE_MODE_RE = re.compile(
    r"^\[MelonPrimePerf\] capture_mode "
    r"instance_id=(?P<instance_id>\d+) capture_only=(?P<capture_only>[01])$"
)
RAW_CAPTURE_MODE_RE = re.compile(
    r"^\[MelonPrimeRawPerf\] capture_mode "
    r"capture_only=(?P<capture_only>[01])$"
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

EXPLICIT_LATENCY_METRICS = (
    "frame_input_sample_to_runframe_begin_us",
    "input_sample_to_present_end_us",
)
INPUT_RETENTION_CAP = 2048
LEGACY_INPUT_RETENTION_CAP = 2048
LEGACY_EXPLICIT_LATENCY_RETENTION_CAP = 512
MIN_CERTIFICATION_SAMPLES = 1000
RETENTION_LATEST_N = "latest_n"
RETENTION_LEGACY_FIRST_N = "legacy_first_n"
RETENTION_LEGACY_UNVERSIONED = "legacy_unversioned"
RAW_LIFETIME_SCOPE = "raw_service_lifetime"

CONTROLLER_EVIDENCE_METRICS = (
    "joystick_lock_wait",
    "joystick_sample",
    "joystick_project",
    "joystick_sdl_update",
    "joystick_process_mutex_wait",
    "joystick_process_mutex_hold",
)
RAW_REQUIRED_STAGES = ("snapshot", "late_latch")
RAW_EVIDENCE_STAGES = ("snapshot", "late_latch", "deferred_drain")


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


def parse_input_body(body: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for match in INPUT_METRIC_RE.finditer(body):
        calls = int(match.group("c"))
        retained = match.group("retained")
        retained_max = match.group("retained_max")
        whole_max_us = parse_float(match.group("max"))
        if retained is not None:
            retention_mode = RETENTION_LATEST_N
            retention_cap = INPUT_RETENTION_CAP
            retained_count = int(retained)
            retained_max_us = (
                parse_float(retained_max) if retained_max is not None else None
            )
            whole_max_known = True
            max_scope = "whole_window"
        else:
            # The pre-3443 generic probe stopped writing after its first
            # capacity samples. Preserve that provenance instead of presenting
            # the prefix as the current latest-N ring.
            retention_mode = RETENTION_LEGACY_FIRST_N
            retention_cap = LEGACY_INPUT_RETENTION_CAP
            retained_count = min(calls, retention_cap)
            # The old generic probe tracked maxTicks across all calls even
            # though its percentile samples stopped at the first cap. Its
            # whole max is therefore known, but retained_max is not.
            retained_max_us = None
            whole_max_known = True
            max_scope = "whole_window"
        if retained_count > calls or retained_count > retention_cap:
            raise SummaryError(
                f"{match.group('name')} retained count is invalid: "
                f"calls={calls}, retained={retained_count}"
            )
        result[match.group("name")] = {
            "calls": calls,
            "retained": retained_count,
            "retention_mode": retention_mode,
            "retention_cap": retention_cap,
            "p50_us": parse_float(match.group("p50")),
            "p95_us": parse_float(match.group("p95")),
            "p99_us": parse_float(match.group("p99")),
            # max_us is the value emitted by the producer. For legacy first-N
            # artifacts with calls above the cap it is not a whole-run max;
            # max_scope/whole_max_known make that limitation explicit.
            "max_us": whole_max_us,
            "whole_max_us": whole_max_us if whole_max_known else None,
            "whole_max_known": whole_max_known,
            "max_scope": max_scope,
            "retained_max_us": retained_max_us,
        }
    return result


def parse_explicit_latency_body(body: str) -> dict[str, dict[str, Any]]:
    result: dict[str, dict[str, Any]] = {}
    for match in EXPLICIT_LATENCY_METRIC_RE.finditer(body):
        calls = int(match.group("calls"))
        retained = int(match.group("retained"))
        if retained > calls or retained > INPUT_RETENTION_CAP:
            raise SummaryError(
                f"{match.group('name')} retained count is invalid: "
                f"calls={calls}, retained={retained}"
            )
        whole_max_us = parse_float(match.group("max"))
        result[match.group("name")] = {
            "calls": calls,
            "retained": retained,
            "retention_mode": RETENTION_LATEST_N,
            "retention_cap": INPUT_RETENTION_CAP,
            "p50_us": parse_float(match.group("p50")),
            "p95_us": parse_float(match.group("p95")),
            "p99_us": parse_float(match.group("p99")),
            "max_us": whole_max_us,
            "whole_max_us": whole_max_us,
            "whole_max_known": True,
            "max_scope": "whole_window",
            "retained_max_us": parse_float(match.group("retained_max")),
        }
    for match in EXPLICIT_LATENCY_LEGACY_METRIC_RE.finditer(body):
        calls = int(match.group("n"))
        retention_cap = LEGACY_EXPLICIT_LATENCY_RETENTION_CAP
        retained = min(calls, retention_cap)
        whole_max_us = parse_float(match.group("max"))
        whole_max_known = calls <= retention_cap
        result[match.group("name")] = {
            "calls": calls,
            "retained": retained,
            "retention_mode": RETENTION_LEGACY_FIRST_N,
            "retention_cap": retention_cap,
            "p50_us": parse_float(match.group("p50")),
            "p95_us": parse_float(match.group("p95")),
            "p99_us": parse_float(match.group("p99")),
            "max_us": whole_max_us,
            "whole_max_us": whole_max_us if whole_max_known else None,
            "whole_max_known": whole_max_known,
            "max_scope": (
                "whole_window" if whole_max_known else "retained_first_n"
            ),
            # The old explicit report's max was calculated over the retained
            # first-N prefix, not over the complete call window.
            "retained_max_us": whole_max_us,
        }
    return result


def parse_input_instance_id(body: str) -> int | None:
    match = INPUT_INSTANCE_RE.search(body)
    return int(match.group("id")) if match else None


def parse_raw_stage(
    match: re.Match[str], retention_mode: str
) -> dict[str, Any]:
    values: dict[str, Any] = {}
    stage_metrics: dict[str, dict[str, Any]] = {}
    stage_value_keys = {
        f"{prefix}_{suffix}"
        for prefix in ("snapshot", "late", "deferred")
        for suffix in (
            "calls", "retained", "p50", "p95", "p99", "max", "retained_max"
        )
    }
    for prefix, stage_name in (
        ("snapshot", "snapshot"),
        ("late", "late_latch"),
        ("deferred", "deferred_drain"),
    ):
        calls_text = match.groupdict().get(f"{prefix}_calls")
        retained_text = match.groupdict().get(f"{prefix}_retained")
        if calls_text is None or retained_text is None:
            calls = None
            retained = None
            retained_max_us = None
        else:
            calls = int(calls_text)
            retained = int(retained_text)
            if retained > calls or retained > INPUT_RETENTION_CAP:
                raise SummaryError(
                    f"Raw {prefix} retained count is invalid: "
                    f"calls={calls}, retained={retained}"
                )
            retained_max_us = parse_float(
                match.group(f"{prefix}_retained_max")
            )
        p50_us = parse_float(match.group(f"{prefix}_p50"))
        p95_us = parse_float(match.group(f"{prefix}_p95"))
        p99_us = parse_float(match.group(f"{prefix}_p99"))
        max_us = parse_float(match.group(f"{prefix}_max"))
        metric = {
            "calls": calls,
            "retained": retained,
            "retention_mode": retention_mode,
            "retention_cap": INPUT_RETENTION_CAP,
            "p50_us": p50_us,
            "p95_us": p95_us,
            "p99_us": p99_us,
            "max_us": max_us,
            "whole_max_us": max_us,
            "whole_max_known": True,
            "max_scope": RAW_LIFETIME_SCOPE,
            "retained_max_us": retained_max_us,
        }
        stage_metrics[stage_name] = metric
        values[f"{prefix}_calls"] = calls
        values[f"{prefix}_retained"] = retained
        values[f"{prefix}_p50"] = p50_us
        values[f"{prefix}_p95"] = p95_us
        values[f"{prefix}_p99"] = p99_us
        values[f"{prefix}_max"] = max_us
        values[f"{prefix}_retained_max"] = retained_max_us
    for key, value in match.groupdict().items():
        if key in stage_value_keys:
            continue
        values[key] = int(value)
    values["stage_metrics"] = stage_metrics
    values["retention_mode"] = retention_mode
    return values


def parse_log(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise SummaryError(f"telemetry log does not exist: {path}")

    input_reports: list[dict[str, dict[str, Any]]] = []
    latest_input_by_instance: dict[str, dict[str, dict[str, Any]]] = {}
    latency_reports: list[dict[str, dict[str, Any]]] = []
    latest_latency_by_instance: dict[str, dict[str, dict[str, Any]]] = {}
    raw_reports: list[dict[str, Any]] = []
    lock_reports: list[dict[str, int]] = []
    generic_capture_only_by_instance: dict[str, bool] = {}
    raw_capture_only: bool | None = None
    pending_generic_capture: dict[str, bool] = {}
    pending_raw_capture: bool | None = None
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        generic_capture_match = GENERIC_CAPTURE_MODE_RE.match(line)
        if generic_capture_match:
            pending_generic_capture[
                generic_capture_match.group("instance_id")
            ] = generic_capture_match.group("capture_only") == "1"
            continue
        raw_capture_match = RAW_CAPTURE_MODE_RE.match(line)
        if raw_capture_match:
            pending_raw_capture = raw_capture_match.group("capture_only") == "1"
            continue
        input_match = INPUT_RE.match(line)
        if input_match:
            parsed = parse_input_body(input_match.group("body"))
            instance_id = parse_input_instance_id(input_match.group("body"))
            instance_key = str(instance_id) if instance_id is not None else "unbound"
            capture_only = pending_generic_capture.pop(instance_key, None)
            if instance_id is None:
                # An unbound/legacy report cannot consume an instance marker.
                # Drop all pending markers rather than allowing one to leak to
                # a later, unrelated report generation.
                pending_generic_capture.clear()
                generic_capture_only_by_instance.clear()
            if parsed:
                input_reports.append(parsed)
                latest_input_by_instance[instance_key] = parsed
                if capture_only is None:
                    # A newer markerless report invalidates any marker attached
                    # to an older report for the same instance.
                    generic_capture_only_by_instance.pop(instance_key, None)
                else:
                    generic_capture_only_by_instance[instance_key] = capture_only
            else:
                # A malformed report still consumes its pending marker and
                # invalidates any provenance retained for that instance.
                generic_capture_only_by_instance.pop(instance_key, None)
            continue
        latency_match = EXPLICIT_LATENCY_RE.match(line)
        if latency_match:
            parsed = parse_explicit_latency_body(latency_match.group("body"))
            if parsed:
                latency_reports.append(parsed)
                instance_id = parse_input_instance_id(latency_match.group("body"))
                instance_key = str(instance_id) if instance_id is not None else "unbound"
                latest_latency_by_instance[instance_key] = parsed
            continue
        raw_match = RAW_STAGE_RE.match(line)
        if raw_match:
            raw_capture_only = pending_raw_capture
            pending_raw_capture = None
            raw_reports.append(parse_raw_stage(raw_match, RETENTION_LATEST_N))
            continue
        raw_legacy_match = RAW_STAGE_LEGACY_RE.match(line)
        if raw_legacy_match:
            # The pre-3443 Raw stage implementation already used a ring, but
            # the artifact did not identify its retained population. Keep that
            # distinction explicit instead of silently treating it as the
            # current fully surfaced format.
            raw_capture_only = pending_raw_capture
            pending_raw_capture = None
            raw_reports.append(
                parse_raw_stage(raw_legacy_match, RETENTION_LEGACY_UNVERSIONED)
            )
            continue
        if line.startswith("[MelonPrimeRawPerf] stage_us "):
            # A malformed/newer stage report must not leave the preceding
            # marker available for a later report.
            pending_raw_capture = None
            raw_capture_only = None
        lock_match = RAW_LOCK_RE.match(line)
        if lock_match:
            lock_reports.append({
                key: int(value)
                for key, value in KEY_VALUE_RE.findall(lock_match.group("body"))
            })

    if not input_reports and not latency_reports and not raw_reports:
        raise SummaryError(
            f"{path} contains neither [MelonPrimePerf] input_metric_us nor "
            "[MelonPrimeRawPerf] stage_us telemetry"
        )

    latest_input = input_reports[-1] if input_reports else {}
    latest_raw = raw_reports[-1] if raw_reports else None
    latest_locks = lock_reports[-1] if lock_reports else None
    retention_modes = {
        metric["retention_mode"]
        for report in (
            list(latest_input_by_instance.values())
            + list(latest_latency_by_instance.values())
        )
        for metric in report.values()
        if metric.get("retention_mode")
    }
    if latest_raw is not None:
        retention_modes.add(latest_raw["retention_mode"])
    retention_mode = (
        next(iter(retention_modes))
        if len(retention_modes) == 1
        else "mixed" if retention_modes else None
    )
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
        "latency_report_count": len(latency_reports),
        "latency_instance_ids": [
            int(key) for key in sorted(
                (key for key in latest_latency_by_instance if key != "unbound"),
                key=int,
            )
        ],
        "latest_latency_by_instance": latest_latency_by_instance,
        "raw_report_count": len(raw_reports),
        "latest_input": latest_input,
        "latest_raw": latest_raw,
        "latest_lock_planes": latest_locks,
        "generic_capture_only_by_instance": generic_capture_only_by_instance,
        "raw_capture_only": raw_capture_only,
        "retention_mode": retention_mode,
        "retention_modes": sorted(retention_modes),
    }


def require_metric(
    report: dict[str, dict[str, Any]], name: str, minimum_calls: int,
    suffix: str = "",
) -> dict[str, Any]:
    metric = report.get(name)
    if metric is None:
        raise SummaryError(f"{name}{suffix} is missing from the telemetry report")
    if int(metric["calls"]) < minimum_calls:
        raise SummaryError(
            f"{name}{suffix} has {metric['calls']} calls; expected at least "
            f"{minimum_calls}"
        )
    return metric


def require_input_metric(
    summary: dict[str, Any], name: str, minimum_calls: int
) -> dict[str, Any]:
    return require_metric(summary["latest_input"], name, minimum_calls)


def require_safe_retention(
    metric: dict[str, Any], name: str, suffix: str,
    allow_legacy_first_n: bool,
) -> None:
    if metric.get("retention_mode") != RETENTION_LEGACY_FIRST_N:
        return
    calls = int(metric["calls"])
    retention_cap = int(metric.get("retention_cap", INPUT_RETENTION_CAP))
    if calls <= retention_cap or allow_legacy_first_n:
        return
    raise SummaryError(
        f"{name} uses legacy_first_n retention for {calls} calls{suffix}; "
        f"latest-N percentile budget enforcement is unsafe above the "
        f"{retention_cap}-sample cap (use --allow-legacy-first-n only for "
        "explicit historical analysis)"
    )


def require_capture_only(
    summary: dict[str, Any], mode: str,
    reports_by_instance: dict[str, dict[str, dict[str, Any]]],
) -> None:
    generic_capture = summary.get("generic_capture_only_by_instance", {})
    if mode in ("keyboard", "controller", "raw"):
        for instance_id in reports_by_instance:
            if generic_capture.get(instance_id) is not True:
                suffix = "" if len(reports_by_instance) == 1 else (
                    f" for instance {instance_id}"
                )
                raise SummaryError(
                    f"{mode} budget certification requires a verified "
                    f"MELONPRIME_PERF_CAPTURE_ONLY run{suffix}"
                )
    if mode == "raw" and summary.get("raw_capture_only") is not True:
        raise SummaryError(
            "raw budget certification requires a verified "
            "MELONPRIME_RAW_INPUT_PERF_CAPTURE_ONLY run"
        )


def capture_mode_verified(
    summary: dict[str, Any], mode: str,
) -> bool:
    """Return whether the report contains markers for the selected scope."""
    reports_by_instance = summary.get("latest_input_by_instance") or {
        "unbound": summary.get("latest_input", {})
    }
    generic_capture = summary.get("generic_capture_only_by_instance", {})
    generic_verified = bool(reports_by_instance) and all(
        generic_capture.get(instance_id) is True
        for instance_id in reports_by_instance
    )
    if not generic_verified:
        return False
    if mode == "raw" or (
        mode == "all" and summary.get("latest_raw") is not None
    ):
        return summary.get("raw_capture_only") is True
    return True


def require_raw_stage(
    raw: dict[str, Any], stage_name: str, minimum_calls: int,
) -> dict[str, Any]:
    metric = raw.get("stage_metrics", {}).get(stage_name)
    if metric is None:
        raise SummaryError(f"Raw telemetry is missing stage {stage_name}")

    # An explicitly allowed legacy-unversioned report has no trustworthy call
    # count. It is suitable only for historical analysis, not sample-gated
    # certification; the caller has already required the explicit opt-in.
    calls = metric.get("calls")
    if calls is None:
        return metric
    if int(calls) < minimum_calls:
        raise SummaryError(
            f"raw {stage_name} has {calls} calls; expected at least "
            f"{minimum_calls}"
        )
    retained = metric.get("retained")
    retained_floor = min(
        minimum_calls,
        int(metric.get("retention_cap", INPUT_RETENTION_CAP)),
    )
    if retained is None or int(retained) < retained_floor:
        raise SummaryError(
            f"raw {stage_name} retains {retained} samples; expected at least "
            f"{retained_floor}"
        )
    return metric


def enforce(
    summary: dict[str, Any], mode: str, minimum_calls: int,
    allow_legacy_first_n: bool = False,
    allow_legacy_raw_unversioned: bool = False,
) -> None:
    if mode == "all":
        raise SummaryError(
            "--check-budget requires explicit --mode keyboard|controller|raw"
        )
    if minimum_calls < MIN_CERTIFICATION_SAMPLES:
        raise SummaryError(
            "--check-budget requires --min-input-samples >= "
            f"{MIN_CERTIFICATION_SAMPLES}; short runs are not certifiable"
        )
    reports_by_instance = summary.get("latest_input_by_instance") or {
        "unbound": summary["latest_input"]
    }
    require_capture_only(summary, mode, reports_by_instance)
    for instance_id, input_report in reports_by_instance.items():
        suffix = "" if len(reports_by_instance) == 1 else f" for instance {instance_id}"
        for name, metric in input_report.items():
            require_safe_retention(
                metric, name, suffix, allow_legacy_first_n
            )
    latency_reports_by_instance = summary.get("latest_latency_by_instance") or {}
    for instance_id, latency_report in latency_reports_by_instance.items():
        suffix = "" if len(latency_reports_by_instance) == 1 else f" for instance {instance_id}"
        for name, metric in latency_report.items():
            require_safe_retention(
                metric, name, suffix, allow_legacy_first_n
            )
    for instance_id, input_report in reports_by_instance.items():
        input_summary = {"latest_input": input_report}
        input_total = require_input_metric(input_summary, "input_total", minimum_calls)
        suffix = "" if len(reports_by_instance) == 1 else f" for instance {instance_id}"
        require_safe_retention(
            input_total, "input_total", suffix, allow_legacy_first_n
        )
        if mode == "controller":
            for name in CONTROLLER_EVIDENCE_METRICS:
                require_metric(input_report, name, minimum_calls, suffix)
        elif mode == "keyboard":
            for name in CONTROLLER_EVIDENCE_METRICS:
                metric = require_metric(input_report, name, 0, suffix)
                if int(metric["calls"]) != 0:
                    raise SummaryError(
                        f"keyboard budget certification found {name} calls "
                        f"({metric['calls']}){suffix}"
                    )
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
        if raw.get("retention_mode") == RETENTION_LEGACY_UNVERSIONED:
            if not allow_legacy_raw_unversioned:
                raise SummaryError(
                    "raw budget certification cannot verify sample counts for "
                    "legacy_unversioned telemetry (use "
                    "--allow-legacy-raw-unversioned only for explicit "
                    "historical analysis)"
                )
        elif raw.get("retention_mode") != RETENTION_LATEST_N:
            raise SummaryError(
                "raw budget certification requires a recognized Raw retention mode"
            )
        raw_stages = {
            stage_name: require_raw_stage(
                raw,
                stage_name,
                minimum_calls if stage_name in RAW_REQUIRED_STAGES else 0,
            )
            for stage_name in RAW_EVIDENCE_STAGES
        }
        snapshot = raw_stages[RAW_REQUIRED_STAGES[0]]
        late_latch = raw_stages[RAW_REQUIRED_STAGES[1]]
        if snapshot["p50_us"] + late_latch["p50_us"] >= 40.0:
            raise SummaryError(
                "Raw snapshot+late-latch p50 exceeds 40 us: "
                f"{snapshot['p50_us'] + late_latch['p50_us']:.1f}"
            )


def format_optional_us(value: Any) -> str:
    return "n/a" if value is None else f"{float(value):.1f}"


def markdown(
    summary: dict[str, Any], *, mode: str = "all",
    certification_scope: str = "summary_only", certified: bool = False,
    historical_analysis: bool = False, capture_verified: bool | None = None,
    minimum_input_samples: int = MIN_CERTIFICATION_SAMPLES,
    budget_checked: bool = False,
) -> str:
    if capture_verified is None:
        capture_verified = capture_mode_verified(summary, mode)
    lines = [
        "# Input performance summary",
        "",
        f"Source: `{summary['path']}`",
        f"Certification scope: {certification_scope}",
        f"Certified: {'true' if certified else 'false'}",
        f"Historical analysis: {'true' if historical_analysis else 'false'}",
        f"Mode: {mode}",
        f"Capture-only verified: {'true' if capture_verified else 'false'}",
        f"Minimum samples: {minimum_input_samples}",
        f"Budget checked: {'true' if budget_checked else 'false'}",
        f"Retention mode: `{summary.get('retention_mode') or 'unknown'}`",
    ]
    if historical_analysis:
        lines.extend([
            "",
            "> NOT A CERTIFICATION RESULT",
            "> Historical analysis only.",
        ])
    generic_capture = summary.get("generic_capture_only_by_instance", {})
    if generic_capture:
        capture_values = ", ".join(
            f"instance {instance_id}={'true' if value else 'false'}"
            for instance_id, value in sorted(
                generic_capture.items(),
                key=lambda item: (
                    item[0] == "unbound",
                    int(item[0]) if item[0] != "unbound" else 0,
                ),
            )
        )
        lines.append(f"Generic capture-only: `{capture_values}`")
    else:
        lines.append("Generic capture-only: `unknown`")
    if summary.get("raw_capture_only") is not None:
        lines.append(
            "Raw capture-only: `"
            f"{'true' if summary['raw_capture_only'] else 'false'}`"
        )
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
            "| Metric | Calls | Retained | Retention | p50 (us) | p95 (us) | p99 (us) | Max (us) | Max scope | Retained max (us) |",
            "|---|---:|---:|---|---:|---:|---:|---:|---|---:|",
        ])
        for name in INPUT_METRICS:
            metric = reports_by_instance[instance_key].get(name)
            if metric is None:
                continue
            lines.append(
                f"| `{name}` | {metric['calls']} | {metric['retained']} | "
                f"`{metric['retention_mode']}` | "
                f"{metric['p50_us']:.1f} | {metric['p95_us']:.1f} | "
                f"{metric['p99_us']:.1f} | {metric['max_us']:.1f} | "
                f"`{metric['max_scope']}` | "
                f"{format_optional_us(metric.get('retained_max_us'))} |"
            )
    latency_by_instance = summary.get("latest_latency_by_instance") or {}
    latency_keys = sorted(
        latency_by_instance,
        key=lambda key: (key == "unbound", int(key) if key != "unbound" else 0),
    )
    for instance_key in latency_keys:
        lines.extend([
            "",
            f"## Explicit input latency instance `{instance_key}`",
            "",
            "| Metric | Calls | Retained | Retention | p50 (us) | p95 (us) | p99 (us) | Max (us) | Max scope | Retained max (us) |",
            "|---|---:|---:|---|---:|---:|---:|---:|---|---:|",
        ])
        for name in EXPLICIT_LATENCY_METRICS:
            metric = latency_by_instance[instance_key].get(name)
            if metric is None:
                continue
            lines.append(
                f"| `{name}` | {metric['calls']} | {metric['retained']} | "
                f"`{metric['retention_mode']}` | "
                f"{metric['p50_us']:.1f} | {metric['p95_us']:.1f} | "
                f"{metric['p99_us']:.1f} | {metric['max_us']:.1f} | "
                f"`{metric['max_scope']}` | "
                f"{format_optional_us(metric.get('retained_max_us'))} |"
            )
    raw = summary.get("latest_raw")
    if raw is not None:
        raw_stage_metrics = raw.get("stage_metrics", {})
        lines.extend([
            "",
            "## Windows Raw Input",
            "",
            "| Stage | Calls | Retained | Retention | p50 (us) | p95 (us) | p99 (us) | Max (us) | Max scope | Retained max (us) |",
            "|---|---:|---:|---|---:|---:|---:|---:|---|---:|",
        ])
        for stage_name, label in (
            ("snapshot", "snapshot"),
            ("late_latch", "late_latch"),
            ("deferred_drain", "deferred_drain"),
        ):
            metric = raw_stage_metrics.get(stage_name)
            if metric is None:
                continue
            calls = "n/a" if metric["calls"] is None else str(metric["calls"])
            retained = (
                "n/a" if metric["retained"] is None else str(metric["retained"])
            )
            lines.append(
                f"| `{label}` | {calls} | {retained} | "
                f"`{metric['retention_mode']}` | {metric['p50_us']:.1f} | "
                f"{metric['p95_us']:.1f} | {metric['p99_us']:.1f} | "
                f"{metric['max_us']:.1f} | `{metric['max_scope']}` | "
                f"{format_optional_us(metric.get('retained_max_us'))} |"
            )
        lines.extend([
            "",
            f"batch calls/nonempty/empty/events: {raw['batch_calls']}/"
            f"{raw['batch_nonempty']}/{raw['batch_empty']}/{raw['batch_events']}",
            "Raw calls, stage sums/maxima, lock totals, and batch totals are "
            "cumulative for the current Raw service/capture lifetime; stage "
            "percentiles and retained_max use the latest retained samples.",
        ])
    locks = summary.get("latest_lock_planes")
    if locks is not None:
        lines.extend([
            "",
            "Raw lock planes (Raw service/capture lifetime totals):",
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
    import contextlib
    import io
    import tempfile

    text = "\n".join([
        "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1",
        "[MelonPrimePerf] capture_mode instance_id=1 capture_only=1",
        "[MelonPrimeRawPerf] capture_mode capture_only=1",
        "[MelonPrimePerf] input_metric_us instance_id=0 input_total[c=5000 p50=4.0 p95=8.0 p99=12.0 max=20.0] joystick_lock_wait[c=1001 p50=0.1 p95=0.2 p99=0.4 max=1.0] joystick_sample[c=1001 p50=5.0 p95=7.0 p99=9.0 max=15.0] joystick_project[c=1001 p50=1.0 p95=2.0 p99=3.0 max=5.0] joystick_sdl_update[c=1001 p50=0.5 p95=0.8 p99=1.0 max=2.0] joystick_process_mutex_wait[c=1001 p50=0.0 p95=0.1 p99=0.2 max=0.5] joystick_process_mutex_hold[c=1001 p50=0.2 p95=0.4 p99=0.6 max=1.0]",
        "[MelonPrimePerf] input_metric_us instance_id=1 input_total[c=5000 retained=2048 p50=4.0 p95=8.0 p99=12.0 max=30.0 retained_max=20.0]",
        "[MelonPrimePerf] explicit_latency_us instance_id=0 frame_input_sample_to_runframe_begin_us=p50=5.0 p95=9.0 p99=13.0 max=31.0 n=600 input_sample_to_present_end_us=p50=7.0 p95=11.0 p99=15.0 max=33.0 n=600",
        "[MelonPrimePerf] explicit_latency_us instance_id=1 frame_input_sample_to_runframe_begin_us=calls=5000 retained=2048 p50=5.0 p95=9.0 p99=13.0 max=31.0 retained_max=21.0 input_sample_to_present_end_us=calls=5000 retained=2048 p50=7.0 p95=11.0 p99=15.0 max=33.0 retained_max=23.0",
        "[MelonPrimeRawPerf] stage_us snapshot[calls=3000 retained=2048 p50=4.0 p95=6.0 p99=8.0 max=12.0 retained_max=10.0] late_latch[calls=3000 retained=2048 p50=3.0 p95=5.0 p99=7.0 max=10.0 retained_max=9.0] deferred_drain[calls=3000 retained=2048 p50=20.0 p95=25.0 p99=30.0 max=40.0 retained_max=35.0] lock_wait_ns snapshot=10 late=20 deferred=30 hidden=40 native=50 | raw_batch calls=1001 nonempty=10 empty=991 events=20 late_delta_claims=5 post_draw_events=10",
        "[MelonPrimeRawPerf] lock_planes subscription_mutex_acq=2 subscription_mutex_wait_ns=3 subscription_mutex_hold_ns=4 subscription_mutex_max_wait_ns=5 frame_mutex_acq=1001 frame_mutex_wait_ns=6 frame_mutex_hold_ns=7 frame_mutex_max_wait_ns=8",
    ]) + "\n"
    with tempfile.TemporaryDirectory(prefix="melonprime-input-perf-") as directory:
        path = Path(directory) / "telemetry.log"
        path.write_text(text, encoding="utf-8")
        summary = parse_log(path)
        if summary["retention_mode"] != "mixed":
            raise SummaryError("self-test did not preserve retention provenance")
        if summary["generic_capture_only_by_instance"] != {"0": True, "1": True}:
            raise SummaryError("self-test did not parse generic capture-only markers")
        if summary["raw_capture_only"] is not True:
            raise SummaryError("self-test did not parse Raw capture-only marker")
        legacy_input = summary["latest_input_by_instance"]["0"]["input_total"]
        if legacy_input["retention_mode"] != RETENTION_LEGACY_FIRST_N:
            raise SummaryError("self-test did not classify legacy input retention")
        if legacy_input["max_scope"] != "whole_window":
            raise SummaryError("self-test did not preserve legacy whole max")
        if legacy_input["whole_max_us"] != 20.0:
            raise SummaryError("self-test did not preserve legacy whole max")
        if legacy_input["retained_max_us"] is not None:
            raise SummaryError("self-test invented a legacy retained max")
        try:
            enforce(summary, "raw", 1000)
        except SummaryError as error:
            if "--allow-legacy-first-n" not in str(error):
                raise SummaryError(
                    "self-test rejected legacy retention for the wrong reason"
                ) from error
        else:
            raise SummaryError(
                "self-test allowed unsafe legacy percentile budget by default"
            )
        enforce(summary, "raw", 1000, allow_legacy_first_n=True)
        if summary["input_instance_ids"] != [0, 1]:
            raise SummaryError("self-test did not preserve input instance identity")
        if summary["latest_input_by_instance"]["0"]["input_total"]["calls"] != 5000:
            raise SummaryError("self-test did not group input metrics by instance")
        if summary["latest_input_by_instance"]["0"]["input_total"]["retained"] != 2048:
            raise SummaryError("self-test did not infer legacy retention cap")
        if summary["latest_input_by_instance"]["1"]["input_total"]["retained"] != 2048:
            raise SummaryError("self-test did not parse retained input samples")
        if summary["latest_input_by_instance"]["1"]["input_total"]["whole_max_us"] != 30.0:
            raise SummaryError("self-test did not preserve whole-run max")
        if summary["latest_latency_by_instance"]["1"][
            "frame_input_sample_to_runframe_begin_us"
        ]["retained"] != 2048:
            raise SummaryError("self-test did not parse explicit latency retention")
        if summary["latest_latency_by_instance"]["0"][
            "frame_input_sample_to_runframe_begin_us"
        ]["retention_mode"] != RETENTION_LEGACY_FIRST_N:
            raise SummaryError("self-test did not parse legacy explicit provenance")
        if summary["latest_raw"]["snapshot_calls"] != 3000:
            raise SummaryError("self-test did not parse Raw stage calls")
        if summary["latest_raw"]["snapshot_retained"] != 2048:
            raise SummaryError("self-test did not parse Raw stage retained count")
        if summary["latest_raw"]["snapshot_retained_max"] != 10.0:
            raise SummaryError("self-test did not parse Raw retained max")
        if summary["latest_raw"]["stage_metrics"]["snapshot"]["max_scope"] \
            != RAW_LIFETIME_SCOPE:
            raise SummaryError("self-test did not preserve Raw lifetime max scope")
        rendered = markdown(summary)
        for stage_name in ("snapshot", "late_latch", "deferred_drain"):
            if stage_name not in rendered:
                raise SummaryError(
                    f"self-test did not render Raw stage {stage_name}"
                )
        if "## Input instance `0`" not in rendered:
            raise SummaryError("self-test did not render input instance identity")
        if "legacy_first_n" not in rendered:
            raise SummaryError("self-test did not render retention provenance")
        if summary["latest_raw"]["batch_empty"] != 991:
            raise SummaryError("self-test did not parse raw batch counts")

        legacy_raw_path = Path(directory) / "legacy-raw.log"
        legacy_raw_path.write_text(
            "[MelonPrimeRawPerf] stage_us "
            "snapshot[p50=1.0 p95=2.0 p99=3.0 max=4.0] "
            "late_latch[p50=5.0 p95=6.0 p99=7.0 max=8.0] "
            "deferred_drain[p50=9.0 p95=10.0 p99=11.0 max=12.0] "
            "lock_wait_ns snapshot=1 late=2 deferred=3 hidden=4 native=5 | "
            "raw_batch calls=6 nonempty=7 empty=8 events=9 "
            "late_delta_claims=10 post_draw_events=11\n",
            encoding="utf-8",
        )
        legacy_raw_summary = parse_log(legacy_raw_path)
        if legacy_raw_summary["retention_mode"] != RETENTION_LEGACY_UNVERSIONED:
            raise SummaryError("self-test did not classify legacy Raw provenance")
        if legacy_raw_summary["latest_raw"]["stage_metrics"]["late_latch"][
            "calls"
        ] is not None:
            raise SummaryError("self-test invented legacy Raw call counts")

        def expect_rejection(
            name: str, log_text: str, mode: str, expected: str,
            **kwargs: Any,
        ) -> None:
            rejection_path = Path(directory) / name
            rejection_path.write_text(log_text, encoding="utf-8")
            rejection_summary = parse_log(rejection_path)
            try:
                enforce(rejection_summary, mode, MIN_CERTIFICATION_SAMPLES, **kwargs)
            except SummaryError as error:
                if expected not in str(error):
                    raise SummaryError(
                        f"self-test rejected {name} for the wrong reason"
                    ) from error
            else:
                raise SummaryError(f"self-test allowed invalid {name}")

        controller_no_evidence = "\n".join([
            "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1",
            "[MelonPrimePerf] input_metric_us instance_id=0 "
            "input_total[c=5000 retained=2048 p50=4.0 p95=8.0 p99=12.0 "
            "max=20.0 retained_max=20.0] "
            "joystick_lock_wait[c=1000 retained=1000 p50=0.1 p95=0.2 "
            "p99=0.4 max=1.0 retained_max=1.0] "
            "joystick_sample[c=0 retained=0 p50=0.0 p95=0.0 p99=0.0 "
            "max=0.0 retained_max=0.0] "
            "joystick_project[c=1000 retained=1000 p50=1.0 p95=2.0 "
            "p99=3.0 max=5.0 retained_max=5.0] "
            "joystick_sdl_update[c=1000 retained=1000 p50=0.5 p95=0.8 "
            "p99=1.0 max=2.0 retained_max=2.0] "
            "joystick_process_mutex_wait[c=1000 retained=1000 p50=0.0 "
            "p95=0.1 p99=0.2 max=0.5 retained_max=0.5] "
            "joystick_process_mutex_hold[c=1000 retained=1000 p50=0.2 "
            "p95=0.4 p99=0.6 max=1.0 retained_max=1.0]",
        ]) + "\n"
        controller_valid = controller_no_evidence.replace(
            "joystick_sample[c=0 retained=0",
            "joystick_sample[c=1000 retained=1000",
        )
        controller_valid_path = Path(directory) / "controller-valid.log"
        controller_valid_path.write_text(controller_valid, encoding="utf-8")
        enforce(
            parse_log(controller_valid_path), "controller",
            MIN_CERTIFICATION_SAMPLES,
        )

        markerless_path = Path(directory) / "markerless-legacy.log"
        markerless_path.write_text(
            "\n".join(
                line for line in text.splitlines()
                if "capture_mode" not in line
            ) + "\n",
            encoding="utf-8",
        )
        markerless_summary = parse_log(markerless_path)
        if markerless_summary["generic_capture_only_by_instance"]:
            raise SummaryError("self-test found an unexpected generic capture marker")
        if markerless_summary["raw_capture_only"] is not None:
            raise SummaryError("self-test found an unexpected Raw capture marker")
        if capture_mode_verified(markerless_summary, "all"):
            raise SummaryError("self-test verified a markerless capture")

        def run_cli(arguments: list[str]) -> tuple[int, str, str]:
            stdout = io.StringIO()
            stderr = io.StringIO()
            with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
                return main(arguments), stdout.getvalue(), stderr.getvalue()

        return_code, _, stderr = run_cli([
            "--check-budget", str(controller_valid_path)
        ])
        if return_code != 1 or "requires explicit --mode" not in stderr:
            raise SummaryError("self-test allowed an implicit all budget certification")
        return_code, _, stderr = run_cli([
            "--mode", "all", "--check-budget", str(controller_valid_path)
        ])
        if return_code != 1 or "requires explicit --mode" not in stderr:
            raise SummaryError("self-test allowed explicit all budget certification")

        return_code, stdout, stderr = run_cli([
            "--mode", "controller", "--check-budget", str(controller_valid_path)
        ])
        if return_code != 0 or stderr:
            raise SummaryError("self-test rejected a valid strict certification")
        strict_output = json.loads(stdout)
        if strict_output["certified"] is not True:
            raise SummaryError("self-test did not mark strict certification")
        if strict_output["certification_scope"] != "strict":
            raise SummaryError("self-test did not label strict certification scope")
        if strict_output["capture_mode_verified"] is not True:
            raise SummaryError("self-test did not verify strict capture mode")
        strict_markdown_path = Path(directory) / "strict.md"
        return_code, _, stderr = run_cli([
            "--mode", "controller", "--check-budget",
            "--markdown-out", str(strict_markdown_path),
            str(controller_valid_path),
        ])
        if return_code != 0 or stderr:
            raise SummaryError("self-test could not render strict Markdown")
        strict_rendered = strict_markdown_path.read_text(encoding="utf-8")
        for needle in (
            "Certification scope: strict",
            "Certified: true",
            "Mode: controller",
            "Capture-only verified: true",
            "Minimum samples: 1000",
            "Budget checked: true",
        ):
            if needle not in strict_rendered:
                raise SummaryError(
                    f"self-test did not render strict metadata: {needle}"
                )

        historical_markdown_path = Path(directory) / "historical.md"
        return_code, _, stderr = run_cli([
            "--historical-analysis", "--markdown-out",
            str(historical_markdown_path), str(markerless_path),
        ])
        if return_code != 0 or stderr:
            raise SummaryError("self-test could not render historical Markdown")
        historical_rendered = historical_markdown_path.read_text(encoding="utf-8")
        for needle in (
            "Certification scope: historical_analysis",
            "Certified: false",
            "Historical analysis: true",
            "Mode: all",
            "Capture-only verified: false",
            "> NOT A CERTIFICATION RESULT",
            "> Historical analysis only.",
        ):
            if needle not in historical_rendered:
                raise SummaryError(
                    f"self-test did not render historical metadata: {needle}"
                )

        markerless_report = controller_valid.replace(
            "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1\n",
            "",
        )
        mixed_generic_path = Path(directory) / "mixed-generic-generation.log"
        mixed_generic_path.write_text(
            controller_valid + markerless_report, encoding="utf-8"
        )
        mixed_generic_summary = parse_log(mixed_generic_path)
        if mixed_generic_summary["generic_capture_only_by_instance"]:
            raise SummaryError("self-test reused a generic marker across reports")
        expect_rejection(
            "mixed-generic-generation.log", controller_valid + markerless_report,
            "controller", "verified MELONPRIME_PERF_CAPTURE_ONLY run",
        )

        reverse_generic = controller_valid.replace(
            "capture_only=1", "capture_only=0", 1
        ) + controller_valid
        reverse_generic_path = Path(directory) / "reverse-generic-generation.log"
        reverse_generic_path.write_text(reverse_generic, encoding="utf-8")
        reverse_generic_summary = parse_log(reverse_generic_path)
        if reverse_generic_summary["generic_capture_only_by_instance"] != {"0": True}:
            raise SummaryError("self-test did not bind the newest generic marker")
        enforce(
            reverse_generic_summary, "controller", MIN_CERTIFICATION_SAMPLES
        )

        instance_one_report = controller_valid.replace(
            "instance_id=0", "instance_id=1"
        )
        multi_instance_path = Path(directory) / "multi-instance-marker-reuse.log"
        multi_instance_path.write_text(
            controller_valid + instance_one_report + markerless_report,
            encoding="utf-8",
        )
        multi_instance_summary = parse_log(multi_instance_path)
        if multi_instance_summary["generic_capture_only_by_instance"] != {"1": True}:
            raise SummaryError("self-test reused a marker between instances")
        expect_rejection(
            "multi-instance-marker-reuse.log",
            controller_valid + instance_one_report + markerless_report,
            "controller", "verified MELONPRIME_PERF_CAPTURE_ONLY run",
        )

        return_code, _, stderr = run_cli([
            "--mode", "controller", "--check-budget",
            "--historical-analysis", str(controller_valid_path),
        ])
        if return_code != 1 or "cannot be combined" not in stderr:
            raise SummaryError("self-test combined strict and historical analysis")
        return_code, _, stderr = run_cli([
            "--allow-legacy-first-n", str(controller_valid_path)
        ])
        if return_code != 1 or "require --historical-analysis" not in stderr:
            raise SummaryError("self-test allowed a legacy escape hatch outside history")

        for historical_path in (markerless_path, legacy_raw_path):
            return_code, stdout, stderr = run_cli([
                "--historical-analysis", str(historical_path)
            ])
            if return_code != 0 or stderr:
                raise SummaryError(
                    f"self-test could not summarize historical artifact {historical_path.name}"
                )
            historical_output = json.loads(stdout)
            if historical_output["certified"] is not False:
                raise SummaryError("self-test certified a historical analysis")
            if historical_output["historical_analysis"] is not True:
                raise SummaryError("self-test did not mark historical analysis")
            if historical_output["certification_scope"] != "historical_analysis":
                raise SummaryError("self-test did not label historical analysis scope")
            if historical_output["capture_mode_verified"] is not False:
                raise SummaryError("self-test verified a markerless historical capture")

        expect_rejection(
            "controller-no-evidence.log", controller_no_evidence,
            "controller", "joystick_sample has 0 calls",
        )
        expect_rejection(
            "controller-without-capture-marker.log",
            controller_valid.replace(
                "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1\n",
                "",
            ),
            "controller", "verified MELONPRIME_PERF_CAPTURE_ONLY run",
        )

        raw_insufficient = "\n".join([
            "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1",
            "[MelonPrimePerf] input_metric_us instance_id=0 input_total["
            "c=5000 retained=2048 p50=4.0 p95=8.0 p99=12.0 max=20.0 "
            "retained_max=20.0]",
            "[MelonPrimeRawPerf] capture_mode capture_only=1",
            "[MelonPrimeRawPerf] stage_us "
            "snapshot[calls=2 retained=2 p50=4.0 p95=6.0 p99=8.0 "
            "max=12.0 retained_max=10.0] "
            "late_latch[calls=2 retained=2 p50=3.0 p95=5.0 p99=7.0 "
            "max=10.0 retained_max=9.0] "
            "deferred_drain[calls=2 retained=2 p50=20.0 p95=25.0 p99=30.0 "
            "max=40.0 retained_max=35.0] "
            "lock_wait_ns snapshot=10 late=20 deferred=30 hidden=40 native=50 | "
            "raw_batch calls=2 nonempty=1 empty=1 events=2 "
            "late_delta_claims=1 post_draw_events=1",
        ]) + "\n"
        expect_rejection(
            "raw-insufficient.log", raw_insufficient,
            "raw", "raw snapshot has 2 calls",
        )

        raw_stage = next(
            line for line in raw_insufficient.splitlines()
            if line.startswith("[MelonPrimeRawPerf] stage_us ")
        )
        raw_stage = raw_stage.replace(
            "calls=2 retained=2", "calls=1000 retained=1000"
        )
        raw_generic_prefix = "\n".join(
            line for line in raw_insufficient.splitlines()
            if line.startswith("[MelonPrimePerf] ")
        )
        raw_mixed = "\n".join([
            raw_generic_prefix,
            "[MelonPrimeRawPerf] capture_mode capture_only=1",
            raw_stage,
            raw_stage,
        ]) + "\n"
        raw_mixed_path = Path(directory) / "mixed-raw-generation.log"
        raw_mixed_path.write_text(raw_mixed, encoding="utf-8")
        raw_mixed_summary = parse_log(raw_mixed_path)
        if raw_mixed_summary["raw_capture_only"] is not None:
            raise SummaryError("self-test reused a Raw marker across reports")
        expect_rejection(
            "mixed-raw-generation.log", raw_mixed, "raw",
            "verified MELONPRIME_RAW_INPUT_PERF_CAPTURE_ONLY run",
        )

        keyboard_contamination = "\n".join([
            "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1",
            "[MelonPrimePerf] input_metric_us instance_id=0 "
            "input_total[c=5000 retained=2048 p50=4.0 p95=8.0 p99=12.0 "
            "max=20.0 retained_max=20.0] "
            "joystick_lock_wait[c=1000 retained=1000 p50=0.1 p95=0.2 "
            "p99=0.4 max=1.0 retained_max=1.0] "
            "joystick_sample[c=1000 retained=1000 p50=5.0 p95=7.0 "
            "p99=9.0 max=15.0 retained_max=15.0] "
            "joystick_project[c=1000 retained=1000 p50=1.0 p95=2.0 "
            "p99=3.0 max=5.0 retained_max=5.0] "
            "joystick_sdl_update[c=1000 retained=1000 p50=0.5 p95=0.8 "
            "p99=1.0 max=2.0 retained_max=2.0] "
            "joystick_process_mutex_wait[c=1000 retained=1000 p50=0.0 "
            "p95=0.1 p99=0.2 max=0.5 retained_max=0.5] "
            "joystick_process_mutex_hold[c=1000 retained=1000 p50=0.2 "
            "p95=0.4 p99=0.6 max=1.0 retained_max=1.0]",
        ]) + "\n"
        expect_rejection(
            "keyboard-contamination.log", keyboard_contamination,
            "keyboard", "keyboard budget certification found",
        )

        short_run_path = Path(directory) / "short-run.log"
        short_run_path.write_text(controller_no_evidence, encoding="utf-8")
        try:
            enforce(
                parse_log(short_run_path), "controller",
                MIN_CERTIFICATION_SAMPLES - 1,
            )
        except SummaryError as error:
            if f"--min-input-samples >= {MIN_CERTIFICATION_SAMPLES}" not in str(error):
                raise SummaryError("self-test accepted the wrong minimum sample contract") from error
        else:
            raise SummaryError("self-test allowed a short budget certification run")

        legacy_raw_cert = "\n".join([
            "[MelonPrimePerf] capture_mode instance_id=0 capture_only=1",
            "[MelonPrimePerf] input_metric_us instance_id=0 input_total["
            "c=5000 retained=2048 p50=4.0 p95=8.0 p99=12.0 max=20.0 "
            "retained_max=20.0]",
            "[MelonPrimeRawPerf] capture_mode capture_only=1",
            legacy_raw_path.read_text(encoding="utf-8").strip(),
        ]) + "\n"
        expect_rejection(
            "legacy-raw-certification.log", legacy_raw_cert,
            "raw", "--allow-legacy-raw-unversioned",
        )
        legacy_raw_cert_path = Path(directory) / "legacy-raw-certification-allowed.log"
        legacy_raw_cert_path.write_text(legacy_raw_cert, encoding="utf-8")
        enforce(
            parse_log(legacy_raw_cert_path), "raw", MIN_CERTIFICATION_SAMPLES,
            allow_legacy_raw_unversioned=True,
        )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("logs", nargs="*", type=Path)
    parser.add_argument(
        "--mode", choices=("all", "keyboard", "controller", "raw"),
        default=None,
        help="certification mode; required with --check-budget (default: all for summaries)",
    )
    parser.add_argument(
        "--min-input-samples", type=int, default=MIN_CERTIFICATION_SAMPLES,
        help=(
            "minimum calls for the applicable certification populations "
            f"(default: {MIN_CERTIFICATION_SAMPLES})"
        ),
    )
    parser.add_argument("--check-budget", action="store_true")
    parser.add_argument(
        "--historical-analysis",
        action="store_true",
        help="summarize legacy or markerless artifacts without certifying them",
    )
    parser.add_argument(
        "--allow-legacy-first-n",
        action="store_true",
        help="deprecated compatibility flag for historical analysis only",
    )
    parser.add_argument(
        "--allow-legacy-raw-unversioned",
        action="store_true",
        help="deprecated compatibility flag for historical analysis only",
    )
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
        if args.check_budget and args.historical_analysis:
            raise SummaryError(
                "--check-budget is strict certification and cannot be combined "
                "with --historical-analysis; omit --check-budget for "
                "non-certifying historical analysis"
            )
        if (
            args.allow_legacy_first_n or args.allow_legacy_raw_unversioned
        ) and not args.historical_analysis:
            raise SummaryError(
                "legacy escape hatches require --historical-analysis; "
                "historical analysis is never certification"
            )
        if args.check_budget and args.mode in (None, "all"):
            raise SummaryError(
                "--check-budget requires explicit --mode keyboard|controller|raw"
            )
        if args.check_budget and args.min_input_samples < MIN_CERTIFICATION_SAMPLES:
            raise SummaryError(
                "--check-budget requires --min-input-samples >= "
                f"{MIN_CERTIFICATION_SAMPLES}; short runs are not certifiable"
            )
        mode = args.mode or "all"
        summaries = [parse_log(path) for path in args.logs]
        if args.check_budget:
            for summary in summaries:
                enforce(
                    summary, mode, args.min_input_samples,
                    allow_legacy_first_n=args.allow_legacy_first_n,
                    allow_legacy_raw_unversioned=args.allow_legacy_raw_unversioned,
                )
        certification_scope = (
            "strict" if args.check_budget else
            "historical_analysis" if args.historical_analysis else
            "summary_only"
        )
        capture_verified = all(
            capture_mode_verified(summary, mode) for summary in summaries
        )
        output: dict[str, Any] = {
            "schema_version": 6,
            "mode": mode,
            "certification_scope": certification_scope,
            "certified": bool(args.check_budget),
            "historical_analysis": args.historical_analysis,
            "capture_mode_verified": capture_verified,
            "minimum_input_samples": args.min_input_samples,
            "budget_checked": args.check_budget,
            "allow_legacy_first_n": args.allow_legacy_first_n,
            "allow_legacy_raw_unversioned": args.allow_legacy_raw_unversioned,
            "runs": summaries,
        }
        rendered_json = json.dumps(output, indent=2, sort_keys=True) + "\n"
        if args.json_out:
            args.json_out.write_text(rendered_json, encoding="utf-8")
        if args.markdown_out:
            if len(summaries) != 1:
                raise SummaryError("--markdown-out requires exactly one telemetry log")
            args.markdown_out.write_text(
                markdown(
                    summaries[0], mode=mode,
                    certification_scope=certification_scope,
                    certified=bool(args.check_budget),
                    historical_analysis=args.historical_analysis,
                    capture_verified=capture_verified,
                    minimum_input_samples=args.min_input_samples,
                    budget_checked=args.check_budget,
                ),
                encoding="utf-8",
            )
        if not args.json_out and not args.markdown_out:
            print(rendered_json, end="")
        return 0
    except SummaryError as error:
        print(f"input performance summary failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
