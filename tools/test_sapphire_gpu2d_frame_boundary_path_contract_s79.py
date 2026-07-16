#!/usr/bin/env python3
"""Contract tests for frame-boundary GPU2D path activation (S79-5)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class FrameBoundaryGpu2DPathContract(unittest.TestCase):
    def test_gpu_owns_active_gpu2d_path(self) -> None:
        gpu_h = (REPO_ROOT / "src/GPU.h").read_text(encoding="utf-8")
        gpu = (REPO_ROOT / "src/GPU.cpp").read_text(encoding="utf-8")
        self.assertIn("enum class GPU2DExecutionPath", gpu_h)
        self.assertIn("ActiveGPU2DPath", gpu_h)
        self.assertIn("ActiveGPU2DPath = GPU2DExecutionPath::SapphireCanonical", gpu)
        self.assertIn("ActiveGPU2DPath = GPU2DExecutionPath::LegacyOuterRenderer", gpu)

    def test_start_hblank_does_not_call_runtime_gate_helper(self) -> None:
        gpu = (REPO_ROOT / "src/GPU.cpp").read_text(encoding="utf-8")
        match = re.search(
            r"void GPU::StartHBlank\(u32 line\) noexcept\s*\{([\s\S]*?)^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        body = match.group(1)
        self.assertNotIn("UsesSapphireGpu2DPath()", body)
        self.assertIn("IsSapphireCanonicalGpu2DActive()", body)


if __name__ == "__main__":
    unittest.main()
