#!/usr/bin/env python3
"""Vulkan lifecycle source-contract checks retained from S75-15."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireVulkanLifecycleSourceContractTests(unittest.TestCase):
    def test_vulkan_cold_start_path_exists(self):
        main_cpp = read_repo("src/frontend/qt_sdl/main.cpp")
        presenter_cpp = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanSurfacePresenter.cpp")
        self.assertIn("MELONPRIME_ENABLE_VULKAN", main_cpp)
        self.assertIn("LogBuildIdentity", main_cpp)
        self.assertIn("VulkanSurfacePresenter", presenter_cpp)

    def test_renderer_switch_hooks_present(self):
        video_backend = read_repo("src/frontend/qt_sdl/MelonPrimeVideoBackend.h")
        session = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanFrontendSession.cpp")
        self.assertIn("MigrateLegacyRendererId", video_backend)
        self.assertIn("setActiveGenerations", session)

    def test_fullscreen_transition_queue_sync_present(self):
        window_cpp = read_repo("src/frontend/qt_sdl/Window.cpp")
        main_cpp = read_repo("src/frontend/qt_sdl/main.cpp")
        frame_queue = read_repo("src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp")
        self.assertIn("toggleFullscreen", window_cpp)
        self.assertIn("toggleFullscreen", main_cpp)
        self.assertIn("synchronizePresentationCompletion", frame_queue)

    def test_custom_hud_final_pass_is_separate_from_compositor(self):
        overlay = read_repo("src/frontend/qt_sdl/MelonPrimeVulkanOverlayRenderer.cpp")
        output = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanOutput.cpp")
        self.assertIn("MelonPrimeVulkanOverlayRenderer", overlay)
        self.assertIn("dispatchCompositor", output)

    def test_runtime_smoke_artifacts_are_logged(self):
        frame_queue = read_repo("src/frontend/qt_sdl/VulkanReference/FrameQueue.cpp")
        output_h = read_repo("src/frontend/qt_sdl/VulkanReference/VulkanOutput.h")
        self.assertIn("takeStatsSnapshotAndReset", frame_queue)
        self.assertIn("VulkanOutputTemporalStats", output_h)
        self.assertIn("takeTemporalStatsSnapshotAndReset", output_h)


if __name__ == "__main__":
    unittest.main()
