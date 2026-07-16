#!/usr/bin/env python3
"""Executable Vulkan cold-start integration contract (S76-9)."""

from __future__ import annotations

import os
import subprocess
import sys
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanColdStartS76Tests(unittest.TestCase):
    def test_cold_start_logging_contract_is_present(self):
        session = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp")
        self.assertIn("[VulkanColdStart]", session)
        self.assertIn("commit frameId=", session)
        self.assertIn("defer frameId=", session)
        self.assertIn("reject reason=", session)

    def test_completed_tuple_builder_is_used(self):
        session = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp")
        self.assertIn("BuildCompletedSapphireFrameTuple", session)
        self.assertIn("BuildDesktopSapphireFrameInput", session)

    def test_executable_launcher_when_binary_available(self):
        candidates = [
            REPO_ROOT / "build" / "release-mingw-x86_64" / "melonPrimeDS.exe",
            REPO_ROOT / "build" / "sapphire-parity-linux-Release" / "melonDS",
        ]
        binary = next((path for path in candidates if path.is_file()), None)
        if binary is None:
            self.skipTest("production Vulkan binary unavailable")

        env = os.environ.copy()
        env["QT_QPA_PLATFORM"] = env.get("QT_QPA_PLATFORM", "offscreen")
        proc = subprocess.run(
            [str(binary), "--help"],
            cwd=REPO_ROOT,
            env=env,
            text=True,
            capture_output=True,
            timeout=30,
            check=False,
        )
        self.assertIn(proc.returncode, {0, 1})
        combined = proc.stdout + proc.stderr
        self.assertTrue(
            "melon" in combined.lower() or "usage" in combined.lower() or proc.returncode == 0,
            msg=combined,
        )


if __name__ == "__main__":
    unittest.main()
