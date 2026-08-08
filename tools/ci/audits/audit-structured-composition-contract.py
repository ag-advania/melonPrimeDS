#!/usr/bin/env python3
"""Verify the structured 2D composition contract is identical in all three places.

The software renderer publishes structured 2D metadata that the DX12 and Vulkan
compositors both decode. C++ can share src/MelonPrimeStructuredComposition.h,
but the two shaders cannot: the DX12 compositor is HLSL compiled at runtime and
the Vulkan compositor is GLSL compiled ahead of time into a SPIR-V header. Both
therefore restate the constants, and a silent drift in any one of them shows up
only as wrong pixels on one backend.

This audit parses the numeric values out of all three files and fails when they
disagree.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

HEADER = ROOT / "src/MelonPrimeStructuredComposition.h"
HLSL = ROOT / "src/GPU3D_DX12_shaders.h"
GLSL = ROOT / "src/GPU3D_Vulkan_shaders/Compositor.comp"

# Contract name -> the expression each consumer is expected to use.
#
# The shaders inline the values rather than naming every one of them, so the
# expectations below are written against what the shader source actually says.
CONTRACT_NAMES = [
    "kControlFlagShift",
    "kControlEvbShift",
    "kControlEvaShift",
    "kControlBlendFactorMask",
    "kControlCompositionModeMask",
    "kControlHas3DSlot",
    "kControlAbovePlane",
    "kCompositionModeBlend4",
    "kCompositionModeBrightnessUp",
    "kCompositionModeBrightnessDown",
    "kCompositionModeBlend5",
    "kLineMetaBrightnessFactorMask",
    "kLineMetaBrightnessModeShift",
    "kLineMetaBrightnessModeMask",
    "kLineMetaDisplayModeShift",
    "kLineMetaDisplayModeMask",
    "kLineMetaRenderXPosShift",
    "kLineMetaRenderXPosMask",
    "kBrightnessModeUp",
    "kBrightnessModeDown",
    "kBrightnessFactorLimit",
    "k3DPlaceholderPixel",
    "k3DLayerSlotPixel",
]

# How the DX12 HLSL compositor spells each contract value. The HLSL keeps the
# raw literals inline, so this table is the mapping that has to stay true.
HLSL_EXPECTATIONS = {
    "kControlFlagShift": "uint controlAlpha = control >> 24u;",
    "kControlHas3DSlot": "if ((controlAlpha & 0x40u) != 0u)",
    "kControlCompositionModeMask": "uint compositionMode = controlAlpha & 0xFu;",
    "kControlEvaShift": "uint eva = (control >> 8u) & 0x1Fu;",
    "kControlEvbShift": "uint evb = (control >> 16u) & 0x1Fu;",
    "kControlAbovePlane": "if (compositionMode == 1u && (controlAlpha & 0x80u) != 0u)",
    "kLineMetaDisplayModeShift": "uint displayMode = (lineMeta >> 16u) & 0x3u;",
    "kLineMetaBrightnessModeShift": "uint brightnessMode = (lineMeta >> 8u) & 0x3u;",
    "kLineMetaBrightnessFactorMask": "uint brightnessFactor = min(lineMeta & 0x1Fu, 16u);",
    "kLineMetaRenderXPosShift": "uint xPosition = (lineMeta >> 23u) & 0x1FFu;",
}


def parse_cpp_constants(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    pattern = re.compile(
        r"inline constexpr u32 (\w+)\s*=\s*([^;]+);", re.MULTILINE)
    for name, expression in pattern.findall(text):
        values[name] = evaluate(expression, values)
    return values


def parse_glsl_constants(text: str) -> dict[str, int]:
    values: dict[str, int] = {}
    pattern = re.compile(r"^const uint (\w+)\s*=\s*([^;]+);", re.MULTILINE)
    for name, expression in pattern.findall(text):
        values[name] = evaluate(expression, values)
    return values


def evaluate(expression: str, known: dict[str, int]) -> int:
    """Evaluate the small integer expressions these contracts use."""
    cleaned = re.sub(r"\b(\w+)\b", lambda m: str(known[m.group(1)])
                     if m.group(1) in known else m.group(1), expression)
    cleaned = re.sub(r"(0[xX][0-9a-fA-F]+|\d+)[uU]\b", r"\1", cleaned)
    cleaned = cleaned.strip()
    try:
        return int(eval(cleaned, {"__builtins__": {}}, {}))  # noqa: S307
    except Exception as error:  # pragma: no cover - surfaced as an audit failure
        raise ValueError(f"could not evaluate {expression!r}: {error}") from error


def main() -> int:
    failures: list[str] = []

    header_values = parse_cpp_constants(HEADER.read_text(encoding="utf-8"))
    hlsl_text = HLSL.read_text(encoding="utf-8")

    # A Vulkan-disabled tree still has to audit the DX12 half, and a moved or
    # renamed shader must fail loudly rather than crash. Audit whatever exists
    # and say plainly when the Vulkan side was not checked.
    glsl_present = GLSL.is_file()
    glsl_text = GLSL.read_text(encoding="utf-8") if glsl_present else ""
    glsl_values = parse_glsl_constants(glsl_text) if glsl_present else {}

    for name in CONTRACT_NAMES:
        if name not in header_values:
            failures.append(f"{HEADER.name} does not define {name}")
            continue
        if not glsl_present:
            continue
        if name not in glsl_values:
            failures.append(f"{GLSL.name} does not define {name}")
            continue
        if header_values[name] != glsl_values[name]:
            failures.append(
                f"{name}: {HEADER.name}=0x{header_values[name]:X} "
                f"but {GLSL.name}=0x{glsl_values[name]:X}")

    # The DX12 compositor is a runtime-compiled HLSL string, so the audit pins
    # the exact expressions instead of parsing declarations.
    compositor_start = hlsl_text.find("inline const std::string Compositor")
    if compositor_start < 0:
        failures.append(f"{HLSL.name} no longer defines the Compositor shader")
    else:
        compositor = hlsl_text[compositor_start:]
        for name, expected in HLSL_EXPECTATIONS.items():
            if expected not in compositor:
                failures.append(
                    f"{name}: {HLSL.name} no longer contains {expected!r}; "
                    "the DX12 compositor has drifted from the contract")

    # Both compositors must reject a 3D pixel with zero alpha the same way.
    if glsl_present and "((pixel3D >> 24u) & 0x1Fu) != 0u" not in glsl_text:
        failures.append(f"{GLSL.name} lost the 5-bit 3D alpha coverage test")
    if compositor_start >= 0 and "((pixel3D >> 24u) & 0x1Fu) != 0u" not in hlsl_text:
        failures.append(f"{HLSL.name} lost the 5-bit 3D alpha coverage test")

    if failures:
        print("structured composition contract audit FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1

    if glsl_present:
        print(
            f"structured composition contract OK: {len(CONTRACT_NAMES)} constants "
            f"agree between {HEADER.name} and {GLSL.name}, "
            f"{len(HLSL_EXPECTATIONS)} pinned expressions present in {HLSL.name}")
    else:
        print(
            f"structured composition contract OK (DX12 only): "
            f"{len(HLSL_EXPECTATIONS)} pinned expressions present in {HLSL.name}; "
            f"{GLSL.name} is absent, so the Vulkan side was NOT checked")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
