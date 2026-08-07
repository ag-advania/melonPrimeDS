#!/usr/bin/env python3
"""Audit that every per-frame Vulkan resource is protected by its own fence.

MelonPrimeVulkanOutput keeps several frames in flight. Each one waits on its own
VkFence before the emulation thread rewrites its inputs. That is only a complete
statement if the inputs actually belong to that frame:

    resource A   compositor dispatch reading the packed planes
    resource B   acquireFrameForCpuWrite waits on B's fence, then writes ...

... whichever planes it was pointed at. When the packed structured planes were a
single object-level pair shared by all slots, that write landed in the buffers
resource A was still reading, because no fence in the system covered them. One
dispatch then saw below/lineMeta from one emulated frame and above/control from
the next, which is what drew the background and 3D layer in front of the UI on
alternating frames.

DX12 can share one composition input buffer because it serializes every
composition with Commands.WaitIdle(). Vulkan here does not, so ownership has to
be per-frame instead. This audit pins that: the planes live in FrameResource, no
object-level copy exists, and the write/build/dispatch/destroy paths all go
through the resource rather than through anything shared.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

HEADER = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanOutput.h"
SOURCE = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanOutput.cpp"

# Resources the compositor writes or binds per frame. Each must be reachable
# only through a FrameResource, so that resource's submitFence is the one and
# only thing that has to be waited on before reuse.
PER_FRAME_MEMBERS = (
    "topPackedBuffer",
    "topPackedMemory",
    "topPackedMapped",
    "bottomPackedBuffer",
    "bottomPackedMemory",
    "bottomPackedMapped",
    "packedBufferSize",
)


def extract_struct(text: str, name: str) -> str:
    """Return the body of `struct <name> { ... };` at brace depth."""
    start = text.index(f"struct {name}")
    open_brace = text.index("{", start)
    depth = 0
    for index in range(open_brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace + 1:index]
    raise ValueError(f"unterminated struct {name}")


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

    header = HEADER.read_text(encoding="utf-8")
    source = SOURCE.read_text(encoding="utf-8")
    header_code = strip_comments(header)
    source_code = strip_comments(source)

    # 1. The planes are declared inside FrameResource.
    frame_resource = strip_comments(extract_struct(header, "FrameResource"))
    for member in PER_FRAME_MEMBERS:
        if not re.search(rf"\b{member}\b", frame_resource):
            failures.append(
                f"{HEADER.name}: FrameResource no longer declares {member}; the "
                "packed planes must be owned by the frame whose fence protects them")

    # 2. And nowhere else in the class. Anything declared at object level is
    #    shared by every slot again, which is the defect.
    class_body = header_code[header_code.index("class MelonPrimeVulkanOutput"):]
    outside = class_body.replace(frame_resource, "")
    # VulkanCompositionInputs legitimately carries resolved handles, not storage.
    composition_inputs = strip_comments(extract_struct(header, "VulkanCompositionInputs"))
    outside = outside.replace(composition_inputs, "")
    for member in PER_FRAME_MEMBERS:
        if re.search(rf"^\s*(VkBuffer|VkDeviceMemory|void\*|VkDeviceSize)\s+{member}\b",
                     outside, re.MULTILINE):
            failures.append(
                f"{HEADER.name}: MelonPrimeVulkanOutput declares an object-level "
                f"{member}; a plane shared across frame slots is covered by no "
                "single fence")

    # 3. lastComposedFrame was the stand-in for "who read the shared buffer
    #    last". It never gated anything, and per-frame ownership makes the
    #    question meaningless, so it must not come back.
    for name, text in ((HEADER.name, header_code), (SOURCE.name, source_code)):
        if "lastComposedFrame" in text:
            failures.append(
                f"{name}: lastComposedFrame is back; per-frame ownership makes a "
                "'last reader of the shared buffer' pointer both unnecessary and "
                "misleading")

    # 4. The CPU write goes through the acquired resource's own mapping.
    update = strip_comments(extract_function(
        source, "bool MelonPrimeVulkanOutput::updateCompositorPackedBuffers("))
    for member in ("topPackedMapped", "bottomPackedMapped"):
        if f"resource.{member}" not in update:
            failures.append(
                f"{SOURCE.name}: updateCompositorPackedBuffers does not write "
                f"through resource.{member}")
    if re.search(r"(?<!resource\.)\b(top|bottom)PackedMapped\b", update):
        failures.append(
            f"{SOURCE.name}: updateCompositorPackedBuffers writes a mapping that "
            "is not the acquired resource's own")

    # 5. The published inputs name that same resource's buffers.
    build = strip_comments(extract_function(
        source, "bool MelonPrimeVulkanOutput::buildCompositionInputs("))
    for member in ("topPackedBuffer", "bottomPackedBuffer", "packedBufferSize"):
        if f"outInputs.{member} = resource.{member};" not in build:
            failures.append(
                f"{SOURCE.name}: buildCompositionInputs does not publish "
                f"resource.{member}")

    # 6. The dispatch refuses inputs naming another frame's buffers.
    dispatch = strip_comments(extract_function(
        source, "bool MelonPrimeVulkanOutput::dispatchCompositor("))
    for member in ("topPackedBuffer", "bottomPackedBuffer", "packedBufferSize"):
        if f"inputs.{member} != resource.{member}" not in dispatch:
            failures.append(
                f"{SOURCE.name}: dispatchCompositor does not verify that "
                f"inputs.{member} is this resource's own buffer")
    if "packedBufferIdentityMismatch++" not in dispatch:
        failures.append(
            f"{SOURCE.name}: dispatchCompositor no longer counts composition "
            "inputs that name another frame's packed buffers")

    # 6b. The descriptor the dispatch binds is this frame's own, and it is
    #     filled from the same resolved inputs the identity check above covers.
    #     A descriptor is cached across frames, so one pointing at another
    #     frame's buffer would persist rather than being a single bad dispatch.
    for binding, info in ((2, "topPackedBufferInfo"), (3, "bottomPackedBufferInfo")):
        if f"makeBufferDescriptorWrite(resource.descriptorSet, {binding}, &{info})" not in dispatch:
            failures.append(
                f"{SOURCE.name}: dispatchCompositor no longer writes binding "
                f"{binding} of this frame's own descriptor set")
    for info, member in (("topPackedBufferInfo", "topPackedBuffer"),
                         ("bottomPackedBufferInfo", "bottomPackedBuffer")):
        if f"{info}.buffer = inputs.{member};" not in dispatch:
            failures.append(
                f"{SOURCE.name}: {info} is not filled from inputs.{member}")

    # 6c. Host writes are made visible to the compute read, on this frame's
    #     buffers. Visibility and ownership are separate problems and both are
    #     required; a barrier alone never made the shared-buffer version safe.
    if ("packedBarriers[i].buffer = i == 0 ? inputs.topPackedBuffer : inputs.bottomPackedBuffer;"
            not in dispatch):
        failures.append(
            f"{SOURCE.name}: the host-to-compute barrier no longer targets this "
            "frame's own packed buffers")
    if ("packedBarriers[i].srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;" not in dispatch
            or "packedBarriers[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;" not in dispatch):
        failures.append(
            f"{SOURCE.name}: the packed-plane barrier no longer makes HOST_WRITE "
            "visible to SHADER_READ")

    # 6d. Submission hands the planes to the GPU, so the next CPU write has to
    #     go back through acquireFrameForCpuWrite() and its fence wait.
    submit = strip_comments(extract_function(
        source, "bool MelonPrimeVulkanOutput::submitFrameCommand("))
    if ("resource.submissionState = SubmissionState::Submitted;" not in submit
            or "resource.cpuWriteOwnership = false;" not in submit):
        failures.append(
            f"{SOURCE.name}: submitFrameCommand does not release CPU write "
            "ownership; the planes could be rewritten while the dispatch reads them")
    if ("resource.submissionState != SubmissionState::Idle" not in update
            or "packedWriteWhileSubmitted++" not in update):
        failures.append(
            f"{SOURCE.name}: updateCompositorPackedBuffers no longer refuses to "
            "write a frame whose dispatch is still in flight")

    # 6e. Resource lifetime must not depend on assertions, which compile out of
    #     release builds. Asserts may check the invariants, never maintain them.
    for match in re.finditer(r"^\s*assert\((.*)\);\s*$", source_code, re.M):
        expression = match.group(1)
        if re.search(r"vk[A-Z]\w*\(|\+\+|--|[^=!<>]=[^=]", expression):
            failures.append(
                f"{SOURCE.name}: assert({expression}) has a side effect; release "
                "builds would then have a different resource lifetime")

    # 7. Teardown waits on this frame's fence and frees only this frame's planes.
    destroy = strip_comments(extract_function(
        source, "void MelonPrimeVulkanOutput::destroyFrameResource("))
    if "vkWaitForFences(device, 1, &resource.submitFence" not in destroy:
        failures.append(
            f"{SOURCE.name}: destroyFrameResource no longer waits on the frame's "
            "own fence before freeing its resources")
    for call, member in (
        ("vkUnmapMemory(device, resource.topPackedMemory)", "top mapping"),
        ("vkFreeMemory(device, resource.topPackedMemory, nullptr)", "top memory"),
        ("vkDestroyBuffer(device, resource.topPackedBuffer, nullptr)", "top buffer"),
        ("vkUnmapMemory(device, resource.bottomPackedMemory)", "bottom mapping"),
        ("vkFreeMemory(device, resource.bottomPackedMemory, nullptr)", "bottom memory"),
        ("vkDestroyBuffer(device, resource.bottomPackedBuffer, nullptr)", "bottom buffer"),
    ):
        if call not in destroy:
            failures.append(
                f"{SOURCE.name}: destroyFrameResource leaks the frame's {member}")

    # 8. Creation is per-frame, and the retired global helpers stay retired.
    create = strip_comments(extract_function(
        source, "bool MelonPrimeVulkanOutput::createFrameResource("))
    if "resource.packedBufferSize = kPackedBufferSize;" not in create:
        failures.append(
            f"{SOURCE.name}: createFrameResource does not size the frame's own "
            "packed planes")
    for retired in ("createPackedBuffers", "destroyPackedBuffers"):
        if retired in source_code or retired in header_code:
            failures.append(
                f"{SOURCE.name}: {retired}() is back; packed planes are created "
                "and destroyed with the frame that owns them")

    if failures:
        print("Vulkan frame resource ownership audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        "Vulkan frame resource ownership OK: the packed structured planes belong "
        "to a single FrameResource, so that frame's submitFence is the only thing "
        "protecting them")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
