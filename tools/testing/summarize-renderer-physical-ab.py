#!/usr/bin/env python3
"""Deterministically summarize one physical renderer A/B run.

The runner writes application-side samples in the same Windows QPC domain as
the PowerShell run manifest.  This tool never asks a person to trim a warmup
window: it selects rows mechanically with the recorded measurement boundaries.
Renderer stage telemetry is emitted as one-second windows; its report marker
is used to keep only reports whose application-side QPC timestamp is inside
the same interval.

Example:
  python tools/testing/summarize-renderer-physical-ab.py \
      run-id.run-manifest.json run-id.frames.csv run-id.telemetry.log
"""

from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path
from typing import Any, Iterable


ACCEPTANCE_METRICS = (
    "raster_cpu_prepare_us",
    "raster_reuse_wait_us",
    "raster_record_submit_us",
    "raster_gpu_time_ns",
    "texture_decode_us",
    "texture_resource_create_us",
    "texture_materialize_count",
    "texture_materialize_pre_fence_fail_count",
    "texture_materialize_retry_after_retire_count",
    "texture_materialize_retry_success_count",
    "texture_materialize_retry_fail_count",
    "texture_pending_storage_grow_us",
    "soft2d_total_us",
    "structured2d_metadata_us",
    "structured_pack_us",
    "present_slot_wait_us",
    "present_acquire_wait_us",
    "input_sample_to_present_end_us",
)

COUNTER_METRICS = {
    "texture_materialize_count",
    "texture_materialize_pre_fence_fail_count",
    "texture_materialize_retry_after_retire_count",
    "texture_materialize_retry_success_count",
    "texture_materialize_retry_fail_count",
}

PHASE_RE = re.compile(
    r"^\[MelonPrimePerfPhase\] report_qpc_ticks=(?P<ticks>\d+) "
    r"qpc_frequency=(?P<frequency>\d+)$"
)
CPU_RE = re.compile(
    r"^\[(?P<backend>VulkanPerf|DX12Perf)\] cpu scale=(?P<scale>\d+) "
    r"name=(?P<name>[A-Za-z0-9_]+) p50_us=(?P<p50>[0-9.]+) "
    r"p95_us=(?P<p95>[0-9.]+) p99_us=(?P<p99>[0-9.]+) "
    r"max_us=(?P<max>[0-9.]+) n=(?P<n>\d+)$"
)
COUNTER_RE = re.compile(
    r"^\[(?P<backend>VulkanPerf|DX12Perf)\] counters "
    r"scale=(?P<scale>\d+) (?P<body>.*)$"
)
COUNTER_VALUE_RE = re.compile(r"(?P<name>[A-Za-z0-9_]+)=(?P<value>\d+)")


class SummaryError(RuntimeError):
    """Raised when an artifact contract is not safe to summarize."""


def percentile(values: Iterable[float], p: float) -> float | None:
    ordered = sorted(float(value) for value in values)
    if not ordered:
        return None
    index = p * (len(ordered) - 1)
    lower = math.floor(index)
    upper = min(lower + 1, len(ordered) - 1)
    fraction = index - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def value_stats(
    values: Iterable[float], unit: str, source: str
) -> dict[str, Any]:
    materialized = [float(value) for value in values]
    return {
        "unit": unit,
        "source": source,
        "sample_count": len(materialized),
        "p50": percentile(materialized, 0.50),
        "p95": percentile(materialized, 0.95),
        "p99": percentile(materialized, 0.99),
        "max": max(materialized) if materialized else None,
    }


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise SummaryError(f"{label} does not exist: {path}")


def load_manifest(path: Path) -> tuple[dict[str, Any], int, int, int]:
    require_file(path, "run manifest")
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise SummaryError(f"invalid run manifest JSON: {error}") from error

    try:
        frequency = int(manifest["clock"]["frequency"])
        start = int(manifest["phases"]["measurement_start"]["monotonic_ticks"])
        end = int(manifest["phases"]["measurement_end"]["monotonic_ticks"])
    except (KeyError, TypeError, ValueError) as error:
        raise SummaryError(
            "run manifest must contain clock.frequency and measurement_start/end ticks"
        ) from error
    if frequency <= 0 or start <= 0 or end <= start:
        raise SummaryError("measurement phase boundaries are not a positive interval")
    return manifest, frequency, start, end


def read_frame_rows(
    path: Path, frequency: int, start: int, end: int
) -> tuple[list[dict[str, float]], int]:
    require_file(path, "per-frame CSV")
    selected: list[dict[str, float]] = []
    total = 0
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        required = {"frame_end_ticks", "qpc_frequency", "frame_time_us"}
        if not required.issubset(reader.fieldnames or ()):
            raise SummaryError(
                f"per-frame CSV is missing required columns: {sorted(required)}"
            )
        for row in reader:
            total += 1
            try:
                row_frequency = int(row["qpc_frequency"])
                frame_end = int(row["frame_end_ticks"])
            except (KeyError, TypeError, ValueError) as error:
                raise SummaryError(f"invalid per-frame CSV row: {row}") from error
            if row_frequency != frequency:
                raise SummaryError(
                    "per-frame CSV clock frequency does not match the run manifest"
                )
            if not start <= frame_end <= end:
                continue
            parsed: dict[str, float] = {"frame_end_ticks": frame_end}
            for name in (
                "frame_time_us",
                "input_sample_to_runframe_begin_us",
                "input_sample_to_present_end_us",
            ):
                value = row.get(name, "")
                if value not in (None, ""):
                    try:
                        parsed[name] = float(value)
                    except ValueError as error:
                        raise SummaryError(f"invalid {name} in per-frame CSV: {row}") from error
            selected.append(parsed)
    return selected, total


def read_vulkan_rows(
    path: Path | None, frequency: int, start: int, end: int
) -> tuple[dict[str, list[float]], int]:
    if path is None or not path.exists():
        return {}, 0
    selected: dict[str, list[float]] = {}
    total = 0
    with path.open("r", encoding="utf-8-sig", newline="") as source:
        reader = csv.DictReader(source)
        fields = set(reader.fieldnames or ())
        if "present_end_qpc_ticks" not in fields:
            return {}, 0
        for row in reader:
            total += 1
            try:
                row_frequency = int(row.get("qpc_frequency", "0"))
                sample_ticks = int(row.get("present_end_qpc_ticks", "0"))
            except ValueError as error:
                raise SummaryError(f"invalid Vulkan latency CSV row: {row}") from error
            if row_frequency != frequency:
                raise SummaryError(
                    "Vulkan latency CSV clock frequency does not match the run manifest"
                )
            if not sample_ticks or not start <= sample_ticks <= end:
                continue

            def add(name: str, value: float) -> None:
                selected.setdefault(name, []).append(value)

            try:
                acquire_wait = float(row.get("acquire_wait_us", "0"))
                add("present_acquire_wait_us", acquire_wait)
                submit_start = float(row.get("render_submit_start_time_us", "0"))
                submit_end = float(row.get("render_submit_end_time_us", "0"))
                if submit_end >= submit_start and submit_end > 0:
                    add("raster_record_submit_us", submit_end - submit_start)
                input_sample = float(row.get("input_sample_time_us", "0"))
                present_end = float(row.get("present_end_time_us", "0"))
                if present_end >= input_sample and present_end > 0:
                    add("input_sample_to_present_end_us", present_end - input_sample)
                gpu_start = float(row.get("gpu_render_start_time_us", "0"))
                gpu_end = float(row.get("gpu_render_end_time_us", "0"))
                if gpu_end > gpu_start:
                    add("raster_gpu_time_ns", (gpu_end - gpu_start) * 1000.0)
            except ValueError as error:
                raise SummaryError(f"invalid Vulkan latency value: {row}") from error
    return selected, total


def read_report_blocks(
    path: Path, frequency: int, start: int, end: int
) -> tuple[list[dict[str, Any]], int]:
    require_file(path, "renderer telemetry log")
    blocks: list[dict[str, Any]] = []
    current: dict[str, Any] | None = None
    current_selected = False
    total_markers = 0
    with path.open("r", encoding="utf-8", errors="replace") as source:
        for raw_line in source:
            line = raw_line.rstrip("\r\n")
            phase_match = PHASE_RE.match(line)
            if phase_match:
                total_markers += 1
                report_frequency = int(phase_match.group("frequency"))
                report_ticks = int(phase_match.group("ticks"))
                current = {
                    "ticks": report_ticks,
                    "frequency": report_frequency,
                    "cpu": {},
                    "counters": {},
                }
                current_selected = report_frequency == frequency and start <= report_ticks <= end
                if current_selected:
                    blocks.append(current)
                continue
            if current is None or not current_selected:
                continue
            cpu_match = CPU_RE.match(line)
            if cpu_match:
                current["cpu"][cpu_match.group("name")] = {
                    "p50": float(cpu_match.group("p50")),
                    "p95": float(cpu_match.group("p95")),
                    "p99": float(cpu_match.group("p99")),
                    "max": float(cpu_match.group("max")),
                    "n": int(cpu_match.group("n")),
                    "backend": cpu_match.group("backend").removesuffix("Perf"),
                }
                continue
            counter_match = COUNTER_RE.match(line)
            if counter_match:
                current["counters"] = {
                    item.group("name"): int(item.group("value"))
                    for item in COUNTER_VALUE_RE.finditer(counter_match.group("body"))
                }
    return blocks, total_markers


def stage_summary(records: list[dict[str, Any]], name: str) -> dict[str, Any]:
    windows = [block["cpu"][name] for block in records if name in block["cpu"]]
    if not windows:
        return value_stats([], "us", "renderer telemetry one-second windows") | {
            "reported_window_count": 0,
        }
    return {
        "unit": "us",
        "source": "renderer telemetry one-second window percentiles",
        "reported_window_count": len(windows),
        "sample_count": sum(item["n"] for item in windows),
        "p50": percentile((item["p50"] for item in windows), 0.50),
        "p95": percentile((item["p95"] for item in windows), 0.95),
        "p99": percentile((item["p99"] for item in windows), 0.99),
        "max": max(item["max"] for item in windows),
        "last_window": windows[-1],
    }


def counter_summary(records: list[dict[str, Any]], name: str) -> dict[str, Any]:
    values = [float(block["counters"][name]) for block in records if name in block["counters"]]
    result = value_stats(values, "count", "renderer telemetry one-second counter windows")
    result.update({
        "reported_window_count": len(values),
        "sum": int(sum(values)) if values else 0,
        "last": int(values[-1]) if values else None,
    })
    return result


def pearson(pairs: list[tuple[float, float]]) -> float | None:
    if len(pairs) < 2:
        return None
    xs = [pair[0] for pair in pairs]
    ys = [pair[1] for pair in pairs]
    x_mean = sum(xs) / len(xs)
    y_mean = sum(ys) / len(ys)
    numerator = sum((x - x_mean) * (y - y_mean) for x, y in pairs)
    denominator = math.sqrt(
        sum((x - x_mean) ** 2 for x in xs) *
        sum((y - y_mean) ** 2 for y in ys)
    )
    return numerator / denominator if denominator else None


def format_number(value: Any) -> str:
    if value is None:
        return "—"
    if isinstance(value, float):
        return f"{value:.3f}"
    return str(value)


def build_markdown(summary: dict[str, Any]) -> str:
    conditions = summary.get("conditions", {})
    phase = summary["phase_selection"]
    lines = [
        "# Renderer physical A/B summary",
        "",
        f"- Run: `{summary['run_id']}`",
        f"- Renderer: `{conditions.get('renderer', 'unknown')}`",
        f"- Measurement clock: `{phase['clock_authority']}`",
        f"- Selected frame samples: `{phase['selected_frame_samples']}` / `{phase['input_frame_rows']}`",
        "",
        "## Deterministic phase selection",
        "",
        f"`frame_end_ticks >= {phase['measurement_start_ticks']} && "
        f"frame_end_ticks <= {phase['measurement_end_ticks']}`",
        "",
        "| Metric | p50 | p95 | p99 | max | samples |",
        "| --- | ---: | ---: | ---: | ---: | ---: |",
    ]
    for name, result in summary["metrics"].items():
        lines.append(
            f"| `{name}` | {format_number(result.get('p50'))} | "
            f"{format_number(result.get('p95'))} | {format_number(result.get('p99'))} | "
            f"{format_number(result.get('max'))} | {result.get('sample_count', 0)} |"
        )
    lines.extend(["", "## Vulkan raster correlation", ""])
    correlation = summary["vulkan_raster_gpu_vs_reuse_wait"]
    if correlation.get("pearson_r") is None:
        lines.append(f"Status: `{correlation.get('status', 'not available')}`.")
    else:
        lines.append(
            f"Pearson r between `raster_gpu_time_ns` and `raster_reuse_wait_us`: "
            f"`{correlation['pearson_r']:.6f}` over `{correlation['pair_count']}` reports."
        )
    lines.extend(["", "## Provenance", "", "```json", json.dumps(
        summary.get("provenance", {}), indent=2, ensure_ascii=False, sort_keys=True
    ), "```", ""])
    return "\n".join(lines)


def summarize(
    manifest_path: Path, frame_path: Path, telemetry_path: Path, output_dir: Path
) -> tuple[Path, Path]:
    manifest, frequency, start, end = load_manifest(manifest_path)
    frame_rows, input_frame_rows = read_frame_rows(frame_path, frequency, start, end)
    if not frame_rows:
        raise SummaryError("measurement interval selected zero per-frame samples")

    latency_path_value = manifest.get("artifacts", {}).get("vulkan_latency_csv")
    latency_path = Path(latency_path_value) if latency_path_value else None
    if latency_path is not None and not latency_path.is_absolute():
        latency_path = manifest_path.parent / latency_path
    vulkan_values, input_latency_rows = read_vulkan_rows(latency_path, frequency, start, end)
    report_blocks, report_marker_count = read_report_blocks(
        telemetry_path, frequency, start, end
    )

    metrics: dict[str, dict[str, Any]] = {}
    direct_values: dict[str, list[float]] = {
        "raster_record_submit_us": vulkan_values.get("raster_record_submit_us", []),
        "raster_gpu_time_ns": vulkan_values.get("raster_gpu_time_ns", []),
        "present_acquire_wait_us": vulkan_values.get("present_acquire_wait_us", []),
        "input_sample_to_present_end_us": vulkan_values.get("input_sample_to_present_end_us", []),
    }
    direct_values["frame_time_us"] = [row["frame_time_us"] for row in frame_rows]
    direct_values["input_sample_to_present_end_us"].extend(
        row["input_sample_to_present_end_us"]
        for row in frame_rows
        if row.get("input_sample_to_present_end_us", 0.0) > 0
    )

    # GPU timestamps are optional in the latency CSV. The renderer telemetry
    # still reports the aggregate raster GPU span and frame count, so retain a
    # deterministic per-frame estimate for the required Vulkan metric instead
    # of silently emitting an empty result.
    raster_gpu_report_values = [
        float(block["counters"]["raster_gpu_ns"]) / float(block["counters"]["frames"])
        for block in report_blocks
        if block["counters"].get("raster_gpu_ns") is not None
        and block["counters"].get("frames", 0) > 0
    ]
    if not direct_values["raster_gpu_time_ns"]:
        direct_values["raster_gpu_time_ns"] = raster_gpu_report_values

    for name in ACCEPTANCE_METRICS:
        if name in COUNTER_METRICS:
            metrics[name] = counter_summary(report_blocks, name)
        elif direct_values.get(name):
            unit = "ns" if name.endswith("_ns") or name == "raster_gpu_time_ns" else "us"
            source = "selected per-frame samples"
            if name == "raster_gpu_time_ns" and not vulkan_values.get("raster_gpu_time_ns"):
                source = "renderer telemetry raster_gpu_ns divided by reported frames"
            metrics[name] = value_stats(direct_values[name], unit, source)
        else:
            metrics[name] = stage_summary(report_blocks, name)

    correlation_pairs: list[tuple[float, float]] = []
    for block in report_blocks:
        reuse = block["cpu"].get("raster_reuse_wait_us")
        gpu_ns = block["counters"].get("raster_gpu_ns")
        frames = block["counters"].get("frames", 0)
        if reuse and gpu_ns is not None and frames:
            correlation_pairs.append((reuse["p50"], gpu_ns / frames))
    correlation: dict[str, Any] = {
        "applicable": manifest.get("conditions", {}).get("renderer") == "Vulkan",
        "status": "ok" if correlation_pairs else "insufficient_report_pairs",
        "pair_count": len(correlation_pairs),
        "pearson_r": pearson(correlation_pairs),
        "pair_units": {
            "x": "raster_reuse_wait_us reported-window p50",
            "y": "raster_gpu_time_ns counter divided by reported frames",
        },
    }

    summary: dict[str, Any] = {
        "schema_version": 1,
        "artifact_type": "renderer_physical_ab_summary",
        "run_id": manifest.get("run_id", manifest_path.stem),
        "conditions": manifest.get("conditions", {}),
        "provenance": manifest.get("provenance", {}),
        "phase_selection": {
            "clock_authority": manifest.get("clock", {}).get("authority"),
            "clock_frequency": frequency,
            "measurement_start_ticks": start,
            "measurement_end_ticks": end,
            "selection_rule": "frame_end_ticks >= measurement_start_ticks && frame_end_ticks <= measurement_end_ticks",
            "input_frame_rows": input_frame_rows,
            "selected_frame_samples": len(frame_rows),
            "input_latency_rows": input_latency_rows,
            "selected_report_markers": len(report_blocks),
            "input_report_markers": report_marker_count,
        },
        "frame_time_us": value_stats(
            direct_values["frame_time_us"], "us", "selected per-frame samples"
        ),
        "metrics": metrics,
        "vulkan_raster_gpu_vs_reuse_wait": correlation,
    }

    output_dir.mkdir(parents=True, exist_ok=True)
    summary_json = output_dir / "summary.json"
    summary_md = output_dir / "summary.md"
    summary_json.write_text(
        json.dumps(summary, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    summary_md.write_text(build_markdown(summary), encoding="utf-8")
    return summary_json, summary_md


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("run_manifest", type=Path)
    parser.add_argument("per_frame_csv", type=Path)
    parser.add_argument("renderer_telemetry_log", type=Path)
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None,
        help="directory for summary.json and summary.md (default: manifest directory)",
    )
    args = parser.parse_args()
    try:
        summary_json, summary_md = summarize(
            args.run_manifest,
            args.per_frame_csv,
            args.renderer_telemetry_log,
            args.output_dir or args.run_manifest.parent,
        )
    except SummaryError as error:
        parser.error(str(error))
    print(f"summary_json={summary_json}")
    print(f"summary_md={summary_md}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
