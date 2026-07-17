#!/usr/bin/env python3
"""PR-15: forbidden-path static gates for 完了A (emulator GPU path full
Metal-ification).

These are regression gates, not new-feature audits: each check locks in a
property that PR-7/PR-8/PR-9 already established in the current code (see
docs/plans/melonPrimeDS_develop_metal_完全Metal化_進捗_17b46586.md §PR-7/8/9),
so a later change cannot silently reintroduce a SoftRenderer/Raster escape
hatch without failing CI.

1. `class MetalRenderer : public SoftRenderer` must never exist again
   (PR-7 flipped MetalRenderer to `public Renderer, public MetalRendererHost`).
   Checked with no comment carve-out -- a class declaration has no
   legitimate reason to appear in a comment, so any textual match fails.
2. Live (non-comment) `SoftRenderer::` calls must be 0 in src/GPU_Metal*
   (PR-7). Comments referencing `SoftRenderer::` for historical context are
   allowed.
3. `RasterReference` must never appear (comment or code) in
   src/GPU3D_MetalCompute* (PR-8 removed the nested raster-renderer member).
4. `MetalRenderer::AcquireOutputLease()` / `MetalRenderer::GetOutput()` in
   src/GPU_Metal.mm must never construct or return a CpuBgra output in live
   code, and must carry the PR-7/PR-9 coordination markers
   (MELONPRIME_METAL_HOST_V1, MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1)
   documenting why. This is function-scoped and intentionally narrower than
   audit-metal-presenter-metaltexture-only.py's file-wide CpuBgra check, to
   avoid duplicating that audit while still pinning the exact two entry
   points a raw CpuBgra escape would have to go through.
5. `CaptureFeedbackCooldownFrames` (a removed Soft-escape counter/knob) must
   never reappear anywhere under src/.
6. (Best-effort) the PR-14 bundled-metallib loader entry point
   `MelonPrimeMetalDefaultLibrary` must still exist.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
SRC = ROOT / "src"

HOST_MARKER = "MELONPRIME_METAL_HOST_V1"
PRESENT_MARKER = "MELONPRIME_METAL_PRESENT_METALTEXTURE_ONLY_V1"


def live_lines(text: str) -> list[str]:
    """Non-comment source lines (same convention as the other metal audits:
    a line is "live" unless its first non-whitespace characters are `//`).
    """
    out = []
    for line in text.splitlines():
        if line.lstrip().startswith("//"):
            continue
        out.append(line)
    return out


def extract_function_body(text: str, signature: str) -> str | None:
    """Return the brace-balanced body (including the signature line) of the
    first function whose signature text matches `signature`, or None if not
    found."""
    start = text.find(signature)
    if start == -1:
        return None
    brace_open = text.find("{", start)
    if brace_open == -1:
        return None
    depth = 0
    i = brace_open
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[start:i + 1]
        i += 1
    return None


def check_metal_renderer_soft_inheritance(issues: list[str]) -> None:
    pattern = re.compile(r"class\s+MetalRenderer\s*:\s*public\s+SoftRenderer\b")
    for path in sorted(SRC.rglob("*")):
        if not path.is_file() or path.suffix not in (".h", ".hpp", ".mm", ".cpp", ".inc"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if pattern.search(text):
            issues.append(
                f"{path.relative_to(ROOT)}: found forbidden "
                f"'class MetalRenderer : public SoftRenderer'"
            )


def check_gpu_metal_star_soft_renderer_calls(issues: list[str]) -> None:
    matched_any = False
    for path in sorted(SRC.glob("GPU_Metal*")):
        if not path.is_file():
            continue
        matched_any = True
        text = path.read_text(encoding="utf-8", errors="ignore")
        live_text = "\n".join(live_lines(text))
        if "SoftRenderer::" in live_text:
            for lineno, line in enumerate(text.splitlines(), start=1):
                if not line.lstrip().startswith("//") and "SoftRenderer::" in line:
                    issues.append(
                        f"{path.relative_to(ROOT)}:{lineno}: live "
                        f"'SoftRenderer::' call (only historical comments "
                        f"are allowed)"
                    )
    if not matched_any:
        issues.append(f"no src/GPU_Metal* files found under {SRC}")


def check_gpu3d_metalcompute_star_raster_reference(issues: list[str]) -> None:
    matched_any = False
    for path in sorted(SRC.glob("GPU3D_MetalCompute*")):
        if not path.is_file():
            continue
        matched_any = True
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "RasterReference" in text:
            issues.append(
                f"{path.relative_to(ROOT)}: found forbidden 'RasterReference' "
                f"symbol (PR-8 removed the nested raster renderer -- no "
                f"code or comment should reference it)"
            )
    if not matched_any:
        issues.append(f"no src/GPU3D_MetalCompute* files found under {SRC}")


def check_acquire_output_lease_and_get_output(issues: list[str]) -> None:
    path = SRC / "GPU_Metal.mm"
    if not path.is_file():
        issues.append(f"missing {path.relative_to(ROOT)}")
        return
    text = path.read_text(encoding="utf-8", errors="ignore")

    functions = {
        "AcquireOutputLease": "RendererOutputLease MetalRenderer::AcquireOutputLease()",
        "GetOutput": "RendererOutput MetalRenderer::GetOutput()",
    }

    for name, signature in functions.items():
        body = extract_function_body(text, signature)
        if body is None:
            issues.append(
                f"GPU_Metal.mm: could not locate MetalRenderer::{name}() body"
            )
            continue

        live_body = "\n".join(live_lines(body))
        if "CpuBgra" in live_body:
            issues.append(
                f"GPU_Metal.mm: MetalRenderer::{name}() has a live 'CpuBgra' "
                f"reference -- it must only ever produce a MetalTexture or "
                f"an empty/None output"
            )

        for marker in (HOST_MARKER, PRESENT_MARKER):
            if marker not in body:
                issues.append(
                    f"GPU_Metal.mm: MetalRenderer::{name}() is missing the "
                    f"{marker} coordination marker"
                )


def check_capture_feedback_cooldown_absent(issues: list[str]) -> None:
    for path in sorted(SRC.rglob("*")):
        if not path.is_file() or path.suffix not in (".h", ".hpp", ".mm", ".cpp", ".inc"):
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "CaptureFeedbackCooldownFrames" in text:
            issues.append(
                f"{path.relative_to(ROOT)}: found forbidden "
                f"'CaptureFeedbackCooldownFrames' Soft-escape"
            )


def check_metallib_default_library_helper(issues: list[str]) -> None:
    header = SRC / "MelonPrimeMetalLibrary.h"
    impl = SRC / "MelonPrimeMetalLibrary.mm"
    for path in (header, impl):
        if not path.is_file():
            issues.append(f"missing {path.relative_to(ROOT)}")
            continue
        text = path.read_text(encoding="utf-8", errors="ignore")
        if "MelonPrimeMetalDefaultLibrary" not in text:
            issues.append(
                f"{path.relative_to(ROOT)}: missing "
                f"MelonPrimeMetalDefaultLibrary helper"
            )


def main() -> int:
    issues: list[str] = []

    check_metal_renderer_soft_inheritance(issues)
    check_gpu_metal_star_soft_renderer_calls(issues)
    check_gpu3d_metalcompute_star_raster_reference(issues)
    check_acquire_output_lease_and_get_output(issues)
    check_capture_feedback_cooldown_absent(issues)
    check_metallib_default_library_helper(issues)

    if issues:
        print("FAIL: metal forbidden-path audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal forbidden-path audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
