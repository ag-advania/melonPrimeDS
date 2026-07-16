#!/usr/bin/env python3
"""Contract tests for MelonPrime build identity embedding (S79-1)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class MelonPrimeBuildIdentityContract(unittest.TestCase):
    def test_cmake_embeds_full_git_identity(self) -> None:
        text = (REPO_ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("MelonPrimeGitBuildIdentity.h.in", text)
        self.assertIn("rev-parse HEAD", text)
        self.assertIn("status --porcelain", text)
        self.assertIn("MELONPRIME_GIT_COMMIT_FULL", text)
        self.assertIn("MELONPRIME_GIT_DIRTY", text)

    def test_build_identity_log_includes_required_fields(self) -> None:
        text = (REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeDesktopVulkanPresenter.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("commitFull=", text)
        self.assertIn("branch=", text)
        self.assertIn("dirty=", text)

    def test_run_identity_is_logged_at_startup(self) -> None:
        main_cpp = (REPO_ROOT / "src/frontend/qt_sdl/main.cpp").read_text(encoding="utf-8")
        run_cpp = (REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeRunIdentity.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("initRunIdentity()", main_cpp)
        self.assertIn("[RunIdentity] runId=", run_cpp)

    def test_crash_artifacts_use_run_id_filenames(self) -> None:
        text = (REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeWindowsCrashHandler.cpp").read_text(
            encoding="utf-8"
        )
        self.assertRegex(text, r"melonPrimeDS-%s-run-%llu\.dmp")
        self.assertIn("exception.moduleBase=", text)
        self.assertIn("exceptionRva=", text)
        self.assertIn("binarySha256=", text)

    def test_artifact_helpers_pair_by_run_id(self) -> None:
        text = (REPO_ROOT / "tools/sapphire_cold_start_artifacts.py").read_text(encoding="utf-8")
        self.assertIn("run_id", text)
        self.assertIn("extract_run_id", text)
        self.assertRegex(text, r"run-\{run_id\}")


if __name__ == "__main__":
    unittest.main()
