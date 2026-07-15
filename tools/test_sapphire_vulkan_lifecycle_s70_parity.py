#!/usr/bin/env python3
"""Static contract tests for Sapphire Vulkan lifecycle parity (S70)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleS70ParityTests(unittest.TestCase):
    def test_presenter_header_matches_ensure_surface_format_signature(self):
        header = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        cpp = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("SwapchainBundle& bundle);", header)
        self.assertIn(
            "bool VulkanSurfacePresenter::ensureSurfaceFormatRenderResources(\n"
            "    VkFormat format,\n"
            "    SwapchainBundle& bundle)",
            cpp,
        )

    def test_build_identity_logged_at_startup(self):
        main_cpp = read_repo("src/frontend/qt_sdl/main.cpp")
        presenter = read_repo("src/frontend/qt_sdl/MelonPrimeDesktopVulkanPresenter.cpp")
        self.assertIn("LogBuildIdentity();", main_cpp)
        self.assertIn("[BuildIdentity]", presenter)
        self.assertIn("MELONPRIME_GIT_COMMIT", presenter)

    def test_hud_upload_region_cpu_only_until_record(self):
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        upload = re.search(
            r"bool MelonPrimeVulkanOverlayRenderer::uploadRegion\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        record = re.search(
            r"OverlayTransferRecord MelonPrimeVulkanOverlayRenderer::recordPendingTransfer\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        self.assertIsNotNone(upload)
        self.assertIsNotNone(record)
        self.assertNotIn("stagePendingRegion();", upload.group(0))
        self.assertIn("stagePendingRegion()", record.group(0))

    def test_submit_and_present_results_are_separated(self):
        header = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("struct SurfaceSubmitResult", header)
        self.assertIn("commandSubmitted", header)
        submit = re.search(
            r"SurfaceSubmitResult VulkanSurfacePresenter::submitSurfaceCommands\("
            r"[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(submit)
        body = submit.group(0)
        self.assertIn("result.commandSubmitted = true", body)
        self.assertNotRegex(body, r"return\s+result\.presentResult\s*==\s*VK_SUCCESS")

    def test_hud_notify_on_submit_success_before_present_failure(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        present = re.search(
            r"const SurfaceSubmitResult submitResult = submitSurfaceCommands\("
            r"[\s\S]*?presentedAnySurface = true;",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(present)
        body = present.group(0)
        notify_true = body.find("true,\n                submitResult.timelineValue")
        notify_present_fail = body.find('recoverSwapchain(surfaceState, "vkQueuePresentKHR")')
        self.assertNotEqual(notify_true, -1)
        if notify_present_fail != -1:
            self.assertLess(notify_true, notify_present_fail)

    def test_hud_texture_uses_per_generation_descriptor_set(self):
        overlay_h = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.h")
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        self.assertIn("VkDescriptorSet descriptorSet", overlay_h)
        create = re.search(
            r"bool MelonPrimeVulkanOverlayRenderer::createTexture\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        self.assertIsNotNone(create)
        self.assertIn("vkAllocateDescriptorSets", create.group(0))
        self.assertIn("lastTextureUseSubmissionSerial", overlay_h)

    def test_hud_retires_texture_by_last_use_serial(self):
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        create = re.search(
            r"bool MelonPrimeVulkanOverlayRenderer::createTexture\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        notify = re.search(
            r"void MelonPrimeVulkanOverlayRenderer::notifySurfaceSubmission\("
            r"[\s\S]*?^}",
            overlay,
            re.MULTILINE,
        )
        self.assertIsNotNone(create)
        self.assertIsNotNone(notify)
        self.assertIn("lastTextureUseSubmissionSerial", create.group(0))
        self.assertIn("lastTextureUseSubmissionSerial = submissionSerial", notify.group(0))

    def test_recovery_avoids_queue_wait_idle(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        destroy = re.search(
            r"void VulkanSurfacePresenter::destroySwapchain\("
            r"[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        recover = re.search(
            r"void VulkanSurfacePresenter::recoverSwapchain\("
            r"[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(destroy)
        self.assertIsNotNone(recover)
        self.assertNotIn("vkQueueWaitIdle", destroy.group(0))
        self.assertNotIn("destroySwapchain(surfaceState)", recover.group(0))
        self.assertIn("swapchainDirty = true", recover.group(0))

    def test_cached_surface_format_resources_destroyed_on_shutdown(self):
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        destroy = re.search(
            r"void VulkanSurfacePresenter::destroyCommonResources\(\)\s*\{[\s\S]*?^}",
            presenter,
            re.MULTILINE,
        )
        self.assertIsNotNone(destroy)
        body = destroy.group(0)
        self.assertIn("cachedSurfaceFormatResources", body)
        self.assertIn("vkDestroyPipeline", body)
        self.assertIn("vkDestroyRenderPass", body)

    def test_integer_transforms_use_nearest_screen_descriptor(self):
        header = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.h")
        presenter = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("screenIntegerDescriptorSet", header)
        self.assertIn("forceNearestInteger", presenter)
        self.assertIn("screenIntegerDescriptorSet", presenter)

    def test_desktop_presenter_wrapper_exists(self):
        header = read_repo("src/frontend/qt_sdl/MelonPrimeDesktopVulkanPresenter.h")
        cmake = read_repo("src/frontend/qt_sdl/CMakeLists.txt")
        self.assertIn("namespace MelonPrime::DesktopVulkan", header)
        self.assertIn("MelonPrimeDesktopVulkanPresenter.cpp", cmake)


if __name__ == "__main__":
    unittest.main()
