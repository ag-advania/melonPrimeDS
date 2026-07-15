#!/usr/bin/env python3
"""Verify production FrameLatch uses generated Sapphire core (S74)."""

from __future__ import annotations

import re
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
WRAPPER_CPP = REPO_ROOT / "src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp"
WRAPPER_H = REPO_ROOT / "src/frontend/qt_sdl/SapphireVulkanFrameLatch.h"
CORE_CPP = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/SapphireFrameLatchCore.cpp"
CMAKE = REPO_ROOT / "src/frontend/qt_sdl/CMakeLists.txt"


class SapphireFrameLatchParityTests(unittest.TestCase):
    def test_generated_core_is_in_production_cmake(self):
        text = CMAKE.read_text(encoding="utf-8")
        self.assertIn("SapphireGenerated/SapphireFrameLatchCore.cpp", text)
        self.assertIn("sapphire_frame_latch_core OBJECT", text)

    def test_wrapper_delegates_to_generated_core(self):
        wrapper = WRAPPER_CPP.read_text(encoding="utf-8")
        header = WRAPPER_H.read_text(encoding="utf-8")
        self.assertIn("SapphireFrameLatchCore core_", header)
        self.assertIn("core_.latchSoftPackedFrameSnapshot", wrapper)
        self.assertNotIn("packedPixelIsPresent2D", wrapper)
        self.assertNotIn("measureSoftPackedBlackContract", wrapper)
        self.assertNotIn("captureSourceMatchesTarget", wrapper)

    def test_generated_core_has_upstream_member_signatures(self):
        core = CORE_CPP.read_text(encoding="utf-8")
        self.assertIn("void SapphireFrameLatchCore::clearLatchedSoftPackedFrameSnapshot()", core)
        self.assertIn("bool SapphireFrameLatchCore::updateVulkanTemporal3dHistoryGate()", core)
        self.assertIn("bool SapphireFrameLatchCore::latchSoftPackedFrameSnapshot(", core)
        self.assertNotIn("MelonInstance::", core)

    def test_generator_verify_is_read_only(self):
        proc = subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools/generate_sapphire_frame_latch.py"), "--verify"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)


if __name__ == "__main__":
    unittest.main()
