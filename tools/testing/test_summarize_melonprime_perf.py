#!/usr/bin/env python3
"""Regression tests for the MelonPrime performance log summarizer."""

from __future__ import annotations

import importlib.util
import io
import sys
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[1] / "perf" / "summarize-melonprime-perf.py"
SPEC = importlib.util.spec_from_file_location("summarize_melonprime_perf", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


class SummarizeMelonPrimePerfTests(unittest.TestCase):
    def test_current_scoped_frame_and_shutdown_records(self) -> None:
        report = MODULE.parse_lines([
            "[MelonPrimePerf] frame_ms session_id=missing instance_id=1 "
            "report_seq=4 p50=16.1 p95=17.2 p99=18.3 max=19.4 n=60\n",
            "[MelonPrimePerf] shutdown summary session_id=missing instance_id=1 "
            "report_seq=5: frames=600 frame_ms p50=16.2 p95=17.3 "
            "p99=18.4 max=20.5\n",
        ])

        self.assertEqual(len(report.windows), 1)
        self.assertEqual(report.windows[0].n, 60)
        self.assertIsNotNone(report.shutdown)
        self.assertEqual(report.shutdown.n, 600)

    def test_legacy_frame_and_shutdown_records_remain_supported(self) -> None:
        report = MODULE.parse_lines([
            "[MelonPrimePerf] frame_ms p50=16.1 p95=17.2 p99=18.3 "
            "max=19.4 n=60\n",
            "[MelonPrimePerf] shutdown summary: frames=600 frame_ms "
            "p50=16.2 p95=17.3 p99=18.4 max=20.5\n",
        ])

        self.assertEqual(len(report.windows), 1)
        self.assertIsNotNone(report.shutdown)

    def test_native_paint_windows_are_parsed_and_reported(self) -> None:
        report = MODULE.parse_lines([
            "[MelonPrimePerf] native_paint_us instance_id=1 "
            "render_lock_wait[n=2 p50=0.1 p95=0.2 p99=0.3 max=0.4] "
            "render_lock_hold[n=2 p50=10.0 p95=11.0 p99=12.0 max=13.0] "
            "framebuffer_copy[n=2 p50=4.0 p95=5.0 p99=6.0 max=7.0] "
            "qpaint_game[n=2 p50=2.0 p95=3.0 p99=4.0 max=5.0] "
            "hud_software[n=2 p50=1.0 p95=2.0 p99=3.0 max=4.0]\n",
        ])

        self.assertEqual(len(report.native_paint), 1)
        self.assertEqual(report.native_paint[0].instance_id, 1)
        self.assertEqual(
            report.native_paint[0].metrics["render_lock_wait"].p99, 0.3)

        output = io.StringIO()
        MODULE.print_report(report, output)
        self.assertIn("native paint 1 Hz windows: 1", output.getvalue())
        self.assertIn("render_lock_wait", output.getvalue())

    def test_screen_input_and_transition_windows_are_parsed(self) -> None:
        report = MODULE.parse_lines([
            "[MelonPrimePerf] screen_input instance_id=2 "
            "mouseMoveEvents=8000 eventSamples=8000 "
            "eventDroppedOrOverwritten=0 eventHistogramSaturated=0 "
            "event_ns[n=4096 p50=120.0 p95=180.0 "
            "p99=220.0 max=500.0] hudEditFastRejected=7990 "
            "hudEditHelperEntered=10 uiSnapshotRead=0 stylusPointerPublish=0\n",
            "[MelonPrimePerf] renderer_transition backend=vulkan instance_id=2 "
            "registry_lock_wait[n=1 p50=0.2 p95=0.2 p99=0.2 max=0.2] "
            "quiesce_duration[n=1 p50=120.0 p95=120.0 p99=120.0 max=120.0] "
            "transition_total[n=1 p50=140.0 p95=140.0 p99=140.0 max=140.0]\n",
        ])

        self.assertEqual(len(report.screen_input), 1)
        self.assertEqual(report.screen_input[0].mouse_move_events, 8000)
        self.assertEqual(report.screen_input[0].event_samples, 8000)
        self.assertEqual(
            report.screen_input[0].event_dropped_or_overwritten, 0)
        self.assertEqual(report.screen_input[0].event_histogram_saturated, 0)
        self.assertEqual(report.screen_input[0].event_p99_ns, 220.0)
        self.assertEqual(len(report.renderer_transition), 1)
        self.assertEqual(
            report.renderer_transition[0].metrics["transition_total"].p99,
            140.0)

        # Pre-histogram reports carried eventSamples and the overwrite count
        # but had no saturation field. Keep those historical logs parseable.
        legacy = MODULE.parse_lines([
            "[MelonPrimePerf] screen_input instance_id=3 "
            "mouseMoveEvents=64 eventSamples=64 "
            "eventDroppedOrOverwritten=0 "
            "event_ns[n=64 p50=120.0 p95=180.0 p99=220.0 max=500.0] "
            "hudEditFastRejected=0 hudEditHelperEntered=0 "
            "uiSnapshotRead=0 stylusPointerPublish=0\n",
        ])
        self.assertEqual(legacy.screen_input[0].event_histogram_saturated, 0)

        output = io.StringIO()
        MODULE.print_report(report, output)
        self.assertIn("screen input 1 Hz windows: 1", output.getvalue())
        self.assertIn("renderer transition windows: 1", output.getvalue())


if __name__ == "__main__":
    unittest.main()
