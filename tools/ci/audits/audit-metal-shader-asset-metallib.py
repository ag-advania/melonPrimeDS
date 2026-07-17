#!/usr/bin/env python3
"""PR-14: MSL asset / metallib.

Production Metal shaders load from an ahead-of-time-compiled
melonPrimeDS.metallib bundled in the app's Contents/Resources
(MelonPrimeMetalDefaultLibrary(), MelonPrimeMetalLibrary.h/.mm) instead of
compiling embedded MSL source with -newLibraryWithSource: at runtime. Any
remaining -newLibraryWithSource: call for a migrated shader must live inside
a `#if !defined(NDEBUG) || defined(MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK)`
guard (debug/opt-in fallback only -- a real release build must not reach it
for these shaders). See §20 of
docs/plans/melonPrimeDS_develop_metal_完全Metal化_実行指示書_17b46586_2026-07-17.md.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]

BUNDLED_METALLIB_MARKER = "MELONPRIME_METAL_BUNDLED_METALLIB_V1"
FALLBACK_GUARD = "#if !defined(NDEBUG) || defined(MELONPRIME_METAL_ALLOW_SOURCE_FALLBACK)"

# Physical .metal assets PR-14 extracted from embedded MSL/NSString literals.
# Each entry point must appear in the extracted file so drift between the
# call site's -newFunctionWithName: string and the shader source is caught.
SHADER_ASSETS = {
    "DisplayCapture.metal": ["mp_metal_display_capture"],
    "GPU2DFullGpu.metal": [
        "mp2d_sprite_vs", "mp2d_sprite_fs", "mp2d_composite_vs", "mp2d_composite_fs",
    ],
    "FullGpuOutput.metal": ["mp_full_gpu_output_vs", "mp_full_gpu_output_fs"],
    "VisibleOutput.metal": ["mp_visible_output_vs", "mp_visible_output_fs"],
    "ComputeFinalPass.metal": ["mp_compute_final_pass"],
    "ComputeTextured.metal": ["mp_compute_rasterise_texture_variants"],
    "ComputeDepthBlend.metal": ["mp_compute_depth_blend_complete"],
    "Presenter.metal": ["mp_screen_vs", "mp_screen_fs", "mp_ui_vs", "mp_ui_fs", "mp_radar_fs"],
}

# (source file, embedded shader-source constant names that must only reach
# -newLibraryWithSource: inside the debug-fallback guard).
MIGRATED_SITES = [
    ("src/GPU_MetalCaptureMethods.inc", ["kMetalDisplayCaptureShaderSource"]),
    ("src/GPU_MetalFullGpuMethods.inc", ["kMetalFullGpuOutputShaderSource"]),
    ("src/GPU_Metal.mm", ["kMetalVisibleOutputShaderSource"]),
    ("src/GPU2D_MetalFullGpuMethods.inc", ["kMetal2DFullGpuShaderSource"]),
    (
        "src/GPU3D_MetalCompute.mm",
        [
            "kMetalComputeTexturedSource",
            "kMetalComputeCompleteDepthBlendSource",
            "kMetalComputeFinalPassSource",
        ],
    ),
    (
        "src/frontend/qt_sdl/MelonPrimeScreenMetal.mm",
        ["kScreenShaderSource", "kUiShaderSource", "kRadarShaderSource"],
    ),
]


def guarded_line_mask(lines: list[str]) -> list[bool]:
    """True for lines inside a FALLBACK_GUARD `#if ... #endif` block. Assumes
    (true for every block PR-14 introduced) that the guarded block itself
    contains no nested #if/#endif, so the block simply ends at the next
    #endif line after the guard opens."""
    mask = [False] * len(lines)
    in_guard = False
    for i, line in enumerate(lines):
        stripped = line.strip()
        if not in_guard and stripped == FALLBACK_GUARD:
            in_guard = True
            mask[i] = True
            continue
        if in_guard:
            mask[i] = True
            if stripped.startswith("#endif"):
                in_guard = False
    return mask


def main() -> int:
    issues: list[str] = []

    shader_dir = ROOT / "src/shaders/metal"
    if not shader_dir.is_dir():
        issues.append(f"{shader_dir} does not exist")
    else:
        for filename, entry_points in SHADER_ASSETS.items():
            path = shader_dir / filename
            if not path.is_file():
                issues.append(f"missing shader asset src/shaders/metal/{filename}")
                continue
            text = path.read_text(encoding="utf-8")
            if not text.strip():
                issues.append(f"src/shaders/metal/{filename} is empty")
            for fn in entry_points:
                if not re.search(rf"\b{re.escape(fn)}\b", text):
                    issues.append(
                        f"src/shaders/metal/{filename} missing expected entry point {fn!r}"
                    )

    library_header = ROOT / "src/MelonPrimeMetalLibrary.h"
    library_impl = ROOT / "src/MelonPrimeMetalLibrary.mm"
    if not library_header.is_file():
        issues.append("missing src/MelonPrimeMetalLibrary.h")
    else:
        header_text = library_header.read_text(encoding="utf-8")
        if BUNDLED_METALLIB_MARKER not in header_text:
            issues.append(f"MelonPrimeMetalLibrary.h missing {BUNDLED_METALLIB_MARKER} marker")
        if "MelonPrimeMetalDefaultLibrary" not in header_text:
            issues.append("MelonPrimeMetalLibrary.h missing MelonPrimeMetalDefaultLibrary() declaration")

    if not library_impl.is_file():
        issues.append("missing src/MelonPrimeMetalLibrary.mm")
    else:
        impl_text = library_impl.read_text(encoding="utf-8")
        if "newLibraryWithURL" not in impl_text and "newLibraryWithData" not in impl_text:
            issues.append(
                "MelonPrimeMetalLibrary.mm must load the bundled metallib via "
                "-newLibraryWithURL: or -newLibraryWithData: (found neither)"
            )
        if "newLibraryWithSource" in impl_text:
            issues.append(
                "MelonPrimeMetalLibrary.mm must not compile shader source at "
                "runtime -- it is the bundled-metallib loader itself"
            )

    cmake_path = ROOT / "src/frontend/qt_sdl/CMakeLists.txt"
    cmake_text = cmake_path.read_text(encoding="utf-8")
    if BUNDLED_METALLIB_MARKER not in cmake_text:
        issues.append(f"CMakeLists.txt missing {BUNDLED_METALLIB_MARKER} marker")
    for needle in ("-sdk macosx metal", "-sdk macosx metallib", "MACOSX_PACKAGE_LOCATION \"Resources\""):
        if needle not in cmake_text:
            issues.append(f"CMakeLists.txt missing expected metallib build fragment: {needle!r}")
    if "MelonPrimeMetalLibrary.mm" not in cmake_text:
        issues.append("CMakeLists.txt does not compile MelonPrimeMetalLibrary.mm into the core target")

    for rel_path, constants in MIGRATED_SITES:
        path = ROOT / rel_path
        if not path.is_file():
            issues.append(f"missing migrated call-site file {rel_path}")
            continue
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        if "MelonPrimeMetalDefaultLibrary(" not in text:
            issues.append(f"{rel_path} does not call MelonPrimeMetalDefaultLibrary()")

        mask = guarded_line_mask(lines)
        for constant in constants:
            # Every remaining use of this embedded shader-source constant as
            # a -newLibraryWithSource:/-initWithUTF8String: argument must be
            # inside the debug-fallback guard.
            for i, line in enumerate(lines):
                if constant in line and ("initWithUTF8String" in line or "stringWithUTF8String" in line):
                    if not mask[i]:
                        issues.append(
                            f"{rel_path}:{i + 1}: {constant} is compiled from "
                            f"source outside the {FALLBACK_GUARD} guard"
                        )

    if issues:
        print("FAIL: metal shader asset / metallib audit")
        for item in issues:
            print(f"  - {item}")
        return 1
    print("PASS: metal shader asset / metallib audit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
