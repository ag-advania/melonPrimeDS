#!/usr/bin/env python3
"""Verify VulkanOutput.h struct ordering and Sapphire snapshot ABI sanity (S73-1)."""

from __future__ import annotations

import re
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HEADER = REPO_ROOT / "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h"


def read_header() -> str:
    return HEADER.read_text(encoding="utf-8")


class VulkanOutputHeaderCompileTests(unittest.TestCase):
    def test_capture_snapshot_not_used_before_definition(self):
        text = read_header()
        snapshot_pos = text.find("struct SoftPackedFrameSnapshot")
        capture_pos = text.find("struct Capture3DSourceSnapshot")
        self.assertNotEqual(snapshot_pos, -1, "SoftPackedFrameSnapshot missing")
        if capture_pos != -1:
            self.assertLess(
                capture_pos,
                snapshot_pos,
                "Capture3DSourceSnapshot must be defined before SoftPackedFrameSnapshot "
                "when embedded by value",
            )

    def test_soft_packed_snapshot_has_required_sapphire_fields(self):
        text = read_header()
        match = re.search(
            r"struct SoftPackedFrameSnapshot\s*\{([\s\S]*?)\n\s*void clear\(\)",
            text,
        )
        self.assertIsNotNone(match, "SoftPackedFrameSnapshot body missing")
        body = match.group(1)
        for field in (
            "frameId",
            "frontBufferLatched",
            "screenSwapLatched",
            "valid",
            "hasCapture3dSource",
            "captureBackedClass4Only",
            "packedTopPlane0",
            "capture3dSourceDsFrame",
            "topScreenStats",
        ):
            self.assertIn(field, body, f"missing Sapphire field {field}")

    def test_soft_packed_snapshot_has_no_desktop_only_fields(self):
        text = read_header()
        match = re.search(
            r"struct SoftPackedFrameSnapshot\s*\{([\s\S]*?)\n\s*void clear\(\)",
            text,
        )
        self.assertIsNotNone(match)
        body = match.group(1)
        for field in (
            "sourceFrameSerial",
            "rendererGeneration",
            "hardwareScreenSwapLatched",
            "renderScreenSwapAt3DLatched",
            "topEngineLatched",
            "bottomEngineLatched",
            "topCapture3dSource",
            "bottomCapture3dSource",
            "Capture3DSourceSnapshot",
        ):
            self.assertNotIn(field, body, f"desktop-only field still in snapshot: {field}")


def main() -> int:
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(
        VulkanOutputHeaderCompileTests
    )
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
