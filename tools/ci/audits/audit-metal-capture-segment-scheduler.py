#!/usr/bin/env python3
"""PR-4 static gate: segment capture scheduler ordering."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []
    sched = ROOT / "src/GPU_MetalSegmentScheduler.inc"
    if not sched.exists():
        issues.append("missing GPU_MetalSegmentScheduler.inc")
    else:
        text = sched.read_text(encoding="utf-8")
        for token in (
            "MELONPRIME_METAL_CAPTURE_SEGMENT_SCHEDULER_V1",
            "RenderMetalFullGpuFrameSegmented",
            "BuildFullGpuCaptureSegments",
            "RenderSegmentedGpuFrame",
            "EncodeMetalDisplayCapture",
        ):
            if token not in text:
                issues.append(f"scheduler missing {token}")
        # Ordering: A then B then capture inside the render loop body.
        fn = text.find("bool MetalRenderer::RenderMetalFullGpuFrameSegmented")
        body = text[fn:] if fn >= 0 else text
        a = body.find("Metal2D_A->RenderSegmentedGpuFrame")
        b = body.find("Metal2D_B->RenderSegmentedGpuFrame")
        c = body.find("EncodeMetalDisplayCapture(")
        if not (0 <= a < b < c):
            issues.append("segment loop must order A → B → Capture")

    full = (ROOT / "src/GPU_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" in full:
        issues.append("CaptureCnt Soft gate must stay removed after cutover")
    if "MELONPRIME_METAL_CAPTURE_FULLGPU_CUTOVER_V2" not in full:
        issues.append("missing CAPTURE_FULLGPU_CUTOVER_V2 marker")

    mm = (ROOT / "src/GPU_Metal.mm").read_text(encoding="utf-8")
    if "GPU_MetalSegmentScheduler.inc" not in mm:
        issues.append("GPU_Metal.mm must include segment scheduler")
    if "RenderMetalFullGpuFrameSegmented" not in mm:
        issues.append("VBlank must call RenderMetalFullGpuFrameSegmented")

    capture = (ROOT / "src/GPU_MetalCaptureMethods.inc").read_text(encoding="utf-8")
    if "LineOffset" not in capture:
        issues.append("capture dispatch must support LineOffset")
    if "int startLine,\n    int endLine)" not in capture and \
       "int startLine,\n    int endLine)\n{" not in capture:
        # tolerate formatting
        if "startLine" not in capture or "endLine" not in capture:
            issues.append("EncodeMetalDisplayCapture must take line range")
    if "MELONPRIME_METAL_CAPTURE_PINGPONG_V1" not in capture:
        issues.append("missing CAPTURE_PINGPONG_V1 marker")
    if "MELONPRIME_METAL_CAPTURE_WRITE_TICKET_V1" not in capture:
        issues.append("missing CAPTURE_WRITE_TICKET_V1 marker")
    if "struct CaptureWriteTicket" not in capture:
        issues.append("CaptureWriteTicket struct missing")
    if "Snapshot128" in capture or "Snapshot256" in capture:
        issues.append("Snapshot textures must be replaced by ping-pong faces")
    if "Capture128[2]" not in capture:
        issues.append("Capture128 ping-pong array missing")

    if "MELONPRIME_METAL_CAPTURE_PINGPONG_V1" not in sched.read_text(encoding="utf-8"):
        issues.append("segment scheduler must document ping-pong")

    if issues:
        print("FAIL: metal capture segment scheduler audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal capture segment scheduler audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
