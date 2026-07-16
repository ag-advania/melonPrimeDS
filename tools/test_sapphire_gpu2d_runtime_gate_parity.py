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
    def test_vulkan_build_skips_native_rend2d(self):
        soft = read_repo("src/GPU_Soft.cpp")

        constructor = re.search(
            r"SoftRenderer::SoftRenderer\(melonDS::NDS& nds\)"
            r"[\s\S]*?^}",
            soft,
            re.MULTILINE,
        )
        self.assertIsNotNone(constructor)
        body = constructor.group(0)
        self.assertRegex(
            body,
            r"#if !defined\(MELONPRIME_DS\) \|\| !defined\(MELONPRIME_ENABLE_VULKAN\)"
            r"[\s\S]*Rend2D_A = std::make_unique<SoftRenderer2D>",
        )

    def test_sapphire_drawscanline_uses_canonical_gpu_units(self):
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
        self.assertIn("&GPU.GPU2D_A", body)
        self.assertIn("&GPU.GPU2D_B", body)
        self.assertNotIn("SyncUnitFromGPU2D", body)

    def test_use_structured_vulkan2d_is_null_safe(self):
        sapphire = read_repo("src/SapphireGPU2DCore/GPU2D_Soft.cpp")
        self.assertIn("HasCurrentRenderer()", sapphire)
        self.assertNotIn(
            "return GPU.GPU3D.GetCurrentRenderer().UsesStructured2DMetadata();",
            sapphire,
        )

    def test_set_power_cnt_updates_canonical_units_only(self):
        gpu = read_repo("src/GPU.cpp")
        set_power = re.search(
            r"void GPU::SetPowerCnt\(u32 val\) noexcept\s*\{[\s\S]*?^}",
            gpu,
            re.MULTILINE,
        )
        self.assertIsNotNone(set_power)
        body = set_power.group(0)
        self.assertIn("GPU2D_A.SetEnabled", body)
        self.assertIn("GPU2D_B.SetEnabled", body)
        self.assertNotIn("Sapphire2D->UnitA", body)

    def test_vblank_routed_on_canonical_gpu_units(self):
        gpu = read_repo("src/GPU.cpp")

        self.assertIn("GPU2D_A.VBlank()", gpu)
        self.assertIn("GPU2D_B.VBlank()", gpu)
        self.assertIn("GPU2D_Renderer->VBlankEnd(&GPU2D_A, &GPU2D_B)", gpu)

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

    def test_gpu_owns_gpu2d_renderer_and_unit_types(self):
        gpu_h = read_repo("src/GPU.h")
        gpu = read_repo("src/GPU.cpp")

        self.assertIn("SapphireGPU2DCore::GPU2D::Unit GPU2D_A", gpu_h)
        self.assertIn("GPU2D_Renderer", gpu_h)
        self.assertIn("std::make_unique<SapphireGPU2DCore::GPU2D::SoftRenderer>", gpu)
        self.assertNotIn("UnitSync", gpu)

    def test_keep_current_skips_stale_init_failure_check(self):
        emu = read_repo("src/frontend/qt_sdl/EmuThread.cpp")
        self.assertRegex(
            emu,
            r"OuterAction\s*==\s*MelonPrime::VideoBackend::OuterRendererAction::Replace"
            r"[\s\S]*?LastRendererInitializationSucceeded",
        )


if __name__ == "__main__":
    unittest.main()
