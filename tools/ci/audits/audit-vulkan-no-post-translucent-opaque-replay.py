#!/usr/bin/env python3
"""Audit the Vulkan 3D renderer's draw pass order.

The contract, matching ComputeRenderer3D's polygon semantics:

    1. opaque polygons          exactly one color draw each
    2. shadow mask / shadow     DS stencil semantics
    3. translucent polygons     with NeedOpaquePass for opaque texels
    4. edge marking / fog / final

`dispatchGraphicsRasterAndReadback()` used to run a fifth pass after step 3
called PaletteUiOpaqueReplay. It walked GraphicsOpaqueDrawIndices a second time,
picked draws that "looked like" menu UI -- palette texture format, clamped
wrapping, a flat W plane at 25600, specific texture pages and texParam values,
screen-coordinate boxes -- and redrew them with polyAttr bit 14 forced on, which
also changed the depth compare of the pipeline they were drawn with.

Large flat background polygons satisfy those same conditions. On frames where
the predicate matched, the background was redrawn on top of the menu it belongs
behind; because the predicate reads the frame's own translucent overlay list, it
alternated with correct frames. ComputeRenderer3D has no such pass, which is why
OpenGL Compute never showed it.

Removing it is not enough on its own: the failure mode is easy to reintroduce
one heuristic at a time, and it is invisible in a diff that only adds "one more
special case". So the shape is pinned here.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

RENDERER = ROOT / "src/GPU3D_Vulkan.cpp"
RENDERER_HEADER = ROOT / "src/GPU3D_Vulkan.h"
COMPOSITOR = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanOutput.cpp"

RASTER_FUNCTION = "VulkanRenderer3D::dispatchGraphicsRasterAndReadback"

# Names the replay pass owned. None may come back.
RETIRED_SYMBOLS = (
    "shouldReplayOpaquePaletteUiDraw",
    "GraphicsOpaqueUiOverlayPipelines",
    "PaletteUiOpaqueReplay",
    "paletteUiOpaqueReplay",
    "replayDraw",
    "isClampPaletteUiTriangle",
    "isCompactPaletteUiReplayTriangle",
    "isFlatDsUiPlaneTriangle",
    "isCompactTopStatusGlyphTriangle",
    "isCompactTopStatusGlyphDraw",
    "isCompactTopStatusGlyphOverlay",
    "isTranslucentPaletteUiOverlay",
    "isPaletteUiHelpPanelOverlay",
    "hasLowAlphaPaletteUiOverlay",
    "paletteUiGate",
    "PaletteUiGate",
)

# Constants that only exist to recognise one game's menu artwork.
RETIRED_CONSTANTS = (
    "0x05C0",
    "0x85C0",
    "0x6DC00200",
    "0x68C01B10",
    "0x6A5016D0",
    "kUiPlaneW",
)

# Passes that must survive: these are DS semantics, not UI inference.
REQUIRED_SYMBOLS = (
    "AcceleratedPolygonFlagNeedOpaquePass",
    "GraphicsNeedOpaqueDrawIndices",
    "drawNeedOpaquePass",
    "GraphicsShadowMaskPipelines",
    "GraphicsShadowBlendPipelines",
    "GraphicsShadowClearPipelines",
    "clearShadowStencilBit",
    "GraphicsFinalEdgePipeline",
    "GraphicsFinalFogPipeline",
    "GraphicsFinalEdgeFogPipeline",
)


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", "", text)


def extract_function(text: str, signature: str) -> str:
    start = text.index(signature)
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    raise ValueError(f"unterminated function {signature}")


def main() -> int:
    failures: list[str] = []

    renderer_raw = RENDERER.read_text(encoding="utf-8")
    header_raw = RENDERER_HEADER.read_text(encoding="utf-8")
    renderer = strip_comments(renderer_raw)
    header = strip_comments(header_raw)

    # 1..4. The replay pass and its helpers are gone. Comments are stripped
    #       first so the explanation of why it was removed may name it.
    for symbol in RETIRED_SYMBOLS:
        for name, text in ((RENDERER.name, renderer), (RENDERER_HEADER.name, header)):
            if symbol in text:
                failures.append(
                    f"{name}: {symbol} is back; opaque polygons are drawn once, in "
                    "the opaque pass, and never replayed after the translucent pass")

    # 5. No UI inference by texture address or plane geometry.
    for constant in RETIRED_CONSTANTS:
        if constant in renderer:
            failures.append(
                f"{RENDERER.name}: {constant} is back; the renderer must not "
                "recognise specific game artwork to decide draw order")

    try:
        raster = strip_comments(extract_function(renderer_raw, RASTER_FUNCTION))
    except ValueError as error:
        print(f"{RENDERER.name}: {error}", file=sys.stderr)
        return 1

    # 6. Exactly one color-draw loop over the opaque list, and it must come
    #    before the translucent work rather than after it.
    opaque_loops = [
        match.start()
        for match in re.finditer(r"for \([^)]*GraphicsOpaqueDrawIndices\)", raster)
    ]
    if len(opaque_loops) != 1:
        failures.append(
            f"{RENDERER.name}: {RASTER_FUNCTION} walks GraphicsOpaqueDrawIndices "
            f"{len(opaque_loops)} times; opaque polygons must be drawn exactly "
            "once, in a single pass")
    else:
        translucent_draws = [
            match.start()
            for match in re.finditer(r"bindAndDrawGraphics\([^;]*0x7Fu", raster)
        ]
        if translucent_draws and opaque_loops[0] > min(translucent_draws):
            failures.append(
                f"{RENDERER.name}: the opaque draw loop runs after a translucent "
                "draw; opaque color polygons must not be issued once the "
                "translucent pass has begun")

    # 7. No drawing from a modified copy of a polygon's attributes. The replay
    #    pass forced polyAttr bit 14, which silently changed the depth compare.
    mutations = re.findall(r"^.*\.polyAttr\s*(?:\|=|&=|\^=|=[^=]).*$", raster, re.M)
    if mutations:
        failures.append(
            f"{RENDERER.name}: {RASTER_FUNCTION} mutates polyAttr ("
            f"{mutations[0].strip()}); polygons must be drawn with the attributes "
            "the DS gave them")

    # 8. The DS passes that are easy to mistake for the removed one survive.
    #    Matched on word boundaries: renaming drawNeedOpaquePass to something
    #    else still removes the pass this is protecting.
    for symbol in REQUIRED_SYMBOLS:
        if re.search(rf"\b{re.escape(symbol)}\b", renderer) is None:
            failures.append(
                f"{RENDERER.name}: {symbol} is gone; NeedOpaquePass, shadow, edge "
                "and fog are DS semantics and must be kept")

    # 9. The pass order is written down where it is enforced, so the next reader
    #    knows the single opaque loop is load-bearing.
    if "Draw pass order. This is a contract" not in renderer_raw:
        failures.append(
            f"{RENDERER.name}: the draw pass order contract comment is gone; it "
            "documents why opaque polygons may not be redrawn after translucent")

    # 10. This is a 3D-renderer fix. The compositor is not involved.
    compositor = COMPOSITOR.read_text(encoding="utf-8")
    if "PaletteUi" in compositor or "OpaqueReplay" in compositor:
        failures.append(
            f"{COMPOSITOR.name}: the structured compositor references the removed "
            "replay pass; draw order belongs to the 3D renderer")

    if failures:
        print("Vulkan draw pass order audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Vulkan draw pass order OK: opaque polygons are drawn once before the "
        "translucent pass, with no post-translucent replay and no UI inference "
        "from texture addresses, coordinates or W values")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
