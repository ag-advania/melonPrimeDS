#!/usr/bin/env python3
"""Gate Metal Full-GPU frame bootstrap ordering (splash-stuck fix)."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]


def main() -> int:
    issues: list[str] = []

    gpu2d_h = (ROOT / "src/GPU2D_Metal.h").read_text(encoding="utf-8")
    if "BeginSegmentedSnapshotFrame" not in gpu2d_h:
        issues.append("explicit segmented snapshot frame begin API missing")
    if "CancelSegmentedSnapshotFrame" not in gpu2d_h:
        issues.append("CancelSegmentedSnapshotFrame API missing")

    full_gpu = (ROOT / "src/GPU_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    start_pos = full_gpu.find("void MetalRenderer::Start3DRendering")
    if start_pos < 0:
        issues.append("Start3DRendering definition missing")
    else:
        # Limit search to Start3DRendering body (until next MetalRenderer::).
        next_fn = full_gpu.find("\nvoid MetalRenderer::", start_pos + 1)
        body = full_gpu[start_pos:next_fn if next_fn > 0 else None]
        begin_pos = body.find("BeginSegmentedSnapshotFrame")
        # Sticky-block clearing may call eligibility before begin; the
        # FrameActive decision must still begin-then-eligible.
        active_pos = body.find("FrameActive")
        eligible_after_begin = body.find("IsMetalFullGpuFrameEligible", begin_pos)
        if begin_pos < 0 or eligible_after_begin < 0 or active_pos < 0:
            issues.append(
                "snapshot begin must occur before eligibility in Start3DRendering"
            )
        elif not (begin_pos < eligible_after_begin < active_pos or
                  begin_pos < eligible_after_begin):
            issues.append(
                "snapshot begin must occur before eligibility in Start3DRendering"
            )
        # Require staticEligible assignment after begin.
        if "staticEligible" not in body or body.find("staticEligible") < begin_pos:
            issues.append("staticEligible must be computed after snapshot begin")
        if "CancelSegmentedSnapshotFrame" not in body:
            issues.append("FrameActive=false must cancel unsubmitted snapshot slots")
        if "NextSnapshotEpoch" not in full_gpu:
            issues.append("renderer-owned NextSnapshotEpoch missing")
        if "ActiveSnapshotEpoch" not in full_gpu:
            issues.append("ActiveSnapshotEpoch missing")

    # Eligibility must not gate on SegmentedSnapshotReady (bootstrap deadlock).
    elig_fn = full_gpu.find("bool MetalRenderer::IsMetalFullGpuFrameEligible")
    if elig_fn >= 0:
        elig_end = full_gpu.find("\nvoid MetalRenderer::", elig_fn + 1)
        elig_body = full_gpu[elig_fn:elig_end if elig_end > 0 else None]
        if "SegmentedSnapshotReady" in elig_body:
            issues.append(
                "IsMetalFullGpuFrameEligible must not require SegmentedSnapshotReady"
            )
        if "BeginSegmentedSnapshotFrame" in elig_body:
            issues.append("IsMetalFullGpuFrameEligible must not begin snapshot frames")

    methods = (ROOT / "src/GPU2D_MetalFullGpuMethods.inc").read_text(encoding="utf-8")
    if "BeginSegmentSnapshotFrameIfNeeded" in methods:
        issues.append("line-0 BeginSegmentSnapshotFrameIfNeeded helper must be removed")
    if "line != 0" in methods and "BeginSegmentedSnapshotFrame" not in methods:
        issues.append("snapshot begin still depends exclusively on line 0")

    # SegmentedFrameComplete must remain on the VBlank/segment path.
    sched = (ROOT / "src/GPU_MetalSegmentScheduler.inc").read_text(encoding="utf-8")
    if "SegmentedFrameComplete" not in sched:
        issues.append("SegmentedFrameComplete must remain on VBlank segment path")

    metal_mm = (ROOT / "src/GPU_Metal.mm").read_text(encoding="utf-8")
    if "CPU output remains available" in metal_mm:
        issues.append("stale CPU fallback initialization log remains")
    if "stable CPU compositor path remains active" in metal_mm:
        issues.append("stale CPU compositor fallback log remains")
    if "capture frames remain on the CPU path" in metal_mm:
        issues.append("stale capture CPU-path fallback log remains")
    if "no displayable MetalTexture path is available" not in metal_mm:
        issues.append("Init failure must log MetalTexture-unavailable message")
    # Init must return false on required stage failure (not always true).
    init_pos = metal_mm.find("bool MetalRenderer::Init()")
    if init_pos < 0:
        issues.append("MetalRenderer::Init missing")
    else:
        init_end = metal_mm.find("\nvoid MetalRenderer::", init_pos + 1)
        init_body = metal_mm[init_pos:init_end if init_end > 0 else None]
        if "return false" not in init_body:
            issues.append("MetalRenderer::Init must return false on stage failure")
        if "ConfigureMetalVisibleOutput" not in init_body:
            issues.append("Init must require ConfigureMetalVisibleOutput")
        if "InitializeMetalFullGpuOutput" not in init_body:
            issues.append("Init must require InitializeMetalFullGpuOutput")
        if "ConfigureMetalCaptureState" not in init_body:
            issues.append("Init must require ConfigureMetalCaptureState")

    presenter = (ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm").read_text(
        encoding="utf-8"
    )
    if "startupBlackClearDone" not in presenter:
        issues.append("presenter startup black clear state missing")
    if "clearing stale splash" not in presenter:
        issues.append("presenter must log startup black clear for stale splash")

    if issues:
        print("FAIL: metal frame bootstrap audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal frame bootstrap audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
