#!/usr/bin/env python3
"""PR-12: OSD/splash rendered via native per-item Metal textures.

Splash logo/text and OSD toast items must no longer be QPainter-composited
into the CPU `uiOverlay` buffer on Metal frames. Each item gets its own
Metal texture (uploaded once, on change, via ScreenPanelMetal::osdRenderItem
-- mirroring ScreenPanelGL's per-item GL texture cache) and is drawn as an
individual textured quad through the PR-11 HUD command list. Only the
custom HUD (gauges/crosshair/etc.) still rasterizes through QPainter into
`uiOverlay`.
"""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

MARKER = "MELONPRIME_METAL_OSD_SPLASH_NATIVE_V1"


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
    header_path = ROOT / "src/frontend/qt_sdl/MelonPrimeScreenMetal.h"
    presenter = presenter_path.read_text(encoding="utf-8")
    header = header_path.read_text(encoding="utf-8")
    presenter_live = live_lines(presenter)
    presenter_live_text = "\n".join(presenter_live)

    if MARKER not in presenter and MARKER not in header:
        issues.append(f"MelonPrimeScreenMetal.mm/.h missing {MARKER} marker")

    if "struct MetalOsdTexture" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: MetalOsdTexture struct not found")

    if "std::unordered_map<unsigned int, MetalOsdTexture> osdTextures" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: expected a per-item id<MTLTexture> "
            "cache (std::unordered_map<unsigned int, MetalOsdTexture> osdTextures)"
        )

    if "void ScreenPanelMetal::osdRenderItem(OSDItem* item)" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: osdRenderItem() override not found")
    if "void ScreenPanelMetal::osdDeleteItem(OSDItem* item)" not in presenter:
        issues.append("MelonPrimeScreenMetal.mm: osdDeleteItem() override not found")
    if "void osdRenderItem(OSDItem* item) override;" not in header:
        issues.append("MelonPrimeScreenMetal.h: osdRenderItem() override declaration not found")
    if "void osdDeleteItem(OSDItem* item) override;" not in header:
        issues.append("MelonPrimeScreenMetal.h: osdDeleteItem() override declaration not found")

    # The old per-frame QPainter compositing of splash/OSD content into
    # uiOverlay must be gone entirely -- these drawImage/drawPixmap call
    # sites (on overlayPainter, for the logo pixmap / OSDItem bitmaps) are
    # the exact CPU-composite pattern this PR replaces with GPU textured
    # quads.
    banned_needles = [
        "overlayPainter.drawPixmap(",
        "overlayPainter.drawImage(",
    ]
    for needle in banned_needles:
        if needle in presenter_live_text:
            issues.append(
                f"MelonPrimeScreenMetal.mm: {needle!r} must not appear -- "
                "splash/OSD must draw via Metal textured quads, not "
                "QPainter-into-uiOverlay compositing"
            )

    # Splash logo and each OSD/splash-text item must be pushed into the same
    # fixed-size HUD command list PR-11 introduced, not drawn ad hoc.
    if "pushMetalTexturedQuad(m->logoTex" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: splash logo must be pushed into the "
            "HUD command list via the per-item textured-quad helper"
        )
    if "pushMetalTexturedQuad(texIt->second.texture" not in presenter_live_text:
        issues.append(
            "MelonPrimeScreenMetal.mm: OSD/splash-text items must be pushed "
            "into the HUD command list via the per-item textured-quad helper"
        )

    # The splash/OSD quads must be pushed after the custom-HUD overlay quad
    # and the PR-10 radar quad (draw order = paint order under alpha
    # blending), matching the GL-native path where MelonPrimeHudScreenCpp
    # OverlayOfGl.inc's overlay+radar pass runs before ScreenPanelGL::
    # drawScreen()'s osdShader passes.
    ui_push_idx = presenter_live_text.find("cmd.texture = m->uiTex;")
    radar_push_idx = presenter_live_text.find("cmd.texture = finalMetalTextureForFrame;")
    splash_push_idx = presenter_live_text.find("pushMetalTexturedQuad(m->logoTex")
    if ui_push_idx == -1:
        issues.append("MelonPrimeScreenMetal.mm: UI overlay quad push site not found")
    if radar_push_idx == -1:
        issues.append("MelonPrimeScreenMetal.mm: radar quad push site not found")
    if splash_push_idx == -1:
        issues.append("MelonPrimeScreenMetal.mm: splash logo push site not found")
    if -1 not in (ui_push_idx, splash_push_idx) and splash_push_idx < ui_push_idx:
        issues.append(
            "MelonPrimeScreenMetal.mm: splash/OSD quads must be pushed after "
            "the UI overlay quad (draw-order regression vs GL-native path)"
        )
    if -1 not in (radar_push_idx, splash_push_idx) and splash_push_idx < radar_push_idx:
        issues.append(
            "MelonPrimeScreenMetal.mm: splash/OSD quads must be pushed after "
            "the radar quad (draw-order regression vs GL-native path)"
        )

    if issues:
        print("FAIL: metal OSD/splash native audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal OSD/splash native audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
