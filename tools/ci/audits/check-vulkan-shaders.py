#!/usr/bin/env python3
"""Compile every MelonPrime Vulkan compute shader variant and validate the SPIR-V.

The Vulkan renderer consumes SPIR-V generated offline by
tools/vulkan/compile-shaders.py, so a GLSL error would only surface when
somebody regenerates -- and a SPIR-V rule violation would only surface as
undefined behaviour on one vendor's driver. This audit assembles exactly the
same sources the generator assembles (same `-D` prologue, same include path,
same target environment), compiles all of them, and runs spirv-val over every
module.

It additionally checks the parts of the design that no single compile can
cover:

  * every internal resolution scale 1x..16x maps to a compiled tile-geometry
    bucket, and specializing that bucket's module with the scale's
    specialization constants still validates (needs spirv-opt);
  * the host-side resource sizing and dispatch math derived from each scale
    does not overflow the 32-bit indices the GLSL uses, does not overflow the
    21-bit work-descriptor tile field, and divides evenly where the dispatch
    assumes it does.

Usage:
    python tools/ci/audits/check-vulkan-shaders.py [--glslang PATH]
                                                   [--spirv-val PATH]
                                                   [--spirv-opt PATH]
                                                   [--scales 1,...,16]
                                                   [--verbose]

Exits non-zero if any variant fails to compile, fails validation, or if the
arithmetic checks find an overflow. Exits 0 with a SKIP message when the
Khronos tools are not installed, mirroring check-dx12-shaders.py.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile

_REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))))
sys.path.insert(0, os.path.join(_REPO_ROOT, "tools", "vulkan"))
import vulkan_shader_set as vss  # noqa: E402


def compile_variant(glslang: str, source: str, defines: dict[str, object],
                    out_path: str) -> tuple[bool, str]:
    args = [glslang, "--target-env", "vulkan1.1", "-I" + vss.SHADER_DIR]
    for name in sorted(defines):
        value = defines[name]
        args.append(f"-D{name}" if value is None else f"-D{name}={value}")
    args += ["-o", out_path, source]
    result = subprocess.run(args, capture_output=True, text=True)
    return result.returncode == 0, "\n".join(
        part.strip() for part in (result.stdout, result.stderr) if part.strip())


def validate(spirv_val: str, path: str) -> tuple[bool, str]:
    result = subprocess.run([spirv_val, "--target-env", "vulkan1.1", path],
                            capture_output=True, text=True)
    return result.returncode == 0, "\n".join(
        part.strip() for part in (result.stdout, result.stderr) if part.strip())


def specialize(spirv_opt: str, src: str, dst: str,
               values: dict[str, int]) -> tuple[bool, str]:
    """Bake one scale's specialization constants into a bucket module."""
    setting = " ".join(f"{cid}:{values[name]}" for name, cid in vss.SPEC_CONSTANTS)
    result = subprocess.run(
        [spirv_opt, "--set-spec-const-default-value", setting, src, "-o", dst],
        capture_output=True, text=True)
    return result.returncode == 0, "\n".join(
        part.strip() for part in (result.stdout, result.stderr) if part.strip())


INT32_MAX = (1 << 31) - 1
UINT32_MAX = (1 << 32) - 1


def check_scale_arithmetic(scale: int) -> tuple[list[str], list[str]]:
    """Resource sizing and dispatch math for one scale.

    Mirrors the allocations and glDispatchCompute calls in
    ComputeRenderer3D::SetRenderSettings() / RenderFrame().
    Returns (failures, notes).
    """
    geo = vss.host_geometry(scale)
    fail: list[str] = []
    note: list[str] = []

    tile_size = geo["TileSize"]
    screen_w = geo["ScreenWidth"]
    screen_h = geo["ScreenHeight"]
    tiles_per_line = geo["TilesPerLine"]
    tile_lines = geo["TileLines"]
    max_work_tiles = geo["MaxWorkTiles"]
    max_yspan = geo["MaxYSpanIndices"]

    # --- divisibility the dispatch shapes assume -------------------------
    for label, num, den in (
        ("ScreenWidth / TileSize", screen_w, tile_size),
        ("ScreenHeight / TileSize", screen_h, tile_size),
        ("ScreenWidth / CoarseTileW", screen_w, geo["CoarseTileW"]),
        ("ScreenHeight / CoarseTileH", screen_h, geo["CoarseTileH"]),
        ("ScreenWidth / 32 (FinalPass)", screen_w, 32),
        ("TilesPerLine*TileLines / ClearCoarseBinMaskLocalSize",
         tiles_per_line * tile_lines, geo["ClearCoarseBinMaskLocalSize"]),
    ):
        if num % den != 0:
            fail.append(f"scale={scale}: {label} is not exact ({num} / {den})")

    # --- bit-field capacity ---------------------------------------------
    # WorkDescs pack the work-tile index into 21 bits and the polygon index
    # into 11 bits (see WorkDescBuffer.glsl / BinCombined.comp).
    tile_field_max = (1 << vss.WORKDESC_TILE_INDEX_BITS) - 1
    if max_work_tiles > tile_field_max:
        fail.append(f"scale={scale}: MaxWorkTiles {max_work_tiles} exceeds the "
                    f"{vss.WORKDESC_TILE_INDEX_BITS}-bit WorkDesc tile field "
                    f"(max {tile_field_max})")

    # --- element indices, which the GLSL computes in signed 32-bit -------
    element_counts = {
        "ColorTiles/DepthTiles/AttrTiles": tile_size * tile_size * max_work_tiles,
        "ResultValue": screen_w * screen_h * 6,
        # Presentation stages. PresentationOutput is indexed as
        # screen * FramebufferStride + y * ScreenWidth + x by Compositor.comp.
        "PresentationOutput (Compositor)": screen_w * screen_h * 2,
        "StructuredInput": 6 * 256 * 192 + 2 * 192,
        "WorkDescs": max_work_tiles * 2,
        "XSpanSetups": max_yspan,
        "BinningMaskAndOffset": tiles_per_line * tile_lines
        * (vss.COARSE_BIN_STRIDE + 2 * vss.BIN_STRIDE),
    }
    for label, count in element_counts.items():
        if count > INT32_MAX:
            fail.append(f"scale={scale}: {label} needs {count} elements, which "
                        f"overflows the signed 32-bit index the GLSL uses")

    # --- buffer byte sizes ----------------------------------------------
    byte_sizes = {
        "TileMemory (x3)": 4 * tile_size * tile_size * max_work_tiles,
        "FinalTileMemory": 4 * 3 * 2 * screen_w * screen_h,
        "BinResultMemory": vss.BIN_RESULT_HEADER_BYTES
        + tiles_per_line * tile_lines * vss.COARSE_BIN_STRIDE * 4
        + tiles_per_line * tile_lines * vss.BIN_STRIDE * 4 * 2,
        "WorkDescMemory": max_work_tiles * 2 * 4 * 2,
        "XSpanSetupMemory": 96 * max_yspan,
        "YSpanIndicesMemory": max_yspan * 2 * 4,
        # Compositor output: two screens, BGRA8, at the internal resolution.
        "ComposeOutputMemory": 4 * 2 * screen_w * screen_h,
        # Native capture resolve and structured 2D input are DS-native by
        # definition and therefore scale-independent.
        "NativeResolveMemory": 4 * 256 * 192,
        "StructuredInputMemory": (6 * 256 * 192 + 2 * 192) * 4,
    }
    for label, size in byte_sizes.items():
        if size > UINT32_MAX:
            fail.append(f"scale={scale}: {label} is {size} bytes, which does not "
                        f"fit a 32-bit size")
        elif size > vss.VK_MIN_MAX_STORAGE_BUFFER_RANGE:
            note.append(f"scale={scale}: {label} is {size} bytes, above Vulkan's "
                        f"guaranteed maxStorageBufferRange "
                        f"({vss.VK_MIN_MAX_STORAGE_BUFFER_RANGE}); the host must "
                        f"query the limit before offering this scale")

    # --- dispatch group counts ------------------------------------------
    # Worst case per stage, i.e. every buffer filled to capacity.
    dispatches = {
        "ClearCoarseBinMask.x": tiles_per_line * tile_lines
        // geo["ClearCoarseBinMaskLocalSize"],
        "ClearIndirectWorkCount.x": (vss.MAX_VARIANTS + 31) // 32,
        "InterpSpans.x": (max_yspan + 31) // 32,
        "BinCombined.x": (2048 + 31) // 32,
        "BinCombined.y": screen_w // geo["CoarseTileW"],
        "BinCombined.z": screen_h // geo["CoarseTileH"],
        "CalculateWorkOffsets.x": (vss.MAX_VARIANTS + 31) // 32,
        "SortWork.x": (max_work_tiles + 31) // 32,
        "Rasterise.y": (max_work_tiles + vss.RASTERISE_CHUNK_SIZE - 1)
        // vss.RASTERISE_CHUNK_SIZE,
        "Rasterise.z": min(max_work_tiles, vss.RASTERISE_CHUNK_SIZE),
        "DepthBlend.x": tiles_per_line,
        "DepthBlend.y": tile_lines,
        "FinalPass.x": screen_w // 32,
        "FinalPass.y": screen_h,
        # Presentation stages, both 8x8 workgroups. Resolve is fixed at the DS's
        # native resolution; Compositor covers both screens in one dispatch.
        "Resolve.x": (256 + 7) // 8,
        "Resolve.y": (192 + 7) // 8,
        "Compositor.x": (screen_w + 7) // 8,
        "Compositor.y": (screen_h * 2 + 7) // 8,
    }
    for label, groups in dispatches.items():
        if groups > UINT32_MAX:
            fail.append(f"scale={scale}: {label} worst case is {groups} groups")
        elif groups > vss.VK_MIN_MAX_COMPUTE_WORKGROUP_COUNT:
            note.append(f"scale={scale}: {label} worst case is {groups} groups, "
                        f"above Vulkan's guaranteed maxComputeWorkGroupCount "
                        f"({vss.VK_MIN_MAX_COMPUTE_WORKGROUP_COUNT}); inherited "
                        f"from the OpenGL compute renderer, host must clamp")

    # --- workgroup invocations ------------------------------------------
    invocations = tile_size * tile_size
    if invocations > vss.VK_MIN_MAX_COMPUTE_WORKGROUP_INVOCATIONS:
        note.append(f"scale={scale}: Rasterise/DepthBlend use {invocations} "
                    f"invocations per workgroup, above Vulkan's guaranteed "
                    f"maxComputeWorkGroupInvocations "
                    f"({vss.VK_MIN_MAX_COMPUTE_WORKGROUP_INVOCATIONS}); the host "
                    f"must check VkPhysicalDeviceLimits before offering this scale")

    return fail, note


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--glslang", default=None, help="path to glslangValidator")
    parser.add_argument("--spirv-val", default=None, dest="spirv_val",
                        help="path to spirv-val")
    parser.add_argument("--spirv-opt", default=None, dest="spirv_opt",
                        help="path to spirv-opt (per-scale specialization check)")
    parser.add_argument("--scales", default=None,
                        help="comma-separated scales to check (default 1..16)")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    glslang = vss.find_tool("glslangValidator", args.glslang)
    spirv_val = vss.find_tool("spirv-val", args.spirv_val)
    if not glslang or not spirv_val:
        missing = [name for name, path in
                   (("glslangValidator", glslang), ("spirv-val", spirv_val))
                   if not path]
        print(f"SKIP: {' and '.join(missing)} not found "
              f"(install the Vulkan SDK, or MSYS2 mingw-w64-glslang and "
              f"mingw-w64-spirv-tools, to run this audit)")
        return 0

    spirv_opt = vss.find_tool("spirv-opt", args.spirv_opt)

    print(f"glslangValidator: {glslang}")
    print(f"spirv-val:        {spirv_val}")
    print(f"spirv-opt:        {spirv_opt or '(not found -- per-scale '
                                           'specialization check skipped)'}")

    if args.scales:
        scales = [int(part) for part in args.scales.split(",") if part.strip()]
    else:
        scales = list(range(vss.MIN_SCALE, vss.MAX_SCALE + 1))

    pipeline_list = vss.pipelines()
    failures = 0
    compiled = 0
    validated = 0
    specialized = 0
    notes: list[str] = []

    with tempfile.TemporaryDirectory(prefix="melonprime-vk-audit-") as tmpdir:
        spv_path = os.path.join(tmpdir, "shader.spv")
        spec_path = os.path.join(tmpdir, "specialized.spv")
        bucket_modules: dict[tuple[int, int], str] = {}

        # ---- compile + validate every pipeline in every bucket ----------
        for bucket in range(vss.BUCKET_COUNT):
            geometry = vss.tile_geometry(bucket)
            for index, (name, stem, defines) in enumerate(pipeline_list):
                source = os.path.join(vss.SHADER_DIR, stem + ".comp")
                all_defines: dict[str, object] = dict(geometry)
                for define in defines:
                    all_defines[define] = None

                ok, diagnostics = compile_variant(glslang, source, all_defines, spv_path)
                compiled += 1
                if not ok:
                    failures += 1
                    print(f"[FAIL] compile bucket={bucket} {name}")
                    print(diagnostics)
                    continue

                ok, diagnostics = validate(spirv_val, spv_path)
                validated += 1
                if not ok:
                    failures += 1
                    print(f"[FAIL] spirv-val bucket={bucket} {name}")
                    print(diagnostics)
                    continue

                kept = os.path.join(tmpdir, f"b{bucket}_p{index}.spv")
                os.replace(spv_path, kept)
                bucket_modules[(bucket, index)] = kept

                if args.verbose:
                    print(f"[ ok ] bucket={bucket} {name:34s} "
                          f"{os.path.getsize(kept):7d} bytes")

        # ---- per-scale specialization ----------------------------------
        if spirv_opt:
            for scale in scales:
                bucket = vss.bucket_for_scale(scale)
                values = vss.spec_constant_values(scale)
                for index, (name, _, _) in enumerate(pipeline_list):
                    module = bucket_modules.get((bucket, index))
                    if module is None:
                        continue  # already reported as a compile/validate failure

                    ok, diagnostics = specialize(spirv_opt, module, spec_path, values)
                    if not ok:
                        failures += 1
                        print(f"[FAIL] specialize scale={scale} {name}")
                        print(diagnostics)
                        continue

                    ok, diagnostics = validate(spirv_val, spec_path)
                    specialized += 1
                    if not ok:
                        failures += 1
                        print(f"[FAIL] spirv-val scale={scale} {name} "
                              f"(ScreenWidth={values['ScreenWidth']}, "
                              f"ScreenHeight={values['ScreenHeight']}, "
                              f"MaxWorkTiles={values['MaxWorkTiles']})")
                        print(diagnostics)

    # ---- arithmetic ----------------------------------------------------
    for scale in scales:
        scale_failures, scale_notes = check_scale_arithmetic(scale)
        notes += scale_notes
        for message in scale_failures:
            failures += 1
            print(f"[FAIL] {message}")

    for message in notes:
        print(f"[note] {message}")

    print()
    print(f"compiled  {compiled} variants "
          f"({len(pipeline_list)} pipelines x {vss.BUCKET_COUNT} tile geometry buckets)")
    print(f"validated {validated} modules with spirv-val")
    if spirv_opt:
        print(f"validated {specialized} scale-specialized modules "
              f"(scales {min(scales)}..{max(scales)})")
    else:
        print("per-scale specialization check SKIPPED (spirv-opt not found)")
    print(f"arithmetic checked for {len(scales)} scales, {len(notes)} device-limit notes")

    if failures:
        print(f"\nFAIL: {failures} problems in the Vulkan shader set")
        return 1

    print("\nPASS: every Vulkan compute shader variant compiles and validates")
    return 0


if __name__ == "__main__":
    sys.exit(main())
