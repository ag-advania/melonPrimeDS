#!/usr/bin/env python3
"""Regenerate or verify the MelonPrime Vulkan SPIR-V headers.

Release builds consume only the checked-in headers.  This developer tool keeps
the GLSL/header pairs and their canonical-LF SHA-256 values synchronized with
docs/development/rendering/vulkan-spirv-manifest.json.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "docs/development/rendering/vulkan-spirv-manifest.json"

# source, stage, symbol, header, additional glslc arguments
SHADERS = [
    ("src/GPU3D_Vulkan_InterpSpansShader.comp", "comp", "melonDS_gpu3d_vulkan_interp_spans_comp_spv", "src/GPU3D_Vulkan_InterpSpansShaderData.h", []),
    ("src/GPU3D_Vulkan_BinCombinedShader.comp", "comp", "melonDS_gpu3d_vulkan_bin_combined_comp_spv", "src/GPU3D_Vulkan_BinCombinedShaderData.h", []),
    ("src/GPU3D_Vulkan_CalculateWorkOffsetsShader.comp", "comp", "melonDS_gpu3d_vulkan_calc_work_offsets_comp_spv", "src/GPU3D_Vulkan_CalculateWorkOffsetsShaderData.h", []),
    ("src/GPU3D_Vulkan_SortWorkShader.comp", "comp", "melonDS_gpu3d_vulkan_sort_work_comp_spv", "src/GPU3D_Vulkan_SortWorkShaderData.h", []),
    ("src/GPU3D_Vulkan_TriRasterShader.comp", "comp", "melonDS_gpu3d_vulkan_tri_raster_comp_spv", "src/GPU3D_Vulkan_TriRasterShaderData.h", []),
    ("src/GPU3D_Vulkan_TriRasterBaseShader.comp", "comp", "melonDS_gpu3d_vulkan_tri_raster_base_comp_spv", "src/GPU3D_Vulkan_TriRasterBaseShaderData.h", []),
    ("src/GPU3D_Vulkan_TriRasterCompatShader.comp", "comp", "melonDS_gpu3d_vulkan_tri_raster_compat_comp_spv", "src/GPU3D_Vulkan_TriRasterCompatShaderData.h", []),
    ("src/GPU3D_Vulkan_DepthBlendShader.comp", "comp", "melonDS_gpu3d_vulkan_depth_blend_comp_spv", "src/GPU3D_Vulkan_DepthBlendShaderData.h", []),
    ("src/GPU3D_Vulkan_FinalPassShader.comp", "comp", "melonDS_gpu3d_vulkan_final_pass_comp_spv", "src/GPU3D_Vulkan_FinalPassShaderData.h", []),
    ("src/GPU3D_Vulkan_CaptureLineExportShader.comp", "comp", "melonDS_gpu3d_vulkan_capture_line_export_comp_spv", "src/GPU3D_Vulkan_CaptureLineExportShaderData.h", []),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.vert", "vert", "melonDS_gpu3d_vulkan_graphics_raster_vert_spv", "src/GPU3D_Vulkan_GraphicsRasterShaderVertexData.h", []),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterShaderFragmentData.h", []),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1", "-DMELONDS_FAST_OPAQUE_MODULATE=1", "-DMELONDS_FAST_TEXTURE_PUSH_CONSTANTS=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_toon_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateToonShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1", "-DMELONDS_FAST_OPAQUE_MODULATE=1", "-DMELONDS_FAST_TOON_MODE=1", "-DMELONDS_FAST_TEXTURE_PUSH_CONSTANTS=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_plain_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulatePlainShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1", "-DMELONDS_FAST_OPAQUE_MODULATE=1", "-DMELONDS_FAST_TOON_MODE=2", "-DMELONDS_FAST_TEXTURE_PUSH_CONSTANTS=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_toon_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaToonShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1", "-DMELONDS_FAST_OPAQUE_MODULATE=1", "-DMELONDS_FAST_TOON_MODE=1", "-DMELONDS_FAST_TEXTURE_PUSH_CONSTANTS=1", "-DMELONDS_FAST_OPAQUE_FULL_ALPHA=1"]),
    ("src/GPU3D_Vulkan_GraphicsRasterShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_raster_no_frag_depth_direct_fast_modulate_opaque_alpha_plain_frag_spv", "src/GPU3D_Vulkan_GraphicsRasterNoFragDepthDirectFastModulateOpaqueAlphaPlainShaderFragmentData.h", ["-DMELONDS_NO_FRAG_DEPTH=1", "-DMELONDS_DIRECT_TEXTURE_INDEXING=1", "-DMELONDS_FAST_OPAQUE_MODULATE=1", "-DMELONDS_FAST_TOON_MODE=2", "-DMELONDS_FAST_TEXTURE_PUSH_CONSTANTS=1", "-DMELONDS_FAST_OPAQUE_FULL_ALPHA=1"]),
    ("src/GPU3D_Vulkan_GraphicsNoColorShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_no_color_frag_spv", "src/GPU3D_Vulkan_GraphicsNoColorShaderData.h", []),
    ("src/GPU3D_Vulkan_GraphicsClearShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_clear_frag_spv", "src/GPU3D_Vulkan_GraphicsClearShaderData.h", []),
    ("src/GPU3D_Vulkan_GraphicsFinalShader.vert", "vert", "melonDS_gpu3d_vulkan_graphics_final_vert_spv", "src/GPU3D_Vulkan_GraphicsFinalShaderVertexData.h", []),
    ("src/GPU3D_Vulkan_GraphicsEdgeShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_edge_frag_spv", "src/GPU3D_Vulkan_GraphicsEdgeShaderData.h", []),
    ("src/GPU3D_Vulkan_GraphicsEdgeFogShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_edge_fog_frag_spv", "src/GPU3D_Vulkan_GraphicsEdgeFogShaderData.h", []),
    ("src/GPU3D_Vulkan_GraphicsFogShader.frag", "frag", "melonDS_gpu3d_vulkan_graphics_fog_frag_spv", "src/GPU3D_Vulkan_GraphicsFogShaderData.h", []),
    ("src/frontend/qt_sdl/MelonPrimeVulkanCompositorShader.comp", "comp", "melonDS_android_vulkan_compositor_comp_spv", "src/frontend/qt_sdl/MelonPrimeVulkanCompositorShaderData.h", []),
    ("src/frontend/qt_sdl/MelonPrimeVulkanAccumulate3dShader.comp", "comp", "melonDS_android_vulkan_accumulate_3d_comp_spv", "src/frontend/qt_sdl/MelonPrimeVulkanAccumulate3dShaderData.h", []),
    ("src/frontend/qt_sdl/MelonPrimeVulkanSurfacePresenter.vert", "vert", "melonDS_android_vulkan_surface_presenter_vert_spv", "src/frontend/qt_sdl/MelonPrimeVulkanSurfacePresenterVertexShaderData.h", []),
    ("src/frontend/qt_sdl/MelonPrimeVulkanSurfacePresenter.frag", "frag", "melonDS_android_vulkan_surface_presenter_frag_spv", "src/frontend/qt_sdl/MelonPrimeVulkanSurfacePresenterFragmentShaderData.h", []),
]


def canonical_sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()


def find_glslc(explicit: str | None) -> str:
    candidate = explicit or os.environ.get("GLSLC") or shutil.which("glslc")
    if not candidate:
        raise RuntimeError("glslc was not found; pass --glslc or set GLSLC")
    return candidate


def compiler_version(glslc: str) -> str:
    return subprocess.check_output([glslc, "--version"], text=True).strip()


def array_pattern(symbol: str) -> re.Pattern[str]:
    return re.compile(
        rf"unsigned char {re.escape(symbol)}\[\] = \{{.*?\n\}};\n"
        rf"unsigned int {re.escape(symbol)}_len = \d+;",
        re.DOTALL,
    )


def header_spirv(header: Path, symbol: str) -> bytes:
    text = header.read_text(encoding="utf-8")
    match = array_pattern(symbol).search(text)
    if not match:
        raise RuntimeError(f"SPIR-V array {symbol} was not found in {header}")
    return bytes(int(value, 16) for value in re.findall(r"0x([0-9a-fA-F]{2})", match.group(0)))


def render_array(symbol: str, spirv: bytes) -> str:
    lines = []
    for offset in range(0, len(spirv), 12):
        values = ", ".join(f"0x{value:02x}" for value in spirv[offset:offset + 12])
        lines.append(f"  {values},")
    return (
        f"unsigned char {symbol}[] = {{\n"
        + "\n".join(lines)
        + f"\n}};\nunsigned int {symbol}_len = {len(spirv)};"
    )


def compile_shader(glslc: str, source: Path, stage: str, flags: list[str]) -> bytes:
    with tempfile.NamedTemporaryFile(suffix=".spv", delete=False) as output:
        output_path = Path(output.name)
    try:
        subprocess.run(
            [glslc, f"-fshader-stage={stage}", *flags, "-o", str(output_path), str(source)],
            check=True,
        )
        return output_path.read_bytes()
    finally:
        output_path.unlink(missing_ok=True)


def only_generator_word_diff(expected: bytes, actual: bytes) -> bool:
    # SPIR-V word 2 is the non-semantic generator identifier.  The pinned NDK
    # compiler uses shaderc generator 0x000d000a; current shaderc uses
    # 0x000d000b.  Every instruction byte must still match exactly.
    return (
        len(expected) == len(actual)
        and expected[:8] == actual[:8]
        and expected[12:] == actual[12:]
    )


def load_manifest() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def verify_hashes(manifest: dict) -> list[str]:
    errors = []
    rows = {row["header"]: row for row in manifest["shaders"]}
    for source, _stage, _symbol, header, _flags in SHADERS:
        row = rows.get(header)
        if not row:
            errors.append(f"manifest entry missing: {header}")
            continue
        if row["source"] != source:
            errors.append(f"manifest source mismatch: {header}")
        for key, relative in (("source_sha256", source), ("header_sha256", header)):
            actual = canonical_sha256(ROOT / relative)
            if row.get(key) != actual:
                errors.append(f"{key} mismatch: {relative}")
    return errors


def update_manifest(manifest: dict, version: str) -> None:
    manifest["last_regenerated_with"] = version
    rows = []
    for source, stage, symbol, header, flags in SHADERS:
        rows.append({
            "source": source,
            "stage": stage,
            "symbol": symbol,
            "header": header,
            "flags": flags,
            "source_sha256": canonical_sha256(ROOT / source),
            "header_sha256": canonical_sha256(ROOT / header),
        })
    manifest["shaders"] = rows
    MANIFEST.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("check", "regenerate"))
    parser.add_argument("--glslc")
    args = parser.parse_args()

    try:
        glslc = find_glslc(args.glslc)
        version = compiler_version(glslc)
        manifest = load_manifest()
        if args.mode == "check":
            errors = verify_hashes(manifest)
            if errors:
                raise RuntimeError("\n".join(errors))

        generator_drift = 0
        for source, stage, symbol, header, flags in SHADERS:
            source_path = ROOT / source
            header_path = ROOT / header
            generated = compile_shader(glslc, source_path, stage, flags)
            checked_in = header_spirv(header_path, symbol)
            if args.mode == "check":
                if checked_in == generated:
                    continue
                if only_generator_word_diff(checked_in, generated):
                    generator_drift += 1
                    continue
                raise RuntimeError(f"generated SPIR-V differs: {header}")

            text = header_path.read_text(encoding="utf-8")
            text, replacements = array_pattern(symbol).subn(render_array(symbol, generated), text, count=1)
            if replacements != 1:
                raise RuntimeError(f"could not update SPIR-V array: {header}")
            header_path.write_text(text, encoding="utf-8")
            print(f"updated {header}")

        if args.mode == "regenerate":
            update_manifest(manifest, version)
            print(f"updated {MANIFEST.relative_to(ROOT)}")
        else:
            if generator_drift:
                print(
                    f"verified {len(SHADERS)} shaders; {generator_drift} pinned headers differ "
                    "only in the non-semantic SPIR-V generator word"
                )
            else:
                print(f"verified {len(SHADERS)} Vulkan SPIR-V headers byte-for-byte")
        print(version)
        return 0
    except (OSError, KeyError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"Vulkan SPIR-V {args.mode} failed: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
