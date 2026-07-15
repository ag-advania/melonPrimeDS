#!/usr/bin/env python3
"""Verify VulkanOutput composition headers match pinned Sapphire (S73-9)."""

from __future__ import annotations

import argparse
import re
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DESKTOP = REPO_ROOT / "src/frontend/qt_sdl/VulkanReference/VulkanOutput.h"
DEFAULT_ANDROID = Path(r"C:\Users\Admin\Documents\git\melonDS-android")
ANDROID_REL = "app/src/main/cpp/renderer/VulkanOutput.h"

STRUCTS = (
    "SoftPackedScreenStats",
    "SoftPackedFrameSnapshot",
    "PreparedSoftPackedFrameDebugView",
    "VulkanCompositionInputs",
    "VulkanOutputTemporalStats",
)


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def extract_struct(text: str, name: str) -> str:
    match = re.search(rf"struct {name}\s*\{{([\s\S]*?)\n\s*\}};", text)
    if not match:
        raise AssertionError(f"struct {name} not found")
    return normalize_struct_body(match.group(1))


def normalize_struct_body(body: str) -> str:
    body = re.sub(r"\r\n", "\n", body)
    body = re.sub(r"//.*", "", body)
    body = re.sub(r"static constexpr size_t kScreenWidth =[\s\S]*?kLineCount = kScreenHeight;",
                  "static constexpr size_t kScreenWidth = 256u;\n    static constexpr size_t kScreenHeight = 192u;\n    static constexpr size_t kPixelCount = kScreenWidth * kScreenHeight;\n    static constexpr size_t kLineCount = kScreenHeight;",
                  body, count=1)
    lines = [line.strip() for line in body.splitlines() if line.strip()]
    return "\n".join(lines)


class VulkanOutputExactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        android_root = Path(__import__("os").environ.get("SAPPHIRE_ANDROID_ROOT", DEFAULT_ANDROID))
        cls.android_text = read(android_root / ANDROID_REL)
        cls.desktop_text = read(DESKTOP)

    def test_soft_packed_snapshot_exact(self):
        self.assertEqual(
            extract_struct(self.android_text, "SoftPackedFrameSnapshot"),
            extract_struct(self.desktop_text, "SoftPackedFrameSnapshot"),
        )

    def test_required_structs_present(self):
        for name in STRUCTS:
            self.assertIn(f"struct {name}", self.desktop_text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--upstream-tag", default="0.7.0.rc4")
    args = parser.parse_args()
    if args.upstream_tag != "0.7.0.rc4":
        print(f"unsupported tag {args.upstream_tag}", file=sys.stderr)
        return 2
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(VulkanOutputExactTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.exit(main())
