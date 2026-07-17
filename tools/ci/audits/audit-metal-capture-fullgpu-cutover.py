#!/usr/bin/env python3
"""PR-5 follow-up: capture Full-GPU stays gated until feedback is ready.

After the cutover freeze regression, capture-start frames must Soft-fallback
again (CaptureCnt bit31 exclusion), and VBlank rejections must sticky-cooldown.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []
    full = (ROOT / "src/GPU_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" not in full:
        issues.append("CaptureCnt bit31 Soft gate missing (required after freeze fix)")
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_GATED_V2" not in full:
        issues.append("missing CAPTURE_FULLGPU_GATED_V2 marker")
    if "CaptureFeedbackCooldownFrames" not in full:
        issues.append("missing CaptureFeedbackCooldownFrames sticky cooldown")
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_CUTOVER_V1" in full and \
            "MELONPRIME_METAL_CAPTURE_FULLGPU_GATED_V2" not in full:
        issues.append("stale unconditional cutover marker without gate")

    mm = (ROOT / "src/GPU_Metal.mm").read_text(encoding="utf-8")
    if "CaptureFeedbackCooldownFrames" not in mm:
        issues.append("VBlank rejection must arm CaptureFeedbackCooldownFrames")
    if "BlockedByMidFrameInvalidation = true" not in mm:
        issues.append("VBlank rejection must sticky-block Full-GPU retries")

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
