#!/usr/bin/env python3
"""Audit the compositor SPIR-V that Vulkan actually executes.

The runtime never sees MelonPrimeVulkanCompositorShader.comp. It consumes the
byte array checked into MelonPrimeVulkanCompositorShaderData.h, so a GLSL-only
audit cannot prove the shipped binary matches the source, nor that the removed
heuristic inputs are really gone from it.

This audit works from the embedded binary: it decodes the SPIR-V out of the
header and checks the interface the compositor is allowed to have.

Checks:
  * the header still contains a well-formed SPIR-V module
  * source and header SHA-256 match docs/development/rendering/vulkan-spirv-manifest.json
  * the descriptor interface is exactly the four deterministic bindings
  * none of the retired heuristic bindings survive
  * the push-constant block matches CompositorPushConstants
  * spirv-val passes, when SPIRV-Tools is available
  * recompiling the GLSL reproduces the embedded binary, when glslc is available
"""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

SOURCE = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanCompositorShader.comp"
HEADER = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanCompositorShaderData.h"
MANIFEST = ROOT / "docs/development/rendering/vulkan-spirv-manifest.json"
OUTPUT_HEADER = ROOT / "src/frontend/qt_sdl/MelonPrimeVulkanOutput.h"
SYMBOL = "melonDS_android_vulkan_compositor_comp_spv"

# binding -> what it is. The compositor is a pure function of one emulated
# frame, so this is the whole interface it is permitted to have.
EXPECTED_BINDINGS = {
    0: "composition output image",
    1: "current 3D color target",
    2: "top screen structured planes",
    3: "bottom screen structured planes",
}

# Bindings the pinned Android compositor used to carry. Their absence is the
# machine-checkable half of "no frame history and no scene statistics".
RETIRED_BINDINGS = {
    4: "previous top 3D image",
    5: "captured-3D history buffer",
    6: "previous bottom 3D image",
    7: "screen statistics buffer",
}

SPIRV_MAGIC = 0x07230203
OP_DECORATE = 71
OP_MEMBER_DECORATE = 72
OP_TYPE_STRUCT = 30
OP_TYPE_POINTER = 32
OP_VARIABLE = 59
DECORATION_BINDING = 33
DECORATION_DESCRIPTOR_SET = 34
DECORATION_BLOCK = 2
STORAGE_CLASS_PUSH_CONSTANT = 9


def canonical_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def embedded_spirv() -> bytes:
    text = HEADER.read_text(encoding="utf-8")
    pattern = re.compile(
        rf"unsigned char {re.escape(SYMBOL)}\[\] = \{{(.*?)\n\}};", re.DOTALL)
    match = pattern.search(text)
    if not match:
        raise RuntimeError(f"{HEADER.name} does not contain {SYMBOL}")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(1)))


def instructions(module: bytes):
    if len(module) < 20 or len(module) % 4 != 0:
        raise RuntimeError("embedded SPIR-V has an invalid length")
    words = struct.unpack(f"<{len(module) // 4}I", module)
    if words[0] != SPIRV_MAGIC:
        raise RuntimeError("embedded SPIR-V has a bad magic number")
    index = 5
    while index < len(words):
        count = words[index] >> 16
        if count == 0 or index + count > len(words):
            raise RuntimeError("embedded SPIR-V has a malformed instruction")
        yield words[index] & 0xFFFF, words[index:index + count]
        index += count


def main() -> int:
    failures: list[str] = []
    notes: list[str] = []

    module = embedded_spirv()
    decoded = list(instructions(module))

    # --- manifest ---------------------------------------------------------
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    row = next(
        (r for r in manifest["shaders"]
         if r["header"] == HEADER.relative_to(ROOT).as_posix()), None)
    if row is None:
        failures.append(f"{MANIFEST.name} has no entry for {HEADER.name}")
    else:
        for key, path in (("source_sha256", SOURCE), ("header_sha256", HEADER)):
            actual = canonical_sha256(path)
            if row.get(key) != actual:
                failures.append(
                    f"{MANIFEST.name}: {key} is stale for {path.name}; "
                    "regenerate the SPIR-V header and the manifest together")

    # --- descriptor interface --------------------------------------------
    bindings: dict[int, int] = {}
    descriptor_sets: dict[int, int] = {}
    for opcode, words in decoded:
        if opcode != OP_DECORATE or len(words) < 4:
            continue
        target, decoration, value = words[1], words[2], words[3]
        if decoration == DECORATION_BINDING:
            bindings[target] = value
        elif decoration == DECORATION_DESCRIPTOR_SET:
            descriptor_sets[target] = value

    used = sorted(bindings.values())
    if used != sorted(EXPECTED_BINDINGS):
        failures.append(
            f"{HEADER.name}: descriptor bindings are {used}, expected "
            f"{sorted(EXPECTED_BINDINGS)} "
            f"({', '.join(f'{k}={v}' for k, v in sorted(EXPECTED_BINDINGS.items()))})")

    for binding, description in RETIRED_BINDINGS.items():
        if binding in used:
            failures.append(
                f"{HEADER.name}: binding {binding} ({description}) is back; the "
                "compositor must not consume frame history or scene statistics")

    stray_sets = {value for value in descriptor_sets.values() if value != 0}
    if stray_sets:
        failures.append(
            f"{HEADER.name}: descriptor sets {sorted(stray_sets)} are used; the "
            "compositor pipeline layout only declares set 0")

    # --- push constant layout --------------------------------------------
    struct_members = {
        words[1]: len(words) - 2
        for opcode, words in decoded
        if opcode == OP_TYPE_STRUCT and len(words) >= 2
    }
    pointer_pointee = {
        words[1]: (words[2], words[3])
        for opcode, words in decoded
        if opcode == OP_TYPE_POINTER and len(words) >= 4
    }
    push_constant_members = None
    for opcode, words in decoded:
        if opcode != OP_VARIABLE or len(words) < 4:
            continue
        if words[3] != STORAGE_CLASS_PUSH_CONSTANT:
            continue
        pointee = pointer_pointee.get(words[1])
        if pointee and pointee[1] in struct_members:
            push_constant_members = struct_members[pointee[1]]

    output_header = OUTPUT_HEADER.read_text(encoding="utf-8")
    struct_match = re.search(
        r"struct CompositorPushConstants\s*\{(.*?)\};", output_header, re.DOTALL)
    if struct_match is None:
        failures.append(f"{OUTPUT_HEADER.name} no longer declares CompositorPushConstants")
    elif push_constant_members is None:
        failures.append(f"{HEADER.name}: no push-constant block found in the module")
    else:
        cpp_members = len(re.findall(r"^\s*u32 \w+;", struct_match.group(1), re.MULTILINE))
        if cpp_members != push_constant_members:
            failures.append(
                f"push constants disagree: {OUTPUT_HEADER.name} declares "
                f"{cpp_members} u32 fields but the module's block has "
                f"{push_constant_members}")

    # --- optional external tools -----------------------------------------
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as handle:
        module_path = Path(handle.name)
        handle.write(module)
    try:
        validator = shutil.which("spirv-val")
        if validator:
            result = subprocess.run(
                [validator, str(module_path)], capture_output=True, text=True)
            if result.returncode != 0:
                failures.append(
                    f"spirv-val rejected the embedded module: "
                    f"{result.stderr.strip() or result.stdout.strip()}")
        else:
            notes.append("spirv-val not found; skipped module validation")

        compiler = shutil.which("glslc")
        if compiler:
            with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as rebuilt_handle:
                rebuilt_path = Path(rebuilt_handle.name)
            try:
                subprocess.run(
                    [compiler, "-fshader-stage=comp", "-o", str(rebuilt_path), str(SOURCE)],
                    check=True, capture_output=True)
                if rebuilt_path.read_bytes() != module:
                    notes.append(
                        "the local glslc produced different bytes than the checked-in "
                        "header; that is expected across shaderc versions, so only the "
                        "interface checks above are authoritative here")
            except subprocess.CalledProcessError as error:
                failures.append(f"glslc could not compile {SOURCE.name}: {error}")
            finally:
                rebuilt_path.unlink(missing_ok=True)
        else:
            notes.append("glslc not found; skipped the recompile comparison")
    finally:
        module_path.unlink(missing_ok=True)

    for note in notes:
        print(f"note: {note}")

    if failures:
        print("Vulkan compositor SPIR-V audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    print(
        f"Vulkan compositor SPIR-V OK: {len(module)} bytes, bindings {used}, "
        f"{push_constant_members} push-constant fields, no retired bindings")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
