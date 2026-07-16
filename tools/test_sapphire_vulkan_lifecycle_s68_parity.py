#!/usr/bin/env python3
"""Static contract tests for Sapphire Vulkan lifecycle parity (S68)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleS68ParityTests(unittest.TestCase):
    def test_activate_sapphire_vulkan2d_transaction(self):
        gpu_h = read_repo("src/GPU.h")
        gpu = read_repo("src/GPU.cpp")
        state = read_repo("src/MelonPrimeSapphireGpu2DState.cpp")
        emu = read_repo("src/frontend/qt_sdl/EmuThread.cpp")

        self.assertIn("ActivateSapphireVulkan2D", gpu_h)
        self.assertIn("DeactivateSapphireVulkan2D", gpu_h)
        self.assertIn("GPU2D_Renderer", gpu_h)
        self.assertIn("TryGetGpu2DSoftRenderer", gpu_h)
        self.assertIn("SapphireRenderingActive", state)
        self.assertIn("ActiveSapphireRendererGeneration", state)
        self.assertIn("ActivateSapphireVulkan2D(rendererGeneration)", emu)

    def test_refresh_bindings_do_not_publish(self):
        gpu = read_repo("src/GPU.cpp")
        refresh = re.search(
            r"void GPU::RefreshSapphireVulkanBindings\(\) noexcept\s*\{[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(refresh)
        body = refresh.group(0)
        self.assertIn("SyncSapphireFramebufferBindings", body)
        self.assertNotIn("PublishCompletedSapphireFrontBuffer", body)

    def test_assign_sapphire_framebuffers_matches_sapphire(self):
        soft = read_repo("src/GPU_Soft.cpp")
        self.assertIn("AssignSapphireFramebuffers", soft)
        self.assertNotIn("physicalTop", soft)
        publish = re.search(
            r"bool SoftRenderer::PublishSapphire2DFrame\(\) noexcept[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(publish)
        publish_body = publish.group(0)
        self.assertIn("BuildPhysicalScreenView", publish_body)
        self.assertNotRegex(
            publish_body,
            r"published\.top\.packed\s*=\s*GPU\.ScreenSwap",
        )

    def test_packed_framebuffer_clear_uses_full_stride(self):
        soft = read_repo("src/GPU_Soft.cpp")
        stop = re.search(
            r"void SoftRenderer::Stop\(\)\s*\{[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(stop)
        self.assertIn("PackedFramebufferClearBytes", stop.group(0))

    def test_custom_hud_upload_bound_to_queue_submission(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        screen = read_repo("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp")

        self.assertIn("notifySurfaceSubmission", overlay_h)
        self.assertIn("SetDesktopOverlaySubmissionNotifier", presenter)
        self.assertIn("SetDesktopOverlaySubmissionNotifier", screen)
        self.assertNotIn("markLastUploadSubmitted(frame->presentTimelineValue)", screen)
        self.assertNotIn("pendingSubmittedUploadSlot", overlay)

    def test_fullscreen_transition_coalescing(self):
        window_h = read_repo("src/frontend/qt_sdl/Window.h")
        window = read_repo("src/frontend/qt_sdl/Window.cpp")
        self.assertIn("fullscreenTransitionActive", window_h)
        self.assertIn("desiredFullscreen", window_h)
        self.assertIn("startFullscreenTransition", window)
        self.assertIn("syncFullscreenTransitionState", window)

    def test_surface_presenter_avoids_unbounded_recovery_wait(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        recover = re.search(
            r"void VulkanSurfacePresenter::recoverSwapchain\([\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(recover)
        body = recover.group(0)
        self.assertIn("kRecoverSwapchainWaitNs", body)
        self.assertNotRegex(body, r"waitForSurfaceIdle\(surfaceState\)\s*;")

    def test_present_skips_native_identity_recheck(self):
        screen = read_repo("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp")
        present = re.search(
            r"void ScreenPanelVulkan::presentOnGuiThread\(\)\s*\{[\s\S]*?^}",
            screen,
            re.MULTILINE,
        )
        self.assertIsNotNone(present)
        body = present.group(0)
        self.assertNotIn("matchesWidget", body)
        self.assertNotIn("ensureNativeSurface()", body)

    def test_integer_scaling_transform_snaps_to_device_pixels(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("integerAxisAligned", presenter)
        self.assertIn("std::round(px)", presenter)


if __name__ == "__main__":
    unittest.main()
