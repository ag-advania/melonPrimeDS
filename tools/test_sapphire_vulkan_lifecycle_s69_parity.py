#!/usr/bin/env python3
"""Static contract tests for Sapphire Vulkan lifecycle parity (S69)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleS69ParityTests(unittest.TestCase):
    def test_physical_publication_uses_fixed_lcd_framebuffer_slots(self):
        soft = read_repo("src/GPU_Soft.cpp")
        view = re.search(
            r"SapphirePhysical2DScreenView SoftRenderer::BuildPhysicalScreenView\("
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(view)
        body = view.group(0)
        packed_line = next(
            (line for line in body.splitlines() if "view.packed" in line),
            "",
        )
        self.assertRegex(packed_line, r"Framebuffer\[frontBuffer\]\[top\s*\?\s*1u\s*:\s*0u\]")
        self.assertNotIn("GPU.ScreenSwap", packed_line)

    def test_publish_pairs_packed_and_structured_through_view(self):
        soft = read_repo("src/GPU_Soft.cpp")
        publish = re.search(
            r"bool SoftRenderer::PublishSapphire2DFrame\(\) noexcept[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(publish)
        body = publish.group(0)
        self.assertIn("BuildPhysicalScreenView", body)
        self.assertIn("topView.plane0", body)
        self.assertIn("bottomView.plane0", body)
        self.assertNotRegex(body, r"published\.top\.packed\s*=\s*GPU\.ScreenSwap")

    def test_engine_metadata_still_tracks_screen_swap(self):
        soft = read_repo("src/GPU_Soft.cpp")
        view = re.search(
            r"SapphirePhysical2DScreenView SoftRenderer::BuildPhysicalScreenView\("
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(view)
        self.assertIn("GPU.ScreenSwap", view.group(0))

    def test_hud_upload_slots_use_state_machine(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        self.assertIn("OverlayUploadState", overlay_h)
        self.assertIn("OverlayUploadState::Free", overlay_h)

    def test_hud_submission_notifies_exact_upload_token(self):
        presenter_h = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        self.assertIn("u64 uploadToken", presenter_h)
        self.assertIn("u64 submissionSerial", presenter_h)
        self.assertIn("notifySurfaceSubmission(", overlay)
        notify = re.search(
            r"void MelonPrimeVulkanOverlayRenderer::notifySurfaceSubmission\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        self.assertIsNotNone(notify)
        self.assertIn("uploadToken == 0", notify.group(0))
        self.assertNotIn("if (!slot.recorded)", notify.group(0))

    def test_hud_texture_committed_after_submission(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        self.assertIn("OverlayTextureState", overlay_h)
        self.assertIn("committedLayout", overlay_h)
        record = re.search(
            r"OverlayTransferRecord MelonPrimeVulkanOverlayRenderer::recordPendingTransfer\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        self.assertIsNotNone(record)
        self.assertNotIn("hasValidUploadedOverlay = true", record.group(0))

    def test_overlay_shutdown_clears_submission_notifier(self):
        screen = read_repo("src/frontend/qt_sdl/MelonPrimeScreenVulkan.cpp")
        destructor = re.search(
            r"ScreenPanelVulkan::~ScreenPanelVulkan\(\)\s*\{[\s\S]*?^}",
            screen,
            re.MULTILINE,
        )
        self.assertIsNotNone(destructor)
        body = destructor.group(0)
        self.assertIn("SetDesktopOverlaySubmissionNotifier(nullptr", body)
        self.assertLess(
            body.index("SetDesktopOverlaySubmissionNotifier"),
            body.index("overlayRenderer.shutdown"),
        )

    def test_reverse_fullscreen_transition_clears_active_flag(self):
        window = read_repo("src/frontend/qt_sdl/Window.cpp")
        sync = re.search(
            r"void MainWindow::syncFullscreenTransitionState\(\)\s*\{[\s\S]*?^}",
            window,
            re.MULTILINE,
        )
        self.assertIsNotNone(sync)
        body = sync.group(0)
        self.assertIn("fullscreenTransitionActive = false", body)
        self.assertIn("QTimer::singleShot", body)

    def test_resize_surface_skips_same_extent(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        resize = re.search(
            r"bool VulkanSurfacePresenter::resizeSurface\("
            r"[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(resize)
        self.assertIn("requestedWidth == width", resize.group(0))

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

    def test_surface_pipeline_cached_by_format(self):
        presenter_h = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("SurfaceFormatRenderResources", presenter_h)
        self.assertIn("ensureSurfaceFormatRenderResources", presenter)

    def test_initial_vulkan_boot_avoids_frame_delay_hacks(self):
        soft = read_repo("src/GPU_Soft.cpp")
        self.assertNotIn("white-only", soft)
        self.assertNotIn("PackedFramebufferClearBytes(0)", soft)

    def test_4x_scaling_uses_integer_pixel_snap(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("integerAxisAligned", presenter)
        self.assertIn("std::round(px)", presenter)


if __name__ == "__main__":
    unittest.main()
