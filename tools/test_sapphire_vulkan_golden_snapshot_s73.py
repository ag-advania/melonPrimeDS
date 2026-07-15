#!/usr/bin/env python3
"""Golden snapshot contract for Sapphire Vulkan parity (S74-11)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HEADER = REPO_ROOT / "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h"
SIDECAR = REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeDesktopSapphireFrameSidecar.h"


class SapphireVulkanGoldenSnapshotTests(unittest.TestCase):
    def test_snapshot_abi_has_upstream_fields_only(self):
        header = HEADER.read_text(encoding="utf-8")
        self.assertIn("capture3dSourceDsFrame", header)
        self.assertNotIn("topCapture3dSource", header)

    def test_sidecar_is_diagnostic_only(self):
        sidecar = SIDECAR.read_text(encoding="utf-8")
        self.assertIn("publishedFrontBuffer", sidecar)
        self.assertIn("liveFrontBuffer", sidecar)
        self.assertNotIn("topCapture3dSource", sidecar)
        self.assertNotIn("physicalTopEngine", sidecar)


if __name__ == "__main__":
    unittest.main()
