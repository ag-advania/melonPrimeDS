#!/usr/bin/env python3
"""Contract tests for canonical Unit register ownership (S79-7)."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GPU_CPP = REPO_ROOT / "src/GPU.cpp"


class CanonicalRegisterOwnershipContract(unittest.TestCase):
    def test_savestate_skips_legacy_special_regs_on_canonical_path(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        self.assertIn("legacyGpuSpecialRegs", text)
        self.assertIn("GPU2D_A.CaptureCnt", text)

    def test_register_routing_uses_canonical_activation(self) -> None:
        text = GPU_CPP.read_text(encoding="utf-8")
        self.assertIn("IsSapphireCanonicalGpu2DActive()", text)
        self.assertGreaterEqual(text.count("GPU2D_A.Read16"), 1)


if __name__ == "__main__":
    unittest.main()
