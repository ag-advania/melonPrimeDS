#!/usr/bin/env python3
"""Contract tests for SapphireGpu2DState removal from timing (S79-6)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GPU_CPP = REPO_ROOT / "src/GPU.cpp"


class SapphireGpu2DTimingSemanticsContract(unittest.TestCase):
    def test_gpu2d_event_loop_avoids_is_active_for_rendering(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        for fn_name in ("StartHBlank", "FinishFrame", "StartScanline", "DisplayFIFO"):
            match = re.search(
                rf"void GPU::{fn_name}\([^\)]*\)[^{{]*\{{([\s\S]*?)^}}",
                text,
                re.MULTILINE,
            )
            self.assertIsNotNone(match, msg=f"missing {fn_name}")
            body = match.group(1)
            with self.subTest(function=fn_name):
                self.assertNotIn("IsActiveForRendering", body)

    def test_uses_sapphire_path_no_longer_queries_gpu3d(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        match = re.search(
            r"bool GPU::UsesSapphireGpu2DPath\(\) const noexcept\s*\{([\s\S]*?)^}",
            text,
            re.MULTILINE,
        )
        self.assertIsNotNone(match)
        body = match.group(1)
        self.assertNotIn("GPU3D", body)
        self.assertNotIn("IsActiveForRendering", body)


if __name__ == "__main__":
    unittest.main()
