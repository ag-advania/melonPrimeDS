#!/usr/bin/env python3
"""Contract test for Windows crash handler (S77-2)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HANDLER_CPP = REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeWindowsCrashHandler.cpp"
MAIN_CPP = REPO_ROOT / "src/frontend/qt_sdl/main.cpp"
CMAKE = REPO_ROOT / "src/frontend/qt_sdl/CMakeLists.txt"


class MelonPrimeWindowsCrashHandlerContract(unittest.TestCase):
    def test_minidump_and_stack_symbols_present(self) -> None:
        text = HANDLER_CPP.read_text(encoding="utf-8")
        self.assertIn("MiniDumpWriteDump", text)
        self.assertIn("StackWalk64", text)
        self.assertIn("MELONPRIME_GIT_COMMIT", text)
        self.assertIn(".crash.txt", text)

    def test_main_installs_handler(self) -> None:
        text = MAIN_CPP.read_text(encoding="utf-8")
        self.assertIn("installWindowsCrashHandler", text)

    def test_cmake_links_dbghelp(self) -> None:
        text = CMAKE.read_text(encoding="utf-8")
        self.assertIn("MelonPrimeWindowsCrashHandler.cpp", text)
        self.assertIn("dbghelp", text)


if __name__ == "__main__":
    unittest.main()
