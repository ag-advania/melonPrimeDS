#!/usr/bin/env python3
"""Ratchet the Custom HUD radar color-key contract across presentation paths."""

from __future__ import annotations

from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[3]
MASK = 0xF8F8F8
EXPECTED_PALETTE_SIZE = 15


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(source: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in source:
        failures.append(f"{label}: missing {needle!r}")


def main() -> int:
    failures: list[str] = []

    constants = read("src/frontend/qt_sdl/MelonPrimeConstants.h")
    palette_match = re.search(
        r"kRadarPaletteColors\[\]\s*=\s*\{(?P<body>.*?)\};",
        constants,
        re.DOTALL,
    )
    if palette_match is None:
        failures.append("palette: kRadarPaletteColors definition was not found")
        palette: tuple[int, ...] = ()
    else:
        palette = tuple(
            int(value, 16)
            for value in re.findall(r"0x([0-9A-Fa-f]{6})", palette_match.group("body"))
        )

    if len(palette) != EXPECTED_PALETTE_SIZE:
        failures.append(
            f"palette: expected {EXPECTED_PALETTE_SIZE} colors, got {len(palette)}"
        )
    if len(set(palette)) != len(palette):
        failures.append("palette: duplicate color")
    for color in palette:
        if color & ~MASK:
            failures.append(f"palette: 0x{color:06X} is outside the 5-bit color-key space")

    require(
        constants,
        "kRadarPaletteQuantizationMask = 0x00F8F8F8u",
        "shared CPU mask",
        failures,
    )

    cpu = read("src/frontend/qt_sdl/MelonPrimeHudRadarRuntime.inc")
    require(
        cpu,
        "rgb & kRadarPaletteQuantizationMask",
        "CPU radar color key",
        failures,
    )

    gl = read("src/frontend/qt_sdl/main_shaders.h")
    require(
        gl,
        "uvec3(round(clamp(pixel.rgb, vec3(0.0), vec3(1.0)) * 255.0))",
        "OpenGL normalized integer conversion",
        failures,
    )
    require(gl, "color &= uvec3(0xF8u)", "OpenGL 5-bit quantization", failures)
    require(
        gl,
        "all(equal(color, uvec3(uPalette[i])))",
        "OpenGL integer palette comparison",
        failures,
    )
    require(
        gl,
        "radarColorKey(texelFetch",
        "OpenGL color-key-before-filter ordering",
        failures,
    )
    require(
        gl,
        "keyedPixel.rgb / keyedPixel.a",
        "OpenGL straight-alpha reconstruction",
        failures,
    )

    vulkan = read("src/frontend/qt_sdl/MelonPrimeVulkanPresentShaders/Present.frag")
    dx12 = read("src/frontend/qt_sdl/MelonPrimeDX12SurfacePresenter.cpp")
    metal = read("src/frontend/qt_sdl/MelonPrimeScreenMetal.mm")
    require(vulkan, "& uvec3(0xf8u)", "Vulkan 5-bit quantization", failures)
    require(
        dx12,
        "& uint3(0xf8u, 0xf8u, 0xf8u)",
        "DX12 5-bit quantization",
        failures,
    )
    require(metal, "color &= uint3(0xF8u)", "Metal 5-bit quantization", failures)

    # The software renderer's 6-to-8-bit expansion can set any of bits [2:0].
    # Every such representation must retain the corresponding palette color.
    palette_set = set(palette)
    # Internal resolution and display filtering select the input texel, but do
    # not alter this per-sample color-key operation. Exercise the same contract
    # for every requested validation mode without pretending that Software has
    # a hardware-renderer internal scale of its own.
    retained_variants = 0
    rejected_colors = 0
    modes = tuple(
        (scale, filtered)
        for scale in (1, 2, 4, 8)
        for filtered in (False, True)
    )
    for scale, filtered in modes:
        mode = f"{scale}x/filter={'on' if filtered else 'off'}"
        for color in palette:
            for red_low in range(8):
                for green_low in range(8):
                    for blue_low in range(8):
                        expanded = color | (red_low << 16) | (green_low << 8) | blue_low
                        if (expanded & MASK) not in palette_set:
                            failures.append(
                                f"{mode}: palette variant 0x{expanded:06X} was rejected"
                            )
                        retained_variants += 1

        # Conversely, every other point in the quantized RGB cube must be rejected.
        for red in range(0, 256, 8):
            for green in range(0, 256, 8):
                for blue in range(0, 256, 8):
                    color = (red << 16) | (green << 8) | blue
                    if color not in palette_set:
                        if (color & MASK) in palette_set:
                            failures.append(
                                f"{mode}: non-palette color 0x{color:06X} was retained"
                            )
                        rejected_colors += 1

    # Regression vector for the native-resolution shrink bug: filtering a
    # palette texel with a transparent/non-palette neighbour before the key
    # changes its RGB and rejects the edge. Keying first preserves fractional
    # coverage, which manual bilinear reconstruction must carry to the output.
    edge_color = palette[0] if palette else 0xC0F868
    edge_coverage = 0.75
    edge_channels = (
        int(((edge_color >> shift) & 0xFF) * edge_coverage + 0.5)
        for shift in (16, 8, 0)
    )
    filtered_first = 0
    for channel in edge_channels:
        filtered_first = (filtered_first << 8) | channel
    if (filtered_first & MASK) in palette_set:
        failures.append("filter-order vector: filtered edge unexpectedly passed the key")
    if not (0.0 < edge_coverage < 1.0):
        failures.append("filter-order vector: keyed-first edge lost fractional coverage")

    if failures:
        print("Radar color-key contract audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"- {failure}", file=sys.stderr)
        return 1

    print(
        "Radar color-key contract audit passed: "
        f"{len(palette)} palette colors, {retained_variants} expanded variants, "
        f"{rejected_colors} non-palette colors, {len(modes)} scale/filter modes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
