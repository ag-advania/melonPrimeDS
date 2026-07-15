#!/usr/bin/env python3
"""Verify generated Sapphire FrameLatch core against pinned upstream (S73-3)."""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
MANIFEST = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/GENERATION_MANIFEST.json"
GENERATED_CPP = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/SapphireFrameLatchCore.cpp"
GENERATED_H = REPO_ROOT / "src/frontend/qt_sdl/SapphireGenerated/SapphireFrameLatchCore.h"


class SapphireFrameLatchParityTests(unittest.TestCase):
    def test_generation_manifest_exists(self):
        self.assertTrue(MANIFEST.is_file(), "run tools/generate_sapphire_frame_latch.py first")

    def test_generated_files_exist(self):
        self.assertTrue(GENERATED_CPP.is_file())
        self.assertTrue(GENERATED_H.is_file())

    def test_manifest_lists_required_functions(self):
        data = json.loads(MANIFEST.read_text(encoding="utf-8"))
        required = {
            "latchSoftPackedFrameSnapshot",
            "updateVulkanTemporal3dHistoryGate",
            "clearLatchedSoftPackedFrameSnapshot",
            "softPackedFramesAlternate3dOwner",
        }
        extracted = set(data.get("extractedFunctions", []))
        self.assertTrue(required.issubset(extracted))

    def test_generator_verify_passes(self):
        proc = subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools/generate_sapphire_frame_latch.py"), "--verify"],
            cwd=REPO_ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            proc.returncode,
            0,
            msg=proc.stdout + proc.stderr,
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream-tag", default="0.7.0.rc4")
    args = parser.parse_args()
    if args.upstream_tag != "0.7.0.rc4":
        print(f"unsupported upstream tag: {args.upstream_tag}", file=sys.stderr)
        return 2
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(SapphireFrameLatchParityTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
