#!/usr/bin/env python3
"""Golden snapshot contract tests for Sapphire desktop parity (S73-10)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class SapphireGoldenSnapshotTests(unittest.TestCase):
    def test_snapshot_abi_is_upstream_exact(self):
        header = (REPO_ROOT / "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("struct SoftPackedFrameSnapshot", header)
        self.assertNotIn("topCapture3dSource", header)
        self.assertNotIn("hardwareScreenSwapLatched", header)

    def test_sidecar_holds_desktop_validation_metadata(self):
        sidecar = (
            REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeDesktopSapphireFrameSidecar.h"
        ).read_text(encoding="utf-8")
        self.assertIn("struct DesktopSapphireFrameSidecar", sidecar)
        self.assertIn("physicalTopEngine", sidecar)
        self.assertIn("topCapture3dSource", sidecar)

    def test_frame_input_adapter_exists(self):
        adapter = (
            REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeSapphireFrameInput.h"
        ).read_text(encoding="utf-8")
        self.assertIn("struct SapphireFrameInput", adapter)
        self.assertIn("BuildDesktopSapphireFrameInput", adapter)


if __name__ == "__main__":
    unittest.main()
