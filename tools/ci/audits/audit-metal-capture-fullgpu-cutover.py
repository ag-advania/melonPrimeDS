#!/usr/bin/env python3
"""PR-5 static gate: capture Full-GPU cutover (CaptureCnt exclusion removed)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []
    full = (ROOT / "src/GPU_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" in full:
        issues.append("CaptureCnt bit31 exclusion still present")
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_CUTOVER_V1" not in full:
        issues.append("missing FULLGPU_CUTOVER marker")

    sched = (ROOT / "src/GPU_MetalSegmentScheduler.inc").read_text(encoding="utf-8")
    if "allowCaptureTextures = true" not in sched:
        issues.append("segment scheduler should allow capture textures after cutover")

    header = (ROOT / "src/GPU_Metal.h").read_text(encoding="utf-8")
    if "class MetalRenderer : public SoftRenderer" not in header:
        issues.append("SoftRenderer inheritance must remain until PR-7")

    if issues:
        print("FAIL: metal capture full-gpu cutover audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal capture full-gpu cutover audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
