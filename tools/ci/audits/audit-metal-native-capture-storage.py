#!/usr/bin/env python3
"""PR-3 static gate: native R16Uint capture canonical storage."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
CAPTURE = ROOT / "src/GPU_MetalCaptureMethods.inc"
FULL_GPU = ROOT / "src/GPU_MetalFullGpuMethods.inc"
SHADER_2D = ROOT / "src/GPU2D_MetalFullGpuShaders.inc"
SHADER_3D = ROOT / "src/GPU3D_MetalComputeTexturedShaders.inc"


def main() -> int:
    issues: list[str] = []
    capture = CAPTURE.read_text(encoding="utf-8")

    if "MTLPixelFormatR16Uint" not in capture:
        issues.append("Capture textures must use MTLPixelFormatR16Uint")
    if re.search(r"128u \* scaleU|256u \* scaleU", capture):
        issues.append("Capture texture allocation still multiplies by scale")
    if "mp_metal_capture_readback" in capture:
        issues.append("decode readback kernel must be removed (direct blit)")
    if "ReadbackPipeline" in capture:
        issues.append("ReadbackPipeline must be removed")
    if "kMaxCaptureStagingBytes = 16u * 1024u * 1024u" in capture:
        issues.append("staging cap still 16 MiB (should be native 128 KiB)")
    if "std::vector<uint32_t> CpuUploadScratch" in capture:
        issues.append("CpuUploadScratch still RGBA8 uint32")
    if "MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1" not in capture:
        issues.append("missing native R16Uint marker comment")

    # Scale-independent early return must update Scale without recreate.
    if "CaptureState->Scale = scale" not in capture:
        issues.append("scale change must update Scale without texture recreate")

    full_gpu = FULL_GPU.read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" in full_gpu:
        issues.append(
            "CaptureCnt Full-GPU exclusion must be removed (PR-5 cutover)"
        )
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_CUTOVER_V1" not in full_gpu:
        issues.append("missing PR-5 cutover marker in FullGpu eligibility")

    s2 = SHADER_2D.read_text(encoding="utf-8")
    if "texture2d_array<ushort, access::read> capture128" not in s2:
        issues.append("2D shaders must read ushort capture arrays")
    if "capture128.sample(" in s2 or "capture256.sample(" in s2:
        issues.append("2D shaders must not .sample() R16Uint capture")

    s3 = SHADER_3D.read_text(encoding="utf-8")
    if "texture2d_array<ushort, access::read> capture" not in s3:
        issues.append("3D capture sampler must use ushort arrays")

    if issues:
        print("FAIL: metal native capture storage audit")
        for item in issues:
            print(f"  - {item}")
        return 1

    print("PASS: metal native capture storage audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
