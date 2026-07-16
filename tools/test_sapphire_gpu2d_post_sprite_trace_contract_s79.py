#!/usr/bin/env python3
"""Contract test for post-sprite HBlank boundary traces (S79-2)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GPU_CPP = REPO_ROOT / "src/GPU.cpp"


class PostSpriteHBlankTraceContract(unittest.TestCase):
    def test_start_hblank_has_post_sprite_gate_traces(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        required = [
            "[FirstGpuLine] after sprites Sapphire2D=",
            "[FirstGpuLine] before path recheck",
            "[FirstGpuLine] after path recheck result=",
            "[FirstGpuLine] before CheckDMAs HBlank",
            "[FirstGpuLine] after CheckDMAs HBlank",
            "[FirstGpuLine] before IRQ HBlank",
            "[FirstGpuLine] after IRQ HBlank",
            "[FirstGpuLine] before ScheduleEvent HBlank",
            "[FirstGpuLine] after ScheduleEvent HBlank",
        ]
        for marker in required:
            with self.subTest(marker=marker):
                self.assertIn(marker, text)


if __name__ == "__main__":
    unittest.main()
