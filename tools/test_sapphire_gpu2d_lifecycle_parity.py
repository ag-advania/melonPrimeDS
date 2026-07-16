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
        self.assertIn("SapphireGPU2DCore::GPU2D::Unit GPU2D_A", gpu_h)
        self.assertIn("GPU2D_Renderer", gpu_h)

    def test_gpu2d_events_use_canonical_units(self):
        gpu_cpp = read_repo("src/GPU.cpp")

        self.assertIn("GPU2D_A.CheckWindows", gpu_cpp)
        self.assertIn("GPU2D_A.VBlank()", gpu_cpp)
        self.assertIn("GPU2D_Renderer->VBlankEnd(&GPU2D_A, &GPU2D_B)", gpu_cpp)
        self.assertIn("UsesSapphireGpu2DPath()", gpu_cpp)
        self.assertIn("GPU2D_Renderer->DrawScanline(line, &GPU2D_A)", gpu_cpp)
        self.assertIn("GPU2D_Renderer->DrawScanline(line, &GPU2D_B)", gpu_cpp)
        self.assertIn("GPU2D_Renderer->DrawSprites(line+1, &GPU2D_A)", gpu_cpp)
        self.assertNotIn("MelonPrimeSapphireGpu2DAdapter", gpu_cpp)

    def test_sapphire_path_skips_outer_renderer_draw_bridge(self):
        soft = read_repo("src/GPU_Soft.cpp")
        draw = re.search(
            r"void SoftRenderer::DrawScanline\(u32 line\)[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(draw)
        body = draw.group(0)
        self.assertIn("GPU.UsesSapphireGpu2DPath()", body)
        self.assertNotIn("renderer2D.DrawScanline(line, &GPU.GPU2D_A)", body)

    def test_framebuffer_assignment_uses_powercontrol9_bit15(self):
        gpu = read_repo("src/GPU.cpp")
        assign = re.search(
            r"bool GPU::AssignFramebuffers\(\) noexcept[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(assign)
        body = assign.group(0)
        self.assertIn("NDS.PowerControl9 & (1<<15)", body)
        self.assertNotIn("if (ScreenSwap)", body)

    def test_set_powercnt_refreshes_framebuffer_bindings(self):
        gpu = read_repo("src/GPU.cpp")
        set_power = re.search(
            r"void GPU::SetPowerCnt\(u32 val\) noexcept[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(set_power)
        body = set_power.group(0)
        self.assertIn("AssignFramebuffers()", body)

    def test_physical_screen_publication_without_screenswap_remap(self):
        soft = read_repo("src/GPU_Soft.cpp")

        publish = re.search(
            r"bool SoftRenderer::PublishSapphire2DFrame\(\) noexcept[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(publish)
        body = publish.group(0)
        self.assertIn("BuildPhysicalScreenView", body)
        self.assertNotRegex(
            body,
            r"published\.top\.packed\s*=\s*GPU\.ScreenSwap",
        )

    def test_gpu_owns_sapphire_framebuffers(self):
        gpu_h = read_repo("src/GPU.h")
        gpu = read_repo("src/GPU.cpp")
        self.assertIn("InitFramebuffers", gpu_h)
        self.assertIn("AssignFramebuffers", gpu_h)
        self.assertIn("SetRenderer3D", gpu)
        self.assertIn("InitFramebuffers();", gpu)

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
