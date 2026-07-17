#!/usr/bin/env python3
"""PR-10: radar native Metal.

The Metal presenter must sample the final MetalTexture's bottom layer
(array index 1) with a circle-mask fragment shader, matching the GL-native
btmOverlay path's semantics (MelonPrimeHudScreenCppOverlayOfGl.inc /
kBtmOverlayFS in main_shaders.h). No CPU bottom-screen buffer (`bottomImage`)
or memcpy compositing may remain on the Metal path -- see §15 of
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MARKER = "MELONPRIME_METAL_RADAR_NATIVE_V1"


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

    presenter_path = ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm"
    presenter = presenter_path.read_text(encoding="utf-8")
    presenter_live = live_lines(presenter)
    presenter_live_text = "\n".join(presenter_live)

    if MARKER not in presenter:
        issues.append(f"MelonPrimeScreenMetal.mm missing {MARKER} marker")

    # The CPU bottom-screen composite buffer and its memcpy must be fully
    # removed, not just unused -- this is the whole point of PR-10.
    for needle in ("bottomImage", "bottomCpuBufForFrame", "hasHudCpuBuffersForFrame", "GetFramebuffers("):
        if needle in presenter_live_text:
            issues.append(
                f"MelonPrimeScreenMetal.mm live code still references {needle!r} "
                "(CPU bottom-screen composite must be fully removed)"
            )

    # The radar fragment shader must sample a texture2d_array (the DS
    # top/bottom array texture), not a plain 2D texture, and must read the
    # bottom layer (array index 1).
    if "mp_radar_fs" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: mp_radar_fs shader function not found")
    if "texture2d_array<float> tex [[texture(0)]]" not in presenter or "mp_radar_fs" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: mp_radar_fs must sample a texture2d_array")
    if "tex.sample(samp, srcUV, uint(1))" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: mp_radar_fs must sample array layer 1 (bottom screen)")

    # Circle mask (matches kBtmOverlayFS's `dist > 1.0` discard) must be present.
    if "if (dist > 1.0) discard_fragment();" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: mp_radar_fs missing circle-mask discard")

    # The radar draw call must source directly from the renderer's own final
    # MetalTexture for the frame, not from any CPU-staged texture.
    if "setFragmentTexture:finalMetalTextureForFrame atIndex:0" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: radar draw call must bind "
            "finalMetalTextureForFrame directly"
        )

    # CustomHud_Render must be called with btmBuffer=nullptr on the Metal
    # path now (matching the GL-native path) -- the crop-from-CPU-buffer
    # radar draw inside DrawBottomScreenOverlay() must never run here.
    if "&m->uiOverlay, nullptr," not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: CustomHud_Render must be called with "
            "btmBuffer=nullptr (no CPU bottom-screen buffer)"
        )

    if issues:
        print("FAIL: metal radar native audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal radar native audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
