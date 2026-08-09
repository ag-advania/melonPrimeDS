#!/usr/bin/env python3
"""Regenerate the SPIR-V blobs for the MelonPrime Vulkan presenter.

The presenter's two graphics stages (Present.vert / Present.frag) are compiled
offline and the resulting SPIR-V words are committed, exactly like the compute
rasterizer's shaders: melonPrimeDS must build with no glslang dependency, and a
shader error must be a build-time failure of this script rather than an
invisible runtime one.

Usage:
    python tools/vulkan/compile-present-shaders.py [--glslang <path>]

Writes src/frontend/qt_sdl/MelonPrimeVulkanPresentShaders/
       MelonPrimeVulkanPresentShaderBlobs.h
"""

from __future__ import annotations

import argparse
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
SHADER_DIR = REPO_ROOT / "src" / "frontend" / "qt_sdl" / "MelonPrimeVulkanPresentShaders"
OUTPUT = SHADER_DIR / "MelonPrimeVulkanPresentShaderBlobs.h"

STAGES = (
    ("Present.vert", "vert", "PresentVertexSpirv"),
    ("Present.frag", "frag", "PresentFragmentSpirv"),
)


def find_glslang(explicit: str | None) -> str:
    if explicit:
        return explicit
    for name in ("glslangValidator", "glslang"):
        found = shutil.which(name)
        if found:
            return found
    raise SystemExit(
        "glslangValidator not found. Install the Vulkan SDK or pass --glslang <path>."
    )


def compile_stage(glslang: str, source: pathlib.Path, stage: str) -> list[int]:
    with tempfile.TemporaryDirectory() as tmp:
        out = pathlib.Path(tmp) / (source.stem + ".spv")
        subprocess.run(
            [glslang, "-V", "--target-env", "vulkan1.1", "-S", stage,
             "-o", str(out), str(source)],
            check=True,
        )
        blob = out.read_bytes()

    if len(blob) % 4 != 0:
        raise SystemExit(f"{source.name}: SPIR-V size {len(blob)} is not a multiple of 4")
    words = list(struct.unpack(f"<{len(blob) // 4}I", blob))
    if not words or words[0] != 0x07230203:
        raise SystemExit(f"{source.name}: missing SPIR-V magic number")
    return words


def format_words(words: list[int]) -> str:
    lines = []
    for start in range(0, len(words), 6):
        chunk = words[start:start + 6]
        lines.append("    " + " ".join(f"0x{word:08x}u," for word in chunk))
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--glslang", default=None)
    args = parser.parse_args()

    glslang = find_glslang(args.glslang)

    blocks = []
    for filename, stage, symbol in STAGES:
        words = compile_stage(glslang, SHADER_DIR / filename, stage)
        blocks.append(
            f"// {filename}: {len(words)} words\n"
            f"inline constexpr std::uint32_t {symbol}[] = {{\n"
            f"{format_words(words)}\n"
            f"}};\n"
        )

    header = (
        "// GENERATED FILE -- do not edit.\n"
        "//\n"
        "// Regenerate with: python tools/vulkan/compile-present-shaders.py\n"
        "// Sources: src/frontend/qt_sdl/MelonPrimeVulkanPresentShaders/Present.{vert,frag}\n"
        "//\n"
        "// SPIR-V is committed so the build has no glslang dependency, matching the\n"
        "// compute rasterizer's generated blobs under src/GPU3D_Vulkan_shaders/generated.\n"
        "\n"
        "#ifndef MELONPRIME_VULKAN_PRESENT_SHADER_BLOBS_H\n"
        "#define MELONPRIME_VULKAN_PRESENT_SHADER_BLOBS_H\n"
        "\n"
        "#include <cstdint>\n"
        "\n"
        "namespace MelonPrime::VulkanPresentShaders\n"
        "{\n"
        "\n"
        + "\n".join(blocks)
        + "\n} // namespace MelonPrime::VulkanPresentShaders\n"
        "\n"
        "#endif // MELONPRIME_VULKAN_PRESENT_SHADER_BLOBS_H\n"
    )

    OUTPUT.write_text(header, encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
