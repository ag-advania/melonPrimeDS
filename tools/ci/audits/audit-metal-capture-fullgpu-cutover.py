#!/usr/bin/env python3
"""Capture Full-GPU gate + no silent Soft mid-frame escape."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []
    full = (ROOT / "src/GPU_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" not in full:
        issues.append("CaptureCnt bit31 eligibility Soft gate missing")
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_GATED_V2" not in full:
        issues.append("missing CAPTURE_FULLGPU_GATED_V2 marker")
    if "CaptureFeedbackCooldownFrames" in full:
        issues.append(
            "CaptureFeedbackCooldownFrames Soft-escape cooldown must not exist "
            "(plan forbids silent Software return)"
        )

    mm = (ROOT / "src/GPU_Metal.mm").read_text(encoding="utf-8")
    if "no silent Soft mid-frame escape" not in mm:
        issues.append("VBlank rejection log must state no silent Soft escape")
    if "CaptureFeedbackCooldownFrames" in mm:
        issues.append("VBlank must not arm CaptureFeedback Soft-escape cooldown")
    # Must not always Soft-block on every rejection.
    if "Always sticky-block Full-GPU retries after any rejection" in mm:
        issues.append("must not always Soft-escape after Full-GPU rejection")

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
