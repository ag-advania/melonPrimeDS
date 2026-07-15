#!/usr/bin/env python3
"""Static contract tests for Sapphire GPU2D lifecycle parity (S66)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireGpu2DLifecycleParityTests(unittest.TestCase):
    def test_outer_renderer_action_preserved_on_vulkan_install(self):
        factory = read_repo("src/frontend/qt_sdl/MelonPrimeRendererFactory.cpp")
        header = read_repo("src/frontend/qt_sdl/MelonPrimeRendererFactory.h")
        emu = read_repo("src/frontend/qt_sdl/EmuThread.cpp")

        self.assertIn("enum class OuterRendererAction", header)
        self.assertIn("KeepCurrent", header)
        self.assertIn("OuterRendererAction::KeepCurrent", factory)
        self.assertIn("OuterRendererAction::Replace", factory)
        self.assertRegex(
            emu,
            r"OuterAction\s*==\s*MelonPrime::VideoBackend::OuterRendererAction::Replace",
        )

    def test_sapphire_gpu2d_state_owned_by_gpu(self):
        gpu_h = read_repo("src/GPU.h")
        gpu_cpp = read_repo("src/GPU.cpp")
        state_h = read_repo("src/MelonPrimeSapphireGpu2DState.h")

        self.assertIn("class SapphireGpu2DState", state_h)
        self.assertIn("TryGetSapphireGpu2DState", gpu_h)
        self.assertIn("Sapphire2D", gpu_h)
        self.assertIn("Sapphire2D = std::make_unique<SapphireGpu2DState>", gpu_cpp)

    def test_gpu2d_events_forwarded_via_adapter(self):
        adapter = read_repo("src/MelonPrimeSapphireGpu2DAdapter.cpp")
        gpu_cpp = read_repo("src/GPU.cpp")

        for symbol in (
            "ForwardRegisterWrite8",
            "ForwardWindowCheck",
            "ForwardVBlank",
            "ForwardVBlankEnd",
        ):
            self.assertIn(symbol, adapter)

        self.assertIn("ForwardVBlankEnd", gpu_cpp)
        self.assertIn("ForwardVBlank", gpu_cpp)

    def test_physical_screen_publication_without_screenswap_remap(self):
        soft = read_repo("src/GPU_Soft.cpp")

        publish = re.search(
            r"void SoftRenderer::PublishCompletedSapphireFrontBuffer\(\) noexcept\s*\{[^}]+\}",
            soft,
        )
        self.assertIsNotNone(publish)
        body = publish.group(0)
        self.assertNotIn("ScreenSwap", body)
        self.assertIn("Framebuffer[buffer][0] = Framebuffer[buffer][0]", body)

    def test_hud_staging_uses_timeline_completion(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        screen = read_repo("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp")

        self.assertIn("completionTimelineValue", overlay_h)
        self.assertIn("releaseCompletedUploadSlots", overlay)
        self.assertIn("markLastUploadSubmitted", overlay)
        self.assertNotIn("releaseUploadSlots", overlay)
        self.assertNotIn("releaseUploadSlots", screen)
        self.assertIn("markLastUploadSubmitted", screen)
        self.assertIn("getCompletedTimelineValue", screen)

    def test_hud_pipeline_timeline_retire(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")

        self.assertIn("retirePipeline", overlay)
        self.assertIn("retiredPipelines", overlay_h)
        self.assertIn("retiredPipelines.Retire", overlay)

    def test_fullscreen_does_not_block_on_pending_swapchain_build(self):
        presenter = read_repo(
            "src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp"
        )

        ensure = re.search(
            r"bool VulkanSurfacePresenter::ensureSwapchain\(SurfaceState& surfaceState\)"
            r"[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(ensure)
        body = ensure.group(0)
        self.assertNotIn("kSwapchainRecreateWaitNs", body)

    def test_window_state_change_does_not_detach_surface(self):
        screen = read_repo("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp")
        change = re.search(
            r"void ScreenPanelVulkan::changeEvent\(QEvent\* event\)"
            r"[\s\S]*?^}",
            screen,
            re.MULTILINE,
        )
        self.assertIsNotNone(change)
        body = change.group(0)
        self.assertNotIn("ensureNativeSurface", body)


if __name__ == "__main__":
    unittest.main()
