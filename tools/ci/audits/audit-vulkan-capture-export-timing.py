#!/usr/bin/env python3
"""Audit when the Vulkan renderer produces its display-capture 3D source.

The DS arms display capture at VCount 0:

    GPU.cpp   if (VCount == 0) { if (CaptureCnt & (1<<31)) { CaptureEnable = true; ... } }

but the 3D frame is rendered at VCount 215 of the preceding sweep:

    GPU.cpp   else if (VCount == 215) { Rend->Start3DRendering(); }

So at render time it is not yet known whether this frame's 3D will be needed as
display-capture source A. A game may write DISPCAPCNT any time between those two
points and the arm is still valid.

The Vulkan backend used to decide at VCount 215, from CaptureCnt bit 31, whether
to produce a capture export at all -- and if it decided no, the export could not
be produced afterwards: PrepareCaptureFrame() explicitly refused a "late" export
and cleared the line cache instead. Software 2D then ran the capture at VCount 0
with no 3D component, wrote that into VRAM, and a later frame displayed that VRAM
as a background. That is a prediction, and the reference renderers do not make it:

    DX12Renderer3D::GetLine()  -> EnsureFrameReadback()   (at the moment of the request)
    GLRenderer::DoCapture()    -> picks OutputTex3D       (when the capture runs)

This audit pins the corrected contract: the export is produced when capture
actually asks, from the frame's own ColorImage, and is refused only when the
image cannot be attributed to the current frame.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

RENDERER = ROOT / "src/GPU3D_Vulkan.cpp"
RENDERER_HEADER = ROOT / "src/GPU3D_Vulkan.h"
DX12 = ROOT / "src/GPU3D_DX12.cpp"
GPU = ROOT / "src/GPU.cpp"


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
    renderer = strip_comments(renderer_raw)
    header = strip_comments(RENDERER_HEADER.read_text(encoding="utf-8"))
    gpu = strip_comments(GPU.read_text(encoding="utf-8"))
    dx12 = strip_comments(DX12.read_text(encoding="utf-8"))

    # 1. The premise. If the DS timing this is built on ever changes, the whole
    #    audit needs rewriting rather than silently passing.
    if "VCount == 215" not in gpu or "Start3DRendering()" not in gpu:
        failures.append(
            f"{GPU.name}: 3D rendering no longer starts at VCount 215; revisit "
            "this audit")
    capture_arm = re.search(
        r"if \(VCount == 0\)\s*\{\s*if \(CaptureCnt & \(1<<31\)\)", gpu)
    if capture_arm is None:
        failures.append(
            f"{GPU.name}: display capture is no longer armed from CaptureCnt bit "
            "31 at VCount 0; revisit this audit")

    # 2. The reference contract this is matched to.
    if "EnsureFrameReadback();" not in extract_function(dx12, "u32* DX12Renderer3D::GetLine("):
        failures.append(
            f"{DX12.name}: GetLine no longer pulls the readback on demand; the "
            "Vulkan late-export path was matched to this contract")

    # 3. The late export exists and is attributed to the current frame.
    if "bool VulkanRenderer3D::ensureExactCaptureExportForCurrentFrame()" not in renderer:
        print(
            f"{RENDERER.name}: ensureExactCaptureExportForCurrentFrame() is gone; "
            "a frame whose capture is armed after VCount 215 has no 3D source",
            file=sys.stderr)
        return 1

    ensure = strip_comments(extract_function(
        renderer_raw, "bool VulkanRenderer3D::ensureExactCaptureExportForCurrentFrame()"))
    if "ColorImageHasCurrentFrame3D" not in ensure:
        failures.append(
            f"{RENDERER.name}: the late capture export does not check that "
            "ColorImage holds the current frame; it could export an older "
            "frame's 3D onto the capture-backed screen")
    if "submitGraphicsCaptureExportForCurrentFrame()" not in ensure:
        failures.append(
            f"{RENDERER.name}: the late capture export no longer produces an "
            "export from this frame's ColorImage")

    # 4. Both places that consume a capture line try the late export before
    #    reporting no 3D coverage.
    for function in (
        "void VulkanRenderer3D::PrepareCaptureFrameActiveBackend()",
        "u32* VulkanRenderer3D::GetLineActiveBackend(",
    ):
        body = strip_comments(extract_function(renderer_raw, function))
        if "ensureExactCaptureExportForCurrentFrame()" not in body:
            failures.append(
                f"{RENDERER.name}: {function.split('::')[1]} clears the line cache "
                "without first trying to produce this frame's export")
        for match in re.finditer(r"clearLineCache\(\);", body):
            window = body[max(0, match.start() - 200):match.start()]
            if "ensureExactCaptureExportForCurrentFrame()" not in window:
                failures.append(
                    f"{RENDERER.name}: {function.split('::')[1]} has a "
                    "clearLineCache() that is not guarded by a late-export attempt")

    # 5. The frame attribution flag has a lifetime: cleared before a render
    #    overwrites ColorImage, set once that render has completed.
    render = strip_comments(extract_function(
        renderer_raw, "void VulkanRenderer3D::RenderFrameActiveBackend("))
    if "ColorImageHasCurrentFrame3D = false;" not in render:
        failures.append(
            f"{RENDERER.name}: RenderFrameActiveBackend does not clear "
            "ColorImageHasCurrentFrame3D before overwriting ColorImage")
    # Two places legitimately mark the image current: the identical-frame reuse
    # early return, and the end of a completed render. The second is the one
    # that matters, so it is checked at its position rather than by presence --
    # the reuse path alone would keep this check green while every real render
    # left capture without a source.
    completion = re.search(
        r"LastSubmittedRenderPolygonCount = gpu\.GPU3D\.RenderNumPolygons;"
        r"(?:(?!ColorImageHasCurrentFrame3D)[\s\S])*?ColorImageHasCurrentFrame3D = true;",
        render)
    if completion is None:
        failures.append(
            f"{RENDERER.name}: RenderFrameActiveBackend does not mark ColorImage "
            "as holding the current frame once the render completes, so a late "
            "export can never succeed for a freshly rendered frame")
    reuse = re.search(
        r"if \(canReuseIdenticalFrame\)[\s\S]{0,400}?ColorImageHasCurrentFrame3D = true;",
        render)
    if reuse is None:
        failures.append(
            f"{RENDERER.name}: the identical-frame reuse path does not keep "
            "ColorImage marked as current, so capture armed late on a reused "
            "frame would find no source")
    if header.count("bool ColorImageHasCurrentFrame3D = false;") != 1:
        failures.append(
            f"{RENDERER_HEADER.name}: ColorImageHasCurrentFrame3D is not declared "
            "exactly once")

    # 6. Invalidating the color target must invalidate the attribution with it.
    initialized_false = renderer.count("ColorImageInitialized = false;")
    paired = len(re.findall(
        r"ColorImageInitialized = false;\s*\n\s*ColorImageHasCurrentFrame3D = false;",
        renderer))
    if paired != initialized_false:
        failures.append(
            f"{RENDERER.name}: {initialized_false - paired} site(s) invalidate "
            "ColorImageInitialized without clearing ColorImageHasCurrentFrame3D; "
            "a late export could then read a destroyed or stale color target")

    if failures:
        print("Vulkan capture export timing audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Vulkan capture export timing OK: the 3D capture source is produced when "
        "display capture asks for it, from the current frame's ColorImage, not "
        "predicted from CaptureCnt at VCount 215")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
