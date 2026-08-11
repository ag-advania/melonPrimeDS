#!/usr/bin/env python3
"""Shared description of the MelonPrimeDS Vulkan compute shader set.

Imported by both `tools/vulkan/compile-shaders.py` (the generator) and
`tools/ci/audits/check-vulkan-shaders.py` (the CI audit) so the pipeline list,
the tile geometry derivation and the tool discovery never drift apart.

Everything here mirrors the OpenGL compute renderer:
  * the pipeline list and its ordering come from
    `ComputeRenderer3D::ShaderCompileStep()` in src/GPU3D_Compute.cpp
    (33 pipelines, indices 0..32);
  * `tile_geometry()` mirrors `ComputeRenderer3D::SetRenderSettings()`;
  * `host_geometry()` mirrors the buffer sizing in that same function.
"""

from __future__ import annotations

import glob
import os
import shutil

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHADER_DIR = os.path.join(REPO_ROOT, "src", "GPU3D_Vulkan_shaders")
GENERATED_DIR = os.path.join(SHADER_DIR, "generated")

# Scales the renderer supports. The contract requires 1x..16x.
MIN_SCALE = 1
MAX_SCALE = 16

# Descriptor-set binding contract. Duplicated into the generated header so the
# C++ pipeline layout and the GLSL can be cross-checked mechanically.
SET0_BINDINGS = [
    ("MetaUniform", 0),
    ("PolygonBuffer", 1),
    ("XSpanSetupsBuffer", 2),
    ("YSpanSetupsBuffer", 3),
    ("ColorTileBuffer", 4),
    ("DepthTileBuffer", 5),
    ("AttrTileBuffer", 6),
    ("ResultBuffer", 7),
    ("BinResultBuffer", 8),
    ("WorkDescBuffer", 9),
    ("SetupIndices", 10),
    ("FinalFB", 11),
    # Presentation stages (Resolve / Compositor). Appended after the rasterizer's
    # own bindings so no existing binding number ever moved.
    ("StructuredInput", 12),
    ("PresentationOutput", 13),
    ("CaptureSidecar", 14),
    ("BlendState", 15),
]
SET1_BINDINGS = [
    ("CurrentTexture", 0),
    ("Capture128Texture", 1),
    ("Capture256Texture", 2),
    ("ClearBitmapColor", 3),
    ("ClearBitmapDepth", 4),
]

# Specialization constant ids declared in src/GPU3D_Vulkan_shaders/Common.glsl.
SPEC_CONSTANTS = [
    ("ScreenWidth", 0),
    ("ScreenHeight", 1),
    ("MaxWorkTiles", 2),
]

PUSH_CONSTANT_SIZE = 32

# Fixed sizes transcribed from GPU3D_Compute.h / the GLSL.
BIN_STRIDE = 2048 // 32
COARSE_BIN_STRIDE = BIN_STRIDE // 32
MAX_VARIANTS = 256
COARSE_TILE_COUNT_X = 8
BIN_RESULT_HEADER_BYTES = (
    MAX_VARIANTS * 16      # uvec4 VariantWorkCount[256]
    + MAX_VARIANTS * 4     # uint  SortedWorkOffset[256]
    + MAX_VARIANTS * 4     # uint  VariantWorkRealCount[256]   (MelonPrimeDS)
    + 16                   # uvec4 SortWorkWorkCount
)

# WorkDescs pack the tile index into 21 bits (see WorkDescBuffer.glsl).
WORKDESC_TILE_INDEX_BITS = 21
# ...and the polygon index into 11 bits.
WORKDESC_POLYGON_INDEX_BITS = 11

# Vulkan's guaranteed minimums (VkPhysicalDeviceLimits). Exceeding one of these
# is not a defect -- desktop drivers report far larger values -- but the host
# must query before offering the scale, so the audit reports them.
VK_MIN_MAX_STORAGE_BUFFER_RANGE = 1 << 27          # 134217728
VK_MIN_MAX_COMPUTE_WORKGROUP_INVOCATIONS = 128
VK_MIN_MAX_COMPUTE_WORKGROUP_COUNT = 65535

# MelonPrimeDS split-dispatch chunk, see BinningBuffer.glsl.
RASTERISE_CHUNK_SIZE = 32768

RASTERISE_KINDS = [
    ("NoTexture", ["NoTexture"]),
    ("NoTextureToon", ["NoTexture", "Toon"]),
    ("NoTextureHighlight", ["NoTexture", "Highlight"]),
    ("UseTextureDecal", ["UseTexture", "Decal"]),
    ("UseTextureModulate", ["UseTexture", "Modulate"]),
    ("UseTextureToon", ["UseTexture", "Toon"]),
    ("UseTextureHighlight", ["UseTexture", "Highlight"]),
    ("ShadowMask", ["ShadowMask"]),
]


def pipelines() -> list[tuple[str, str, list[str]]]:
    """(pipeline name, .comp source stem, defines) in GL ShaderCompileStep order.

    Indices 0..32 are deliberately identical to
    ComputeRenderer3D::ShaderCompileStep() so the two renderers' pipeline
    indices can be compared directly when debugging.

    Indices 33..35 are the three *presentation* stages, which have no OpenGL
    compute counterpart: the GL renderer hands its FinalFB texture to the GL 2D
    engine instead. They are appended rather than interleaved so the shared
    prefix keeps matching. The DX12 backend numbers its own steps the same way
    (Resolve / CaptureSidecar / Compositor after ShaderStep_FinalPass0+8).
    """
    out: list[tuple[str, str, list[str]]] = []

    for buf in ("ZBuffer", "WBuffer"):                       # 0, 1
        out.append((f"InterpSpans{buf[0]}", "InterpSpans", ["InterpSpans", buf]))

    out.append(("BinCombined", "BinCombined", ["BinCombined"]))   # 2

    for buf in ("ZBuffer", "WBuffer"):                       # 3, 4
        out.append((f"DepthBlend{buf[0]}", "DepthBlend", ["DepthBlend", buf]))

    for kind_name, kind_defines in RASTERISE_KINDS:          # 5..20
        for buf in ("ZBuffer", "WBuffer"):
            out.append((
                f"Rasterise{kind_name}{buf[0]}",
                "Rasterise",
                ["Rasterise", buf] + kind_defines,
            ))

    out.append(("ClearCoarseBinMask", "ClearCoarseBinMask", ["ClearCoarseBinMask"]))          # 21
    out.append(("ClearIndirectWorkCount", "ClearIndirectWorkCount", ["ClearIndirectWorkCount"]))  # 22
    out.append(("CalculateWorkOffsets", "CalculateWorkOffsets", ["CalculateWorkOffsets"]))    # 23
    out.append(("SortWork", "SortWork", ["SortWork"]))                                        # 24

    for variant in range(8):                                 # 25..32
        defines = ["FinalPass"]
        if variant & 1:
            defines.append("EdgeMarking")
        if variant & 2:
            defines.append("Fog")
        if variant & 4:
            defines.append("AntiAliasing")
        out.append((f"FinalPass{variant}", "FinalPass", defines))

    out.append(("Resolve", "Resolve", ["Resolve"]))                 # 33
    out.append(("CaptureSidecar", "CaptureSidecar", ["CaptureSidecar"])) # 34
    out.append(("Compositor", "Compositor", ["Compositor"]))        # 35

    return out


def bucket_for_scale(scale: int) -> int:
    """`range` in ComputeRenderer3D::SetRenderSettings(); 0, 1 or 2."""
    return (1 if scale >= 5 else 0) + (1 if scale >= 9 else 0)


BUCKET_COUNT = 3
# Lowest scale in each bucket, used as the representative when compiling.
BUCKET_REPRESENTATIVE_SCALE = [1, 5, 9]


def tile_geometry(bucket: int) -> dict[str, int]:
    """The four literal-only constants, derived exactly as the GL renderer does."""
    tile_size = 8 << bucket
    coarse_tile_count_y = 4 + ((bucket >> 1) << 1)
    return {
        "TileSize": tile_size,
        "CoarseTileCountY": coarse_tile_count_y,
        "CoarseTileArea": COARSE_TILE_COUNT_X * coarse_tile_count_y,
        "ClearCoarseBinMaskLocalSize": 64 - ((bucket >> 1) << 4),
    }


def host_geometry(scale: int) -> dict[str, int]:
    """Everything ComputeRenderer3D::SetRenderSettings() derives for one scale.

    Used for the specialization constant values and for the audit's resource
    sizing / dispatch overflow checks.
    """
    bucket = bucket_for_scale(scale)
    geo = dict(tile_geometry(bucket))
    tile_size = geo["TileSize"]

    screen_w = 256 * scale
    screen_h = 192 * scale
    tile_shift = 3 + bucket
    tiles_per_line = screen_w >> tile_shift
    tile_lines = screen_h >> tile_shift

    geo.update({
        "Scale": scale,
        "Bucket": bucket,
        "ScreenWidth": screen_w,
        "ScreenHeight": screen_h,
        "TilesPerLine": tiles_per_line,
        "TileLines": tile_lines,
        "MaxWorkTiles": (tiles_per_line * tile_lines) << 4,
        "CoarseTileW": COARSE_TILE_COUNT_X * tile_size,
        "CoarseTileH": geo["CoarseTileCountY"] * tile_size,
        # Every valid DS polygon may cover the full output height.
        "MaxYSpanIndices": screen_h * 2048,
    })
    return geo


def spec_constant_values(scale: int) -> dict[str, int]:
    geo = host_geometry(scale)
    return {
        "ScreenWidth": geo["ScreenWidth"],
        "ScreenHeight": geo["ScreenHeight"],
        "MaxWorkTiles": geo["MaxWorkTiles"],
    }


def _exe(name: str) -> str:
    return name + (".exe" if os.name == "nt" else "")


def find_tool(name: str, override: str | None = None) -> str | None:
    """Locate a Khronos tool.

    Search order: explicit override, VULKAN_SDK, MSYS2 mingw64, the vcpkg
    buildtrees this repository configures, then PATH.
    """
    if override:
        return override if os.path.isfile(override) else None

    candidates: list[str] = []

    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        candidates.append(os.path.join(sdk, "Bin", _exe(name)))
        candidates.append(os.path.join(sdk, "bin", _exe(name)))

    candidates.append(os.path.join("C:\\", "msys64", "mingw64", "bin", _exe(name)))
    candidates.append(os.path.join("/usr", "bin", name))
    candidates.append(os.path.join("/usr", "local", "bin", name))

    candidates += sorted(glob.glob(
        os.path.join(REPO_ROOT, "vcpkg", "buildtrees", "*", "*", "**", _exe(name)),
        recursive=True,
    ))

    for path in candidates:
        if os.path.isfile(path):
            return path

    return shutil.which(name)
