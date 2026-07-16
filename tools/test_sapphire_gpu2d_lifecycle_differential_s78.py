#!/usr/bin/env python3
"""Executable Sapphire GPU2D lifecycle differential test (S78-13)."""

from __future__ import annotations

import os
import re
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def resolve_upstream_gpu_cpp() -> Path:
    env = os.environ.get("SAPPHIRE_ANDROID_LIB_ROOT")
    if env:
        candidate = Path(env) / "src" / "GPU.cpp"
        if candidate.is_file():
            return candidate
    vendored = (
        REPO_ROOT
        / "src"
        / "SapphireVendor"
        / "upstream"
        / "melonDS-android-lib"
        / "src"
        / "GPU.cpp"
    )
    if vendored.is_file():
        return vendored
    local = Path(r"C:\Users\Admin\Documents\git\melonDS-android-lib\src\GPU.cpp")
    if local.is_file():
        return local
    raise unittest.SkipTest("Sapphire android-lib GPU.cpp unavailable")


def normalize_lifecycle_snippet(text: str) -> str:
    text = re.sub(r"//.*", "", text)
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    text = re.sub(r"\s+", "", text)
    return text


def extract_block(text: str, signature: str) -> str:
    match = re.search(signature + r"[\s\S]*?^}", text, re.MULTILINE)
    if match is None:
        raise AssertionError(f"missing block for {signature}")
    return match.group(0)


class SapphireGpu2DLifecycleDifferentialS78Tests(unittest.TestCase):
    def test_start_hblank_draw_order_matches_upstream(self):
        local = (REPO_ROOT / "src" / "GPU.cpp").read_text(encoding="utf-8")
        upstream = resolve_upstream_gpu_cpp().read_text(encoding="utf-8")

        local_block = extract_block(local, r"void GPU::StartHBlank\(u32 line\) noexcept")
        upstream_block = extract_block(upstream, r"void GPU::StartHBlank\(u32 line\) noexcept")

        for needle in (
            "GPU2D_Renderer->DrawScanline(line,&GPU2D_A)",
            "GPU2D_Renderer->DrawScanline(line,&GPU2D_B)",
            "GPU2D_Renderer->DrawSprites(line+1,&GPU2D_A)",
            "GPU2D_Renderer->DrawSprites(line+1,&GPU2D_B)",
        ):
            self.assertIn(
                needle,
                normalize_lifecycle_snippet(local_block),
                msg=f"local StartHBlank missing {needle}",
            )
            self.assertIn(
                needle,
                normalize_lifecycle_snippet(upstream_block),
                msg=f"upstream StartHBlank missing {needle}",
            )

    def test_start_scanline_vblank_end_at_line_zero(self):
        local = (REPO_ROOT / "src" / "GPU.cpp").read_text(encoding="utf-8")
        upstream = resolve_upstream_gpu_cpp().read_text(encoding="utf-8")

        local_block = extract_block(local, r"void GPU::StartScanline\(u32 line\) noexcept")
        upstream_block = extract_block(upstream, r"void GPU::StartScanline\(u32 line\) noexcept")

        local_norm = normalize_lifecycle_snippet(local_block)
        upstream_norm = normalize_lifecycle_snippet(upstream_block)

        self.assertIn("if(line==0)", local_norm)
        self.assertIn("GPU2D_Renderer->VBlankEnd(&GPU2D_A,&GPU2D_B)", local_norm)
        self.assertIn("if(line==0)", upstream_norm)
        self.assertIn("GPU2D_Renderer->VBlankEnd(&GPU2D_A,&GPU2D_B)", upstream_norm)

    def test_assign_framebuffers_uses_powercontrol9(self):
        local = (REPO_ROOT / "src" / "GPU.cpp").read_text(encoding="utf-8")
        upstream = resolve_upstream_gpu_cpp().read_text(encoding="utf-8")

        local_block = extract_block(local, r"bool GPU::AssignFramebuffers\(\) noexcept")
        upstream_block = extract_block(upstream, r"void GPU::AssignFramebuffers\(\) noexcept")

        self.assertIn(
            "NDS.PowerControl9&(1<<15)",
            normalize_lifecycle_snippet(local_block),
        )
        self.assertIn(
            "NDS.PowerControl9&(1<<15)",
            normalize_lifecycle_snippet(upstream_block),
        )


if __name__ == "__main__":
    unittest.main()
