#!/usr/bin/env python3
"""Contract tests for Windows crash symbolization fields (S79-9)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
HANDLER = REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeWindowsCrashHandler.cpp"
WORKFLOW = REPO_ROOT / ".github/workflows/sapphire-vendor-parity.yml"


class WindowsCrashSymbolizationContract(unittest.TestCase):
    def test_crash_report_includes_module_base_and_registers(self) -> None:
        text = HANDLER.read_text(encoding="utf-8")
        self.assertIn("exception.moduleBase=", text)
        self.assertIn("exceptionRva=", text)
        self.assertIn("registers.rip=", text)

    def test_sanitizer_matrix_runs_regression_gate(self) -> None:
        text = WORKFLOW.read_text(encoding="utf-8")
        self.assertIn("test_sapphire_vulkan_cold_start_regression_s79.py", text)
        self.assertNotIn("if: matrix.sanitizer == 'asan-ubsan'", text)


if __name__ == "__main__":
    unittest.main()
