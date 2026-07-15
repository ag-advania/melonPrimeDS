#!/usr/bin/env python3
"""Static contract tests for Sapphire Vulkan 2D black contract parity (S71)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleS71ParityTests(unittest.TestCase):
    def test_soft_packed_snapshot_splits_physical_and_render_ownership(self):
        header = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanOutput.h")
        self.assertIn("bool hardwareScreenSwapLatched = false;", header)
        self.assertIn("bool renderScreenSwapAt3DLatched = false;", header)
        self.assertIn("u32 topEngineLatched = UINT32_MAX;", header)
        self.assertIn("u32 bottomEngineLatched = UINT32_MAX;", header)

    def test_frame_latch_records_engine_metadata_from_publication(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("lastSoftPackedFrameSnapshot.hardwareScreenSwapLatched = published.hardwareScreenSwap;", latch)
        self.assertIn("lastSoftPackedFrameSnapshot.renderScreenSwapAt3DLatched = published.renderScreenSwapAt3D;", latch)
        self.assertIn("lastSoftPackedFrameSnapshot.topEngineLatched = published.top.engine;", latch)
        self.assertIn("lastSoftPackedFrameSnapshot.bottomEngineLatched = published.bottom.engine;", latch)

    def test_engine_a_physical_owner_uses_published_engine_metadata(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("engineAOwnsPhysicalTop(", latch)
        self.assertNotRegex(
            latch,
            r"const\s+bool\s+engineAOnTop\s*=\s*!\s*lastSoftPackedFrameSnapshot\.screenSwapLatched",
        )

    def test_physical_and_render_owner_transitions_are_separate(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("const bool render3DOwnerChanged =", latch)
        self.assertIn("const bool physical2DOwnerChanged =", latch)
        self.assertIn("previousSoftPackedFrameSnapshot.renderScreenSwapAt3DLatched", latch)
        self.assertIn("previousSoftPackedFrameSnapshot.topEngineLatched", latch)

    def test_present_2d_helpers_are_shared_and_used_for_cache_eligibility(self):
        contract = read_repo("src/frontend/qt_sdl/MelonPrimeDesktop2DBlackContract.h")
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        self.assertIn("bool packedPixelIsPresent2D(u32 pixel)", contract)
        self.assertIn("bool screenHasPresent2DContent(", contract)
        self.assertIn("#include \"MelonPrimeDesktop2DBlackContract.h\"", latch)
        self.assertIn("return screenHasPresent2DContent(plane0, plane1, control);", latch)

    def test_structured_2d_only_counts_opaque_black_and_protected_black(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        block = re.search(
            r"auto screenHasStructured2DOnlyContent\s*=[\s\S]*?return false;\s*\};",
            latch,
        )
        self.assertIsNotNone(block)
        body = block.group(0)
        self.assertIn("packedPixelIsPresent2D(plane0[i])", body)
        self.assertIn("packedControlMarksProtectedBlack2D(control[i])", body)
        self.assertNotIn("packedPixelHasVisibleColor(plane0[i])", body)

    def test_merge_structured_display_line_selects_actual_packed_2d_source(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        merge = re.search(
            r"auto mergeStructuredDisplayLine\s*=[\s\S]*?^\s*\};",
            latch,
            re.MULTILINE,
        )
        self.assertIsNotNone(merge)
        body = merge.group(0)
        self.assertIn("const u32 packed2D = packedPlane0Is2D", body)
        self.assertIn("plane1[index] = packed2D;", body)
        self.assertNotRegex(body, r"plane1\[index\]\s*=\s*packedPlane0;")

    def test_merge_preserves_protected_black_from_existing_control(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        merge = re.search(
            r"auto mergeStructuredDisplayLine\s*=[\s\S]*?^\s*\};",
            latch,
            re.MULTILINE,
        )
        self.assertIsNotNone(merge)
        body = merge.group(0)
        self.assertIn("((packedControlAlpha | structuredControlAlpha) & 0x20u)", body)

    def test_black_contract_validation_logs_before_snapshot_valid(self):
        latch = read_repo("src/frontend/qt_sdl/SapphireVulkanFrameLatch.cpp")
        valid_idx = latch.rfind("lastSoftPackedFrameSnapshot.valid = true;")
        self.assertNotEqual(valid_idx, -1)
        prefix = latch[:valid_idx]
        self.assertIn("[Vulkan2DBlackContract]", prefix)
        self.assertIn("measureSoftPackedBlackContract(", prefix)

    def test_manifest_tracks_frame_latch_vendor_closure(self):
        manifest = read_repo("src/frontend/qt_sdl/VulkanReference/SAPPHIRE_SOURCE_MANIFEST.md")
        self.assertIn("SapphireVulkanFrameLatch.cpp", manifest)
        self.assertIn("MelonInstance.cpp", manifest)

    def test_s71_parity_test_is_wired_in_ci(self):
        workflow = read_repo(".github/workflows/sapphire-vendor-parity.yml")
        self.assertIn("tools/test_sapphire_vulkan_lifecycle_s71_parity.py", workflow)


if __name__ == "__main__":
    unittest.main()
