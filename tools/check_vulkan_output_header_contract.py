#!/usr/bin/env python3
"""Contract test for VulkanOutput composition headers (S74-11)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HEADER = REPO_ROOT / "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h"


class VulkanOutputHeaderContractTests(unittest.TestCase):
    def test_required_snapshot_fields_present(self):
        text = HEADER.read_text(encoding="utf-8")
        for field in (
            "packedTopPlane0",
            "packedTopPlane1",
            "packedTopControl",
            "packedTopLineMeta",
            "packedBottomPlane0",
            "packedBottomPlane1",
            "packedBottomControl",
            "packedBottomLineMeta",
            "capture3dSourceDsFrame",
            "captureLineUses3dMask",
            "captureFallbackLines",
            "captureBackedClass4Only",
            "topScreenStats",
            "bottomScreenStats",
        ):
            self.assertIn(field, text)

    def test_sidecar_fields_are_not_in_snapshot(self):
        text = HEADER.read_text(encoding="utf-8")
        self.assertNotIn("topCapture3dSource", text)
        self.assertNotIn("physicalTopEngine", text)


if __name__ == "__main__":
    unittest.main()
