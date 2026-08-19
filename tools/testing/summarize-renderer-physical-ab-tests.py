#!/usr/bin/env python3
"""Focused contract test for the physical A/B deterministic summarizer."""

from __future__ import annotations

import csv
import importlib.util
import json
import tempfile
from pathlib import Path


def load_summarizer():
    path = Path(__file__).with_name("summarize-renderer-physical-ab.py")
    spec = importlib.util.spec_from_file_location("physical_ab_summarizer", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load summarizer: {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    summarizer = load_summarizer()
    with tempfile.TemporaryDirectory(prefix="melonprime-physical-ab-") as temp:
        root = Path(temp)
        manifest_path = root / "run.run-manifest.json"
        frame_path = root / "run.frames.csv"
        latency_path = root / "run.csv"
        telemetry_path = root / "run.telemetry.log"
        output_dir = root / "summary"

        manifest_path.write_text(
            json.dumps(
                {
                    "schema_version": 1,
                    "artifact_type": "renderer_physical_ab_run_manifest",
                    "run_id": "contract-test",
                    "clock": {"authority": "test-qpc", "frequency": 1_000_000},
                    "phases": {
                        "measurement_start": {"monotonic_ticks": 100},
                        "measurement_end": {"monotonic_ticks": 300},
                    },
                    "conditions": {"renderer": "Vulkan"},
                    "provenance": {"provenance_verified": True},
                    "artifacts": {"vulkan_latency_csv": str(latency_path)},
                }
            ),
            encoding="utf-8",
        )

        with frame_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=[
                    "run_id",
                    "frame_index",
                    "frame_start_ticks",
                    "frame_end_ticks",
                    "qpc_frequency",
                    "frame_time_us",
                    "input_sample_to_runframe_begin_us",
                    "input_sample_to_present_end_us",
                ],
            )
            writer.writeheader()
            for index, (end, frame_time) in enumerate(((90, 1.0), (150, 2.0), (250, 4.0), (310, 8.0))):
                writer.writerow(
                    {
                        "run_id": "contract-test",
                        "frame_index": index,
                        "frame_start_ticks": end - 10,
                        "frame_end_ticks": end,
                        "qpc_frequency": 1_000_000,
                        "frame_time_us": frame_time,
                        "input_sample_to_runframe_begin_us": 1.0,
                        "input_sample_to_present_end_us": 5.0,
                    }
                )

        with latency_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.DictWriter(
                stream,
                fieldnames=[
                    "present_end_qpc_ticks",
                    "qpc_frequency",
                    "acquire_wait_us",
                    "input_sample_time_us",
                    "present_end_time_us",
                    "render_submit_start_time_us",
                    "render_submit_end_time_us",
                ],
            )
            writer.writeheader()
            for end, wait in ((150, 2.0), (250, 4.0)):
                writer.writerow(
                    {
                        "present_end_qpc_ticks": end,
                        "qpc_frequency": 1_000_000,
                        "acquire_wait_us": wait,
                        "input_sample_time_us": 1.0,
                        "present_end_time_us": 6.0,
                        "render_submit_start_time_us": 2.0,
                        "render_submit_end_time_us": 3.0,
                    }
                )

        telemetry_path.write_text(
            "\n".join(
                [
                    "[MelonPrimePerfPhase] report_qpc_ticks=50 qpc_frequency=1000000",
                    "[VulkanPerf] cpu scale=4 name=raster_reuse_wait_us p50_us=99.00 p95_us=99.00 p99_us=99.00 max_us=99.00 n=10",
                    "[MelonPrimePerfPhase] report_qpc_ticks=150 qpc_frequency=1000000",
                    "[VulkanPerf] cpu scale=4 name=raster_reuse_wait_us p50_us=2.00 p95_us=3.00 p99_us=3.50 max_us=4.00 n=10",
                    "[VulkanPerf] counters scale=4 frames=10 raster_gpu_ns=1000 texture_materialize_count=3 texture_materialize_retry_success_count=1",
                    "[MelonPrimePerfPhase] report_qpc_ticks=250 qpc_frequency=1000000",
                    "[VulkanPerf] cpu scale=4 name=raster_reuse_wait_us p50_us=4.00 p95_us=5.00 p99_us=5.50 max_us=6.00 n=20",
                    "[VulkanPerf] counters scale=4 frames=20 raster_gpu_ns=3000 texture_materialize_count=5 texture_materialize_retry_success_count=2",
                ]
            )
            + "\n",
            encoding="utf-8",
        )

        summary_json, summary_md = summarizer.summarize(
            manifest_path, frame_path, telemetry_path, output_dir
        )
        summary = json.loads(summary_json.read_text(encoding="utf-8"))
        assert summary["phase_selection"]["input_frame_rows"] == 4
        assert summary["phase_selection"]["selected_frame_samples"] == 2
        assert summary["phase_selection"]["selected_report_markers"] == 2
        assert summary["frame_time_us"]["p50"] == 3.0
        assert summary["metrics"]["raster_reuse_wait_us"]["p50"] == 3.0
        assert summary["metrics"]["raster_gpu_time_ns"]["p50"] == 125.0
        assert summary["metrics"]["texture_materialize_count"]["sum"] == 8
        assert summary["vulkan_raster_gpu_vs_reuse_wait"]["pair_count"] == 2
        assert summary_md.is_file()

    print("PASS: physical A/B summarizer deterministic interval and GPU fallback test")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
