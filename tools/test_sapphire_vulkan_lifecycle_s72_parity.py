#!/usr/bin/env python3
"""Static contract tests for Sapphire Vulkan 2D temporal repair parity (S72)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleS72ParityTests(unittest.TestCase):
    def test_current_frame_only_is_default_repair_mode(self):
        header = read_repo("src/frontend/qt_sdl/MelonPrimeDesktop2DRepairMode.h")
        self.assertIn("enum class Desktop2DRepairMode", header)
        self.assertIn("CurrentFrameOnly", header)
        self.assertIn("return Desktop2DRepairMode::CurrentFrameOnly;", header)

    def test_temporal_repairs_are_gated_by_repair_mode(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("const bool temporalRepairEnabled = desktop2DTemporalRepairEnabled();", latch)
        self.assertIn("if (!temporalRepairEnabled)", latch)
        self.assertIn("invalidateAll2DTemporalSources();", latch)

    def test_capture_repair_uses_present2d_not_visible_color(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        repair = re.search(
            r"auto repairVramCapturePrimaryFromCaptureSource\s*=[\s\S]*?return repairedLines;\s*\};",
            latch,
        )
        self.assertIsNotNone(repair)
        body = repair.group(0)
        self.assertIn("packedLineHasPresent2D(plane0, plane1, control, y)", body)
        self.assertNotIn("packedLineHasAnyVisibleColor(plane0, y)", body)

    def test_capture_source_is_tagged_with_physical_screen_and_engine(self):
        output = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanOutput.h")
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("struct Capture3DSourceSnapshot", output)
        self.assertIn("topCapture3dSource", output)
        self.assertIn("bottomCapture3dSource", output)
        self.assertIn("taggedCapture.physicalScreen", latch)
        self.assertIn("captureSourceMatchesTarget(", latch)

    def test_previous_carry_validates_physical_screen_engine(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("canCarryPreviousPhysicalScreen(", latch)
        carry = re.search(
            r"auto carryPreviousLatchedScreenLines\s*=[\s\S]*?return carriedLines;\s*\};",
            latch,
        )
        self.assertIsNotNone(carry)
        self.assertIn("canCarryPreviousPhysicalScreen(", carry.group(0))

    def test_2d_repair_does_not_use_render3d_alternating_mode_directly(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("const bool isIn3DTemporalAlternatingMode", latch)
        self.assertNotIn("const bool screenSwapToggledThisFrame", latch)
        self.assertNotIn("isInAlternatingMode", latch)

    def test_engine_a_whole_screen_replacement_is_legacy_only(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("legacyHeuristicRepairEnabled && !renderer2dDebugControlsActive", latch)
        self.assertIn("shouldReplaceBottom = cachedEngineABottomValid", latch)
        self.assertIn("&& legacyHeuristicRepairEnabled", latch)

    def test_cached_line_meta_is_not_mixed_with_current_low_bits(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        apply = re.search(
            r"auto applyCachedEngineASnapshot\s*=[\s\S]*?^\s*\};",
            latch,
            re.MULTILINE,
        )
        self.assertIsNotNone(apply)
        self.assertNotIn("0x0000FFFFu", apply.group(0))

    def test_hard_coded_bottom_row_black_repair_removed(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertNotIn("for (int y = 171; y < kScreenshotScreenHeight; y++)", latch)

    def test_s72_parity_test_is_wired_in_ci(self):
        workflow = read_repo(".github/workflows/sapphire-vendor-parity.yml")
        self.assertIn("tools/test_sapphire_vulkan_lifecycle_s72_parity.py", workflow)


if __name__ == "__main__":
    unittest.main()
