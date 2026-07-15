#!/usr/bin/env python3
"""120-frame period-two flicker detection contract (S73-11)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class SapphireFlickerDetectionTests(unittest.TestCase):
    def test_sapphire_exact_is_default_pipeline_mode(self):
        mode = (REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeSapphirePipelineMode.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("SapphireExact", mode)
        self.assertIn("return SapphirePipelineMode::SapphireExact;", mode)

    def test_temporal_is_end_to_end_gated(self):
        session = (
            REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp"
        ).read_text(encoding="utf-8")
        latch = (REPO_ROOT / "src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp").read_text(
            encoding="utf-8"
        )
        self.assertIn("sapphireTemporalEnabled()", session)
        self.assertIn("frameLatch.setTemporalEnabled(sapphireTemporalEnabled());", session)
        self.assertIn("if (!temporalEnabled_)", latch)

    def test_no_partial_current_frame_only_default(self):
        repo = REPO_ROOT / "src/frontend/qt_sdl"
        repair = repo / "MelonPrimeDesktop2DRepairMode.h"
        if repair.is_file():
            text = repair.read_text(encoding="utf-8")
            self.assertNotIn("return Desktop2DRepairMode::CurrentFrameOnly;", text)
        latch = (repo / "SapphireVulkanFrameLatch.cpp").read_text(encoding="utf-8")
        self.assertNotIn("desktop2DTemporalRepairEnabled()", latch)

    def test_frame_identity_fields_available_for_diagnostics(self):
        session = (
            REPO_ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp"
        ).read_text(encoding="utf-8")
        for token in (
            "preparedFrameScreenSwap",
            "frameSerial",
            "rendererGeneration",
            "frontBuffer",
        ):
            self.assertIn(token, session)


if __name__ == "__main__":
    unittest.main()
