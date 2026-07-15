#!/usr/bin/env python3
"""Static contract tests for Sapphire GPU2D runtime gate parity (S67)."""

from __future__ import annotations

import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def read_repo(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


class SapphireGpu2DRuntimeGateParityTests(unittest.TestCase):
    def test_native_rend2d_created_in_vulkan_build(self):
        soft = read_repo("src/GPU_Soft.cpp")

        constructor = re.search(
            r"SoftRenderer::SoftRenderer\(melonDS::NDS& nds\)"
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(constructor)
        body = constructor.group(0)
        self.assertIn("Rend2D_A = std::make_unique<SoftRenderer2D>", body)
        self.assertIn("Rend2D_B = std::make_unique<SoftRenderer2D>", body)
        self.assertNotRegex(body, r"#else\s*\n\s*Rend2D_A")

    def test_sapphire_drawscanline_gated_by_active_renderer(self):
        soft = read_repo("src/GPU_Soft.cpp")

        draw = re.search(
            r"void SoftRenderer::DrawScanline\(u32 line\)"
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(draw)
        body = draw.group(0)
        self.assertIn("IsActiveForRendering", body)
        self.assertIn("Rend2D_A->DrawScanline", body)

    def test_use_structured_vulkan2d_is_null_safe(self):
        sapphire = read_repo("src/SapphireGPU2DCore/GPU2D_Soft.cpp")
        self.assertIn("HasCurrentRenderer()", sapphire)
        self.assertNotIn(
            "return GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();",
            sapphire,
        )

    def test_set_power_cnt_syncs_sapphire_units(self):
        gpu = read_repo("src/GPU.cpp")
        set_power = re.search(
            r"void GPU::SetPowerCnt\(u32 val\) noexcept\s*\{[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(set_power)
        body = set_power.group(0)
        self.assertIn("Sapphire2D->UnitA.SetEnabled", body)
        self.assertIn("Sapphire2D->UnitB.SetEnabled", body)

    def test_vblank_forwarded_once_from_gpu(self):
        soft = read_repo("src/GPU_Soft.cpp")
        adapter = read_repo("src/MelonPrimeSapphireGpu2DAdapter.cpp")

        vblank = re.search(
            r"void SoftRenderer::VBlank\(\)\s*\{[^}]*\}",
            soft,
        )
        self.assertIsNotNone(vblank)
        self.assertNotIn("ForwardVBlank", vblank.group(0))
        self.assertIn("IsActiveForRendering", adapter)

    def test_renderer_replacement_invalidates_framebuffer_bindings(self):
        gpu_h = read_repo("src/GPU.h")
        gpu = read_repo("src/GPU.cpp")

        self.assertIn("InvalidateSapphireFramebufferBindings", gpu_h)
        set_renderer = re.search(
            r"void GPU::SetRenderer\(std::unique_ptr<Renderer>&& renderer\) noexcept"
            r"[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(set_renderer)
        self.assertIn("InvalidateSapphireFramebufferBindings", set_renderer.group(0))

    def test_set_renderer_does_not_publish_completed_frame(self):
        gpu = read_repo("src/GPU.cpp")
        set_renderer = re.search(
            r"void GPU::SetRenderer\(std::unique_ptr<Renderer>&& renderer\) noexcept"
            r"[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(set_renderer)
        self.assertNotIn("PublishCompletedSapphireFrontBuffer", set_renderer.group(0))

    def test_publish_rejects_inactive_backend_and_zero_serial(self):
        soft = read_repo("src/GPU_Soft.cpp")

        publish = re.search(
            r"bool SoftRenderer::PublishSapphire2DFrame\(\) noexcept"
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(publish)
        body = publish.group(0)
        self.assertIn("IsActiveForRendering", body)
        self.assertIn("VulkanFrameSerial == 0", body)

    def test_savestate_load_seeds_complete_unit_state(self):
        gpu = read_repo("src/GPU.cpp")
        unit_sync = read_repo("src/SapphireGPU2DCore/UnitSync.h")

        self.assertIn("SeedCompleteUnitFromNative", unit_sync)
        self.assertIn("SeedCompleteUnitFromNative", gpu)

    def test_keep_current_skips_stale_init_failure_check(self):
        emu = read_repo("src/frontend/qt_sdl/EmuThread.cpp")
        self.assertRegex(
            emu,
            r"OuterAction\s*==\s*MelonPrime::VideoBackend::OuterRendererAction::Replace"
            r"[\s\S]*?LastRendererInitializationSucceeded",
        )


if __name__ == "__main__":
    unittest.main()
