#!/usr/bin/env python3
"""PR-11: HUD primitive renderer (Metal HUD draw-command path).

Custom HUD/OSD/splash/radar content must be encoded through a real Metal
draw-command list (a fixed-size, no-per-frame-heap-allocation array of quad
commands + a single encode loop) rather than ad hoc inline
setRenderPipelineState/drawPrimitives call sites scattered through
drawScreen(). This is the explicitly-sanctioned "at least route main HUD
output through Metal textured quads" interim tier (documented as transitional
in the PR-11 progress note) -- QPainter still rasterizes HUD/OSD/splash
content into the CPU `uiOverlay` buffer; this audit only guards the
draw-command-list plumbing that turns that buffer (and the PR-10 radar
sample) into actual GPU draw calls. See §15 of
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MARKER = "MELONPRIME_METAL_HUD_COMMAND_LIST_V1"


def live_lines(text: str) -> list[str]:
    out = []
    for line in text.splitlines():
        stripped = line.lstrip()
        if stripped.startswith("//"):
            continue
        out.append(line)
    return out


def main() -> int:
    issues: list[str] = []

    presenter_path = ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm"
    presenter = presenter_path.read_text(encoding="utf-8")
    presenter_live = live_lines(presenter)
    presenter_live_text = "\n".join(presenter_live)

    if MARKER not in presenter:
        issues.append(f"MelonPrimeScreenMetal.mm missing {MARKER} marker")

    if "struct MetalHudDrawCommand" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: MetalHudDrawCommand struct not found")

    if "void EncodeMetalHudCommand(" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: EncodeMetalHudCommand() encode function not found")

    # Fixed-size storage only -- no per-frame heap allocation for the command
    # list (std::vector would allocate; std::array does not).
    if "std::array<MetalHudDrawCommand" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: HUD command list must use fixed-size "
            "std::array storage, not a per-frame heap allocation"
        )
    if "std::vector<MetalHudDrawCommand" in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: HUD command list must not use "
            "std::vector (per-frame heap allocation on a hot path)"
        )

    # Both the UI overlay quad and the PR-10 radar quad must be routed
    # through the command list (pushed, not encoded inline at their own call
    # sites) and drained by exactly one encode loop before endEncoding.
    if "hudCommands[hudCommandCount++]" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: draw sites must push into the HUD "
            "command list (hudCommands[hudCommandCount++])"
        )
    if presenter_live_text.count("EncodeMetalHudCommand(encoder,") != 1:
        issues.append(
            "MelonPrimeScreenMetal.mm: expected exactly one HUD command-list "
            "encode loop call site"
        )

    # The old direct-encode call sites (setRenderPipelineState immediately
    # followed by drawPrimitives at the same call site for the UI/radar
    # pipelines) must be gone -- this exact unit-quad draw call
    # (vertexStart:0 vertexCount:6) must appear exactly once now, inside
    # EncodeMetalHudCommand()'s single body. The DS screen quad loop uses a
    # different (dynamic) vertexStart and is unaffected.
    draw_needle = "drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6"
    if presenter_live_text.count(draw_needle) != 1:
        issues.append(
            f"MelonPrimeScreenMetal.mm: expected exactly one {draw_needle!r} "
            "call site (inside EncodeMetalHudCommand()); found "
            f"{presenter_live_text.count(draw_needle)}"
        )

    if issues:
        print("FAIL: metal HUD command list audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal HUD command list audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
