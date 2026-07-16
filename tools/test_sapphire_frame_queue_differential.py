#!/usr/bin/env python3
"""FrameQueue upstream-vs-Desktop differential sequence tests (S75-6)."""

from __future__ import annotations

import re
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HARNESS_CPP = REPO_ROOT / "src" / "frontend" / "qt_sdl" / "tests" / "FrameQueueDifferentialHarness.cpp"
FRAME_QUEUE_CPP = REPO_ROOT / "src" / "frontend" / "qt_sdl" / "VulkanReference" / "FrameQueue.cpp"
BUILD_DIR = REPO_ROOT / "build" / "release-mingw-x86_64"


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireFrameQueueDifferentialTests(unittest.TestCase):
    def test_harness_source_defines_complete_and_defer_cases(self):
        text = HARNESS_CPP.read_text(encoding="utf-8")
        self.assertIn("test_complete_lifetime_render_present_parity", text)
        self.assertIn("test_first_present_failure_injection_recovers", text)
        self.assertIn("test_core_and_wrapper_agree_when_present_queue_empty", text)

    def test_wrapper_does_not_retry_core_on_render_block(self):
        body = read_repo("src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp")
        get_render = re.search(
            r"Frame\* FrameQueue::getRenderFrame\([\s\S]*?^}",
            body,
            re.MULTILINE,
        )
        self.assertIsNotNone(get_render)
        block = get_render.group(0)
        self.assertEqual(block.count("impl_->core.getRenderFrame"), 1)
        self.assertIn("undoRenderAcquisition", block)
        self.assertIn("return nullptr", block)

    def test_wrapper_does_not_retry_core_on_present_candidate_block(self):
        body = read_repo("src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp")
        get_candidate = re.search(
            r"Frame\* FrameQueue::getPresentCandidate\([\s\S]*?^}",
            body,
            re.MULTILINE,
        )
        self.assertIsNotNone(get_candidate)
        block = get_candidate.group(0)
        self.assertEqual(block.count("impl_->core.getPresentCandidate"), 1)
        self.assertIn("allowPresentationAcquisition", block)
        self.assertIn("return nullptr", block)

    def test_generated_core_regions_match_manifest(self):
        proc = subprocess.run(
            [sys.executable, str(REPO_ROOT / "tools" / "generate_sapphire_frame_queue.py"), "--verify"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)

    def test_harness_binary_when_available(self):
        cmake = Path("C:/msys64/mingw64/bin/cmake.exe")
        if not cmake.is_file() or not BUILD_DIR.is_dir():
            self.skipTest("local MinGW build tree unavailable")
        build = subprocess.run(
            [
                str(cmake),
                "--build",
                str(BUILD_DIR),
                "--target",
                "sapphire_frame_queue_differential_test",
                "-j",
                "1",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        self.assertEqual(build.returncode, 0, msg=build.stdout + build.stderr)
        binary = BUILD_DIR / "sapphire_frame_queue_differential_test.exe"
        if not binary.is_file():
            self.skipTest("differential harness binary not built")
        if cmake.is_file():
            proc = subprocess.run(
                [
                    "C:/msys64/usr/bin/bash.exe",
                    "-lc",
                    f"export PATH=/mingw64/bin:$PATH; "
                    f"cd /c/Users/Admin/Documents/git/melonPrimeDS/build/release-mingw-x86_64 && "
                    f"./sapphire_frame_queue_differential_test.exe",
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                check=False,
            )
        else:
            proc = subprocess.run([str(binary)], cwd=REPO_ROOT, text=True, capture_output=True, check=False)
        self.assertEqual(proc.returncode, 0, msg=proc.stdout + proc.stderr)
        self.assertIn("FrameQueue differential harness OK", proc.stdout)


if __name__ == "__main__":
    unittest.main()
