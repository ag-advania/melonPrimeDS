#!/usr/bin/env python3
"""Audit the ColorImage write-after-read dependency between 3D frames.

VulkanRenderer3D's ColorImage is written as a color attachment by the 3D
renderer and read as a storage image by MelonPrimeVulkanOutput's compute
compositor. Those two alternate on the same image:

    frame N   3D render          color attachment write
    frame N   compositor         compute shader read
    frame N+1 3D render          color attachment write   <-- must wait

The barrier that reuses ColorImage as a color attachment carries
VK_ACCESS_SHADER_READ_BIT in its source access mask, which reads as if the
compositor were covered. It is not, unless the stage that performed the read is
also in the source stage mask: a Vulkan access scope is the intersection of the
stage mask and the access mask. With only FRAGMENT_SHADER named, the compute
compositor's read fell outside the dependency and the next frame's 3D render
could overwrite the image it was still sampling -- this frame's structured 2D
composited against the next frame's 3D.

This is invisible in review because the access mask alone looks right, and it
only reproduces when the GPU actually overlaps the two submissions. So it is
pinned here.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

RENDERER = ROOT / "src/GPU3D_Vulkan.cpp"
COMPOSITOR_SHADER = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanCompositorShader.comp"
COMPOSITOR = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanOutput.cpp"


# The identifiers below are unique in the file, so the checks run against the
# whole translation unit rather than a fragile window around the barrier.


def main() -> int:
    failures: list[str] = []

    renderer = RENDERER.read_text(encoding="utf-8")
    shader = COMPOSITOR_SHADER.read_text(encoding="utf-8")
    compositor = COMPOSITOR.read_text(encoding="utf-8")

    # 1. The compositor really does read the 3D color target from a compute
    #    shader. If that ever stops being true the dependency below is moot,
    #    and this audit should be revisited rather than silently passing.
    if "uniform readonly image2D image3dInput" not in shader:
        failures.append(
            f"{COMPOSITOR_SHADER.name}: the compositor no longer declares the 3D "
            "color target as a readonly storage image; revisit this audit")
    if "imageLoad(image3dInput" not in shader:
        failures.append(
            f"{COMPOSITOR_SHADER.name}: the compositor no longer samples "
            "image3dInput; revisit this audit")
    if "GetColorTargetImageView()" not in compositor:
        failures.append(
            f"{COMPOSITOR.name}: the compositor no longer binds the 3D renderer's "
            "color target; revisit this audit")

    # 2..6. The barrier that reuses ColorImage as a color attachment.
    block = renderer
    if "rasterAttachmentBarriers[0].image = ColorImage;" not in renderer:
        failures.append(
            f"{RENDERER.name}: could not find the ColorImage color-attachment "
            "reuse barrier (rasterAttachmentBarriers[0])")
    else:
        source_stage = re.search(
            r"const VkPipelineStageFlags rasterAttachmentSrcStage[^;]*;", block, re.DOTALL)
        if source_stage is None:
            failures.append(
                f"{RENDERER.name}: rasterAttachmentSrcStage is gone; the ColorImage "
                "reuse dependency cannot be verified")
        else:
            stages = source_stage.group(0)
            if "VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT" not in stages:
                failures.append(
                    f"{RENDERER.name}: rasterAttachmentSrcStage does not include "
                    "VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, so the compositor's "
                    "compute read of ColorImage is outside the source scope and "
                    "the next 3D frame may overwrite it mid-read")
            if "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT" not in stages:
                failures.append(
                    f"{RENDERER.name}: rasterAttachmentSrcStage lost "
                    "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT")

        access = re.search(
            r"rasterAttachmentBarriers\[0\]\.srcAccessMask[^;]*;", block, re.DOTALL)
        if access is None or "VK_ACCESS_SHADER_READ_BIT" not in access.group(0):
            failures.append(
                f"{RENDERER.name}: rasterAttachmentBarriers[0].srcAccessMask no "
                "longer covers VK_ACCESS_SHADER_READ_BIT")

        destination = re.search(
            r"rasterAttachmentBarriers\[0\]\.dstAccessMask[^;]*;", block, re.DOTALL)
        if destination is None or "VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT" not in destination.group(0):
            failures.append(
                f"{RENDERER.name}: rasterAttachmentBarriers[0].dstAccessMask no "
                "longer covers VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT")

        old_layout = re.search(
            r"rasterAttachmentBarriers\[0\]\.oldLayout[^;]*;", block, re.DOTALL)
        new_layout = re.search(
            r"rasterAttachmentBarriers\[0\]\.newLayout[^;]*;", block, re.DOTALL)
        if old_layout is None or "VK_IMAGE_LAYOUT_GENERAL" not in old_layout.group(0):
            failures.append(
                f"{RENDERER.name}: the ColorImage reuse barrier no longer starts "
                "from VK_IMAGE_LAYOUT_GENERAL, which is the layout the compositor "
                "reads it in")
        if new_layout is None or "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL" not in new_layout.group(0):
            failures.append(
                f"{RENDERER.name}: the ColorImage reuse barrier no longer targets "
                "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL")

        barrier_call = re.search(
            r"vkCmdPipelineBarrier\(\s*commandBuffer,\s*rasterAttachmentSrcStage,\s*([^,]+),",
            block, re.DOTALL)
        if barrier_call is None:
            failures.append(
                f"{RENDERER.name}: rasterAttachmentSrcStage is no longer the source "
                "stage of the ColorImage reuse barrier")
        elif "VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT" not in barrier_call.group(1):
            failures.append(
                f"{RENDERER.name}: the ColorImage reuse barrier's destination stage "
                "no longer covers VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT")

    if failures:
        print("Vulkan ColorImage synchronization audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Vulkan ColorImage synchronization OK: the compositor's compute read is "
        "inside the source scope of the color-attachment reuse barrier "
        "(GENERAL -> COLOR_ATTACHMENT_OPTIMAL)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
