#!/usr/bin/env python3
"""PR-8: Compute RasterReference removal — no nested Metal raster renderer,
no silent Raster fallback, in the production Metal Compute path."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []

    header = (ROOT / "src/GPU3D_MetalCompute.h").read_text(encoding="utf-8")
    if "RasterReference" in header:
        issues.append("GPU3D_MetalCompute.h must not declare a RasterReference member")
    if "MELONPRIME_METAL_COMPUTE_RASTER_REFERENCE_REMOVAL_V1" not in header:
        issues.append("missing RASTER_REFERENCE_REMOVAL_V1 marker in header")

    mm = (ROOT / "src/GPU3D_MetalCompute.mm").read_text(encoding="utf-8")
    if "RasterReference" in mm:
        issues.append("GPU3D_MetalCompute.mm must not reference RasterReference")
    if "MELONPRIME_METAL_COMPUTE_RASTER_REFERENCE_REMOVAL_V1" not in mm:
        issues.append("missing RASTER_REFERENCE_REMOVAL_V1 marker in .mm")
    if "continueWithRasterOnly" in mm:
        issues.append("Init() must not silently continue with a raster-only fallback")
    if "using Metal raster renderer only" in mm:
        issues.append("Init() must not silently degrade to a raster-only renderer")

    # GetLine() must fail closed (nullptr), matching the OpenGL Compute path,
    # never delegate to a nested raster renderer.
    if "return RasterReference.GetLine" in mm:
        issues.append("GetLine() must not delegate to a raster renderer")

    tooltip = (ROOT / "src/frontend/qt_sdl/VideoSettingsDialog.cpp").read_text(
        encoding="utf-8"
    )
    for needle in (
        "internal Metal raster renderer is used only as an automatic fallback",
        "Metal raster renderer is only an automatic per-frame fallback",
    ):
        if needle in tooltip:
            issues.append(f"VideoSettingsDialog.cpp still advertises a raster fallback: {needle!r}")

    if issues:
        print("FAIL: metal compute raster-reference removal audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal compute raster-reference removal audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
