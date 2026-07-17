#!/usr/bin/env python3
"""PR-9: presenter MetalTexture-only.

On the Metal present path: no CpuBgra acceptance as normal output, no
screenTex CPU upload for DS screens, no startup grace that prefers CpuBgra
over a real MetalTexture. See §15 of
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MARKER = "MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1"


def live_lines(text: str) -> list[str]:
    """Non-comment source lines (mirrors the other metal audits' convention:
    a line is "live" unless its first non-whitespace characters are `//`)."""
    out = []
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        out.append(line)
    return out


def main() -> int:
    issues: list[str] = []

    gpu_metal_path = ROOT / "src/GPU_Metal.mm"
    gpu_metal = gpu_metal_path.read_text(encoding="utf-8")
    gpu_metal_live_text = "\n".join(live_lines(gpu_metal))
    if MARKER not in gpu_metal:
        issues.append(f"GPU_Metal.mm missing {MARKER} marker")

    # AcquireOutputLease()/GetOutput() must never construct or return a
    # CpuBgra output -- MetalTexture or an empty/None lease only.
    for needle in ("RendererOutputKind::CpuBgra", "RendererOutput::CpuBgra("):
        if needle in gpu_metal_live_text:
            issues.append(f"GPU_Metal.mm live code must not reference {needle!r}")

    presenter_path = ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm"
    presenter = presenter_path.read_text(encoding="utf-8")
    presenter_live = live_lines(presenter)
    presenter_live_text = "\n".join(presenter_live)

    if MARKER not in presenter:
        issues.append(f"MelonPrimeScreenMetal.mm missing {MARKER} marker")

    # The CPU-upload DS screen texture (`screenTex`) and its 256x192
    # replaceRegion uploads must be fully removed, not just unused.
    for needle in (
        "screenTex",
        "hasCpuBaseFallbackForFrame",
        "topCpuBufForFrame ? topCpuBufForFrame",
    ):
        if needle in presenter_live_text:
            issues.append(
                f"MelonPrimeScreenMetal.mm live code still references {needle!r}"
            )

    # The old 180-frame "CpuBgra is a legitimate startup fallback" grace
    # window must be gone; a CpuBgra output must never become displayable
    # content (must never assign into finalMetalTextureForFrame).
    if "kStartupGraceFrames = 180" in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm still has the 180-frame CpuBgra startup grace"
        )

    cpubgra_branch_start = presenter.find(
        "else if (output.Kind == melonDS::RendererOutputKind::CpuBgra)"
    )
    if cpubgra_branch_start == -1:
        issues.append("MelonPrimeScreenMetal.mm: CpuBgra branch not found")
    else:
        # Body of the `else if (... CpuBgra)` block: up to the next
        # top-level `if (!finalMetalTextureForFrame)` fallback-chain check.
        branch_end = presenter.find(
            "if (!finalMetalTextureForFrame)", cpubgra_branch_start
        )
        branch = presenter[cpubgra_branch_start:branch_end] if branch_end != -1 else presenter[cpubgra_branch_start:cpubgra_branch_start + 2000]
        if "finalMetalTextureForFrame =" in branch:
            issues.append(
                "MelonPrimeScreenMetal.mm: CpuBgra branch must never assign "
                "finalMetalTextureForFrame (CpuBgra must never become "
                "displayable content)"
            )
        if "MetalStrictGpuOnlyViolation" not in branch:
            issues.append(
                "MelonPrimeScreenMetal.mm: CpuBgra branch must report a "
                "strict-mode violation when Metal is selected"
            )

    if issues:
        print("FAIL: metal presenter MetalTexture-only audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal presenter MetalTexture-only audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
