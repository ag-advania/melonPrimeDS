#!/usr/bin/env python3
"""PR-2 static gate: capture differential scaffold must stay side-channel.

Fails if HEAD regresses contracts from
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md §8 / §29.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

FULL_GPU = ROOT / "src/GPU_MetalFullGpuMethods.inc"
HEADER = ROOT / "src/GPU_Metal.h"
EXPERIMENT = ROOT / "src/GPU_MetalCaptureExperiment.inc"
MM = ROOT / "src/GPU_Metal.mm"
PRESENTER = ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm"


def live_lines(text: str):
    for i, line in enumerate(text.splitlines(), 1):
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        yield i, line


def main() -> int:
    issues: list[str] = []

    if not EXPERIMENT.exists():
        issues.append("missing GPU_MetalCaptureExperiment.inc")
    else:
        exp = EXPERIMENT.read_text(encoding="utf-8")
        for token in (
            "MELONPRIME_METAL_CAPTURE_EXPERIMENT",
            "mp_metal_capture_experiment",
            "MetalCaptureExperimentRecordLine",
            "FinishMetalCaptureExperimentFrame",
            "reference-native.rgb5551",
            "capture-lines.csv",
            "frame.json",
        ):
            if token not in exp:
                issues.append(f"GPU_MetalCaptureExperiment.inc missing {token}")

        # Candidate must never be published as RendererOutput.
        for i, line in live_lines(exp):
            if "RendererOutput" in line or "AcquireOutputLease" in line:
                if "never" in line.lower() or "comment" in line.lower():
                    continue
                if line.lstrip().startswith("//") or "note" in line.lower():
                    continue
                if "MetalTexture" in line or "CpuBgra" in line:
                    issues.append(
                        f"GPU_MetalCaptureExperiment.inc:{i}: "
                        "must not construct presenter output"
                    )

        # PR-7: MetalRenderer no longer inherits SoftRenderer, so this
        # scaffold must not read SoftRenderer's Output2D/Output3D anymore.
        for i, line in live_lines(exp):
            if re.search(r"\bOutput2D\b|\bOutput3D\b", line):
                issues.append(
                    f"GPU_MetalCaptureExperiment.inc:{i}: "
                    "live SoftRenderer Output2D/Output3D reference (PR-7 flip)"
                )

    header = HEADER.read_text(encoding="utf-8")
    if "class MetalRenderer : public SoftRenderer" in header:
        issues.append("MetalRenderer SoftRenderer inheritance must be gone (PR-7 flip)")
    if "MetalCaptureExperimentState" not in header:
        issues.append("GPU_Metal.h missing MetalCaptureExperimentState")

    full_gpu = FULL_GPU.read_text(encoding="utf-8")
    if "if (GPU.CaptureCnt & (1u << 31))" in full_gpu:
        issues.append("CaptureCnt Soft gate must stay removed after cutover")
    # PR-7: DrawScanline's Soft path (and its experiment record hook) is
    # gone along with SoftRenderer inheritance -- the hook must not be
    # live-called from here anymore (MetalCaptureExperimentRecordLine still
    # exists as a function in GPU_MetalCaptureExperiment.inc, just unused).
    for i, line in live_lines(full_gpu):
        if "MetalCaptureExperimentRecordLine" in line:
            issues.append(
                f"GPU_MetalFullGpuMethods.inc:{i}: DrawScanline must not call "
                "MetalCaptureExperimentRecordLine (Soft hook removed, PR-7)"
            )
        if "SoftRenderer::" in line:
            issues.append(
                f"GPU_MetalFullGpuMethods.inc:{i}: live SoftRenderer:: call (PR-7 flip)"
            )

    mm = MM.read_text(encoding="utf-8")
    if "GPU_MetalCaptureExperiment.inc" not in mm:
        issues.append("GPU_Metal.mm does not include capture experiment .inc")
    if "FinishMetalCaptureExperimentFrame" not in mm:
        issues.append("VBlank Soft path missing FinishMetalCaptureExperimentFrame")

    # Presenter must not gain a capture-experiment presentation path.
    presenter = PRESENTER.read_text(encoding="utf-8")
    if "CAPTURE_EXPERIMENT" in presenter:
        issues.append(
            "MelonPrimeScreenMetal.mm must not reference capture experiment "
            "(candidate stays side-channel)"
        )

    if issues:
        print("FAIL: metal capture experiment scaffold audit")
        for item in issues:
            print(f"  - {item}")
        return 1

    print("PASS: metal capture experiment scaffold audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
