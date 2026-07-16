#!/usr/bin/env python3
"""Contract test for single HBlank GPU2D path latch (S79-3)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GPU_CPP = REPO_ROOT / "src/GPU.cpp"


class HBlankGpu2DPathLatchContract(unittest.TestCase):
    def test_start_hblank_evaluates_gpu2d_path_once(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        match = re.search(
            r"void GPU::StartHBlank\(u32 line\) noexcept\s*\{([\s\S]*?)^}",
            text,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        body = match.group(1)
        self.assertIn("const bool useSapphire2D = UsesSapphireGpu2DPath();", body)
        self.assertEqual(body.count("UsesSapphireGpu2DPath()"), 1)
        self.assertGreaterEqual(body.count("useSapphire2D"), 8)


if __name__ == "__main__":
    unittest.main()
