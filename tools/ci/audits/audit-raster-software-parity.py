#!/usr/bin/env python3
"""Ratchet confirmed Software raster rules in the compute renderer family."""

from __future__ import annotations

from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[3]


def read(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(source: str, needle: str, label: str, failures: list[str]) -> None:
    if needle not in source:
        failures.append(f"{label}: missing {needle!r}")


def forbid(source: str, needle: str, label: str, failures: list[str]) -> None:
    if needle in source:
        failures.append(f"{label}: forbidden legacy contract {needle!r}")


def require_order(source: str, first: str, second: str, label: str,
                  failures: list[str]) -> None:
    first_at = source.find(first)
    second_at = source.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        failures.append(f"{label}: expected {first!r} before {second!r}")


def software_linear(y0: int, y1: int, offset: int, distance: int) -> int:
    if distance == 0 or y0 == y1:
        return y0
    if y0 < y1:
        return y0 + ((y1 - y0) * offset) // distance
    return y1 + ((y0 - y1) * (distance - offset)) // distance


def bounded_batches(costs: tuple[int, ...], capacity: int) -> list[tuple[int, int]]:
    batches: list[tuple[int, int]] = []
    first = 0
    count = 0
    used = 0
    for index, cost in enumerate(costs):
        if count and used + cost > capacity:
            batches.append((first, count))
            first = index
            count = 0
            used = 0
        if cost > capacity:
            raise ValueError("a single polygon must fit one full-screen tile set")
        used += cost
        count += 1
    if count:
        batches.append((first, count))
    return batches


def main() -> int:
    failures: list[str] = []

    edge_contract = read("src/GPU3D_RasterEdge.h")
    soft_header = read("src/GPU3D_Soft.h")
    soft_cpp = read("src/GPU3D_Soft.cpp")
    gl_cpp = read("src/GPU3D_Compute.cpp")
    gl_shader = read("src/GPU3D_Compute_shaders.h")
    edge_vectors = read("tools/testing/raster-edge-vectors.cpp")
    variant_index = read("src/GPU3D_FixedVariantIndex.h")

    # Includes the audit's reciprocal-rounding counterexample (expected 6).
    vectors = (
        (0, 80, 2, 23, 6),
        (80, 0, 2, 23, 73),
        (-80, 0, 2, 23, -74),
        (0, -80, 2, 23, -7),
        (17, 17, 9, 23, 17),
        (0x10000, 0x7FFFF, 22, 23, 504341),
    )
    for y0, y1, offset, distance, expected in vectors:
        actual = software_linear(y0, y1, offset, distance)
        if actual != expected:
            failures.append(
                f"linear vector {(y0, y1, offset, distance)}: "
                f"expected {expected}, got {actual}"
            )

    # Seventeen full-screen layers exceed the historical tiles*16 heuristic.
    # The parity contract must preserve all layers and their original order by
    # producing a second batch, never by truncating the seventeenth polygon.
    batches = bounded_batches((1,) * 17, 16)
    if batches != [(0, 16), (16, 1)]:
        failures.append(f"work batching vector: expected [(0, 16), (16, 1)], got {batches}")
    covered = [index for first, count in batches for index in range(first, first + count)]
    if covered != list(range(17)):
        failures.append(f"work batching order/loss: got {covered}")

    vk_header = read("src/GPU3D_Vulkan.h")
    vk_cpp = read("src/GPU3D_Vulkan.cpp")
    vk_common = read("src/GPU3D_Vulkan_shaders/Common.glsl")
    vk_depth = read("src/GPU3D_Vulkan_shaders/DepthBlend.comp")
    vk_interp = read("src/GPU3D_Vulkan_shaders/InterpSpans.comp")
    vk_edge = read("src/GPU3D_Vulkan_shaders/YSpanSetupBuffer.glsl")
    vk_bin = read("src/GPU3D_Vulkan_shaders/BinCombined.comp")
    vk_probe = read("src/VulkanFeatureProbe.cpp")

    dx_header = read("src/GPU3D_DX12.h")
    dx_cpp = read("src/GPU3D_DX12.cpp")
    dx_shader = read("src/GPU3D_DX12_shaders.h")
    metal_cpp = read("src/GPU3D_MetalCompute.mm")
    metal_span = read("src/GPU3D_MetalComputeSpanMath.inc")
    metal_textured = read("src/GPU3D_MetalComputeTexturedShaders.inc")
    metal_depth = read("src/GPU3D_MetalComputeDepthBlendShaders.inc")
    metal_final = read("src/GPU3D_MetalComputeFinalPassShaders.inc")
    metal_wrapper = read("src/GPU3D_Metal.mm")
    video_settings = read("src/frontend/qt_sdl/VideoSettingsDialog.cpp")
    vk_wrapper = read("src/GPU_Vulkan.cpp")
    dx_wrapper = read("src/GPU_DX12.cpp")

    for name, source in (("Vulkan", vk_header), ("DX12", dx_header)):
        require(source, "u32 FacingView;", f"{name} facing upload", failures)
        require(source, "MaxVariants = MaxRenderPolygons", f"{name} variant capacity", failures)
        require(source, "MaxYSpanSetups = MaxRenderPolygons * 10",
                f"{name} y-span setup capacity", failures)
        require(source, "std::array<PolygonBatch, MaxRenderPolygons> PolygonBatches",
                f"{name} allocation-free polygon batches", failures)

    for name, source in (("Vulkan", vk_cpp), ("DX12", dx_cpp)):
        require(source, "if (polygon->Degenerate)", f"{name} degenerate skip", failures)
        require(source, "const u32 i = numPolygons;", f"{name} compact polygon index", failures)
        require(source, "MaxYSpanIndices = ScreenHeight * MaxRenderPolygons;",
                f"{name} full-height span budget", failures)
        require(source, "BuildPolygonBatches", f"{name} bounded work batching", failures)
        require(source, "assert(polygonTiles <= capacity)",
                f"{name} single-polygon work guarantee", failures)

    require(edge_contract, "xlen != 1", "one-scanline vertical slope exception", failures)
    require(edge_contract, "InterpolationOriginOffset",
            "edge interpolation origin contract", failures)
    require(soft_header, "else if (ylen == xlen && xlen != 1)",
            "Software one-scanline vertical slope reference", failures)
    require(soft_cpp, "rp->SlopeR.Increment==0 && (rp->SlopeL.Increment!=0 || xstart != xend) && (xend != 0)",
            "Software conditional right vertical reference", failures)
    require(soft_cpp, "y == polygon->YBottom-1",
            "Software bottom edge reference", failures)
    require(gl_cpp, "RasterEdge::CalculateSlopeIncrement",
            "OpenGL Compute canonical slope helper", failures)
    require(gl_cpp, "RasterEdge::ConservativeRightVerticalMin",
            "OpenGL Compute conservative polygon bounds", failures)
    require(gl_cpp, "RasterEdge::InterpolationOriginOffset",
            "OpenGL Compute edge interpolation origin", failures)
    require(gl_cpp, "#ifndef MELONPRIME_DS\n        if (side) span->XMin--;",
            "OpenGL Compute guarded upstream vertical setup", failures)
    require(gl_cpp, "#ifdef MELONPRIME_DS\n            span->DxInitial = 0;\n#else\n            span->DxInitial = -0x40000;",
            "OpenGL Compute guarded upstream vertical origin", failures)

    for name, source in (("Vulkan", vk_cpp), ("DX12", dx_cpp)):
        require(source, "RasterEdge::CalculateSlopeIncrement",
                f"{name} canonical slope helper", failures)
        require(source, "RasterEdge::ConservativeRightVerticalMin",
                f"{name} conservative polygon bounds", failures)
        require(source, "RasterEdge::InterpolationOriginOffset",
                f"{name} edge interpolation origin", failures)
        forbid(source, "if (side) span->XMin--;",
               f"{name} unconditional right vertical decrement", failures)
        forbid(source, "span->DxInitial = -0x40000;",
               f"{name} encoded right vertical decrement", failures)

    require(metal_cpp, "RasterEdge::CalculateSlopeIncrement",
            "Metal canonical slope helper", failures)
    require(metal_cpp, "RasterEdge::ConservativeRightVerticalMin",
            "Metal conservative polygon bounds", failures)
    require(metal_cpp, "RasterEdge::InterpolationOriginOffset",
            "Metal edge interpolation origin", failures)
    require(metal_cpp, "polygon->Degenerate", "Metal degenerate skip", failures)
    require(metal_cpp, "const uint32_t polygonIndex = polygonCount++;",
            "Metal compact polygon index", failures)
    require(metal_cpp, "kMaxVariants = 2048", "Metal variant capacity", failures)
    require(metal_cpp, "kMaxPolygons * 10", "Metal y-span capacity", failures)
    require(metal_cpp, "State->ScreenHeight * kMaxPolygons",
            "Metal full-height span budget", failures)
    require(metal_cpp, "PolygonBatches", "Metal bounded work batching", failures)
    require(metal_cpp, "BlendContinuationState", "Metal batch continuation", failures)
    require(metal_cpp, "kRasterWorkCountStart = kSortWorkCountStart + 4",
            "Metal raster indirect argument", failures)
    require(metal_cpp, "dispatchThreadgroupsWithIndirectBuffer:slot->Header",
            "Metal work-count indirect dispatch", failures)
    require(metal_cpp, "UpdateSharedSnapshotForVersion",
            "Metal versioned VRAM snapshot", failures)
    require(metal_cpp, "State->TextureMemoryVersion++",
            "Metal texture dirty version", failures)
    require(metal_cpp, "State->TexturePaletteVersion++",
            "Metal palette dirty version", failures)
    require(metal_cpp, "MELONPRIME_METAL_COMPUTE_DEAD_WORK_REMOVAL_V1",
            "Metal dead-work removal contract", failures)
    require(metal_cpp, "std::memset([headerBuffer contents], 0xA5",
            "Metal GPU-owned header initialization self-test", failures)
    require(metal_final, "mpf_read_rgb6a5",
            "Metal reversible final-texture native resolve", failures)
    require(metal_final, "texture2d<float, access::read> finalTexture",
            "Metal native resolve texture source", failures)
    forbid(metal_cpp, "FinalColorBuffer",
           "Metal duplicate full-resolution final-color buffer", failures)
    forbid(metal_cpp, "std::memset([slot->Header contents]",
           "Metal per-frame CPU header clear", failures)
    forbid(metal_cpp, "CoarseMask",
           "Metal write-only coarse mask", failures)
    forbid(metal_cpp, "coarseMask",
           "Metal write-only coarse mask shader argument", failures)
    forbid(metal_cpp, "CoarseBinStride",
           "Metal unused coarse-mask stride", failures)
    forbid(metal_cpp, "coarseBinStride",
           "Metal unused coarse-mask shader stride", failures)
    forbid(metal_cpp, "mp_compute_clear_coarse_mask",
           "Metal unused coarse-mask clear dispatch", failures)
    forbid(metal_cpp, "keepCount", "Metal work-tile layer drop", failures)
    forbid(metal_cpp, "workOffset >= config.maxWorkTiles",
           "Metal work-tile overflow discard", failures)
    forbid(metal_cpp, "DispatchGroups(State->MaxWorkTiles, 32)",
           "Metal fixed-capacity sort dispatch", failures)
    forbid(metal_cpp, "MTLSizeMake(State->MaxWorkTiles, 1, 1)",
           "Metal fixed-capacity raster dispatch", failures)

    forbid(vk_cpp, "std::vector<PolygonBatch> polygonBatches",
           "Vulkan per-frame polygon-batch allocation", failures)
    forbid(vk_cpp, "std::vector<VkDescriptorSet> variantTextureSets",
           "Vulkan per-frame descriptor allocation", failures)
    require(vk_header, "TextureSetCacheCapacity = 4096",
            "Vulkan fixed descriptor cache", failures)
    # f4ec32dda's per-frame epoch reset (TextureSetCacheEpoch++) was replaced
    # by b8a5d35e1 with a bounded persistent-slot cache: the first
    # PersistentTextureSetCapacity distinct textures get a permanently
    # dedicated descriptor set (never recycled, so it never needs a reset),
    # and textures beyond that cap fall back to the per-frame ring allocator
    # uncached. Ratchet the bound that replaced the epoch, not the epoch
    # itself.
    require(vk_header, "PersistentTextureSetCapacity = 1024",
            "Vulkan bounded persistent descriptor cache", failures)
    require(vk_cpp, "PersistentTextureSetCursor < PersistentTextureSetCapacity",
            "Vulkan persistent-cache admission bound", failures)
    forbid(dx_cpp, "std::vector<PolygonBatch> polygonBatches",
           "DX12 per-frame polygon-batch allocation", failures)
    forbid(dx_header, "std::unordered_map<ID3D12Resource*",
           "DX12 per-frame SRV cache allocation", failures)
    require(dx_header, "FrameSrvCacheCapacity = 4096",
            "DX12 fixed SRV cache", failures)
    require(dx_cpp, "ResetFrameSrvCache()",
            "DX12 epoch SRV cache reset", failures)
    require(dx_cpp, "kRootParamStaticSrvTable = 2",
            "DX12 static SRV root table", failures)
    require(dx_cpp, "kRootParamTextureSrvTable = 3",
            "DX12 texture SRV root table", failures)
    require(dx_cpp, "textureSrvRange.BaseShaderRegister = 5",
            "DX12 texture SRV t5 register", failures)
    require(dx_cpp, "BindStaticSrvTable(list)",
            "DX12 once-per-frame static SRV bind", failures)
    require(dx_cpp, "Descriptors.Allocate(kTextureSrvCount, cpu, gpu)",
            "DX12 one-descriptor texture tables", failures)
    require(dx_cpp, "(MaxVariants + 1) * kTextureSrvCount + kUavTableSize",
            "DX12 full variant descriptor headroom", failures)
    forbid(dx_cpp, "kSrvTableSize = 6",
           "DX12 replicated six-descriptor texture tables", failures)

    require(variant_index, "class FixedVariantIndex",
            "shared fixed variant index", failures)
    require(variant_index, "class AdaptiveVariantIndex",
            "L1-sized adaptive variant index", failures)
    require(variant_index, "std::array<Entry, Capacity> Entries",
            "allocation-free variant index storage", failures)
    require(variant_index, "if (CurrentGeneration != 0)",
            "variant epoch reset", failures)
    require(variant_index, "entry.Generation = 0",
            "variant epoch rollover clear", failures)
    for name, header, source in (("Vulkan", vk_header, vk_cpp),
                                 ("DX12", dx_header, dx_cpp)):
        require(header, "VariantIndexCapacity = 4096",
                f"{name} fixed variant index capacity", failures)
        require(source, "VariantLookup.Reset()",
                f"{name} epoch variant index reset", failures)
        require(source, "VariantLookup.Find(variantHash",
                f"{name} indexed variant lookup", failures)
        require(source, "variant index disagreed with legacy insertion order",
                f"{name} differential variant-sequence verifier", failures)
        require_order(source, "variants[numVariants] = variant;",
                      "VariantLookup.Insert(\n                        variantHash, numVariants",
                      f"{name} canonical variant insertion order", failures)
        forbid(source,
               "for (int j = static_cast<int>(numVariants) - 1; j >= 0; j--)",
               f"{name} linear variant fallback", failures)
    require(metal_cpp, "VariantIndexCapacity = 4096",
            "Metal fixed variant index capacity", failures)
    require(metal_cpp, "State->VariantLookup.Reset()",
            "Metal epoch variant index reset", failures)
    require(metal_cpp, "State->VariantLookup.Find(variantHash",
            "Metal indexed variant lookup", failures)
    require(metal_cpp, "variant index disagreed with legacy insertion order",
            "Metal differential variant-sequence verifier", failures)
    require_order(metal_cpp, "State->VariantData.push_back(key);",
                  "State->VariantLookup.Insert(\n                variantHash, variantIndex",
                  "Metal canonical variant insertion order", failures)
    forbid(metal_cpp,
           "std::find(State->VariantData.begin(), State->VariantData.end(), key)",
           "Metal linear variant fallback", failures)

    require(soft_header, "void RenderReferenceFrame();",
            "Software coherent-mirror differential entry point", failures)
    require(soft_cpp, "void SoftRenderer3D::RenderReferenceFrame()",
            "Software synchronous differential implementation", failures)
    for name, source in (("Vulkan", vk_wrapper), ("DX12", dx_wrapper)):
        require_order(source, "Renderer::Start3DRendering();",
                      "RenderReferenceFrame();",
                      f"{name} accelerated-first differential order", failures)
    require(metal_wrapper, "Delegate.RenderReferenceFrame();",
            "Metal coherent-mirror differential reference", failures)
    require(dx_wrapper, "if (composed && DifferentialReference",
            "DX12 composed-output differential gate", failures)

    signed_right_coverage = "max(31 - (xcov >> 5), 0)"
    if dx_shader.count(signed_right_coverage) != 2:
        failures.append(
            "DX12 signed right-edge coverage: expected exactly two signed clamps"
        )
    forbid(dx_shader, "max(0x1F - (xcov >> 5), 0)",
           "DX12 unsigned right-edge coverage underflow", failures)

    for name, source in (("OpenGL Compute", gl_shader),
                         ("Vulkan", vk_interp), ("DX12", dx_shader)):
        require(source, "ShouldDecrementRightVertical",
                f"{name} conditional right vertical helper", failures)
        require_order(source, "ShouldDecrementRightVertical(spanL, spanR, xl, xr)",
                      "swappedEdges", f"{name} right correction ordering", failures)
        require(source, "int i = y - spanL.I0;",
                f"{name} left edge Y interpolation", failures)
        require(source, "int i = y - spanR.I0;",
                f"{name} right edge Y interpolation", failures)

    require(metal_cpp, "ShouldDecrementRightVertical",
            "Metal conditional right vertical helper", failures)
    require_order(metal_cpp,
                  "ShouldDecrementRightVertical(spanL, spanR, xL, xR)",
                  "swappedEdges", "Metal right correction ordering", failures)
    require(metal_cpp, "int i = y - spanL.I0;",
            "Metal left edge Y interpolation", failures)
    require(metal_cpp, "int i = y - spanR.I0;",
            "Metal right edge Y interpolation", failures)

    require(gl_shader, "#ifdef MELONPRIME_DS\nR\"(\nbool ShouldDecrementRightVertical",
            "OpenGL Compute guarded parity shader helper", failures)
    require(gl_shader, "#else\nR\"(        int i = (spanL.Increment > 0x40000 ? xl : y) - spanL.I0;",
            "OpenGL Compute guarded upstream interpolation", failures)

    for vector in ("V1 ordinary right vertical", "V2 right vertical at x=0",
                   "V3 coincident vertical edges", "V4 one-scanline vertical increment",
                   "V5 swapped vertical AA coverage", "V6 bottom non-flat edge",
                   "V7 exact linear interpolation", "V8 alpha blend disabled",
                   "V9 front facing opaque-back tie", "V10 back facing attribute bit",
                   "V11 seventeen layer bounded batching",
                   "V12 degenerate compact polygon indices",
                   "V13 all edge flags preserve second layer",
                   "V14 accepted-pixel AA progression",
                   "V15 native quantized coordinates",
                   "V16 fixed variant collision and insertion order",
                   "V17 fixed variant epoch rollover"):
        require(edge_vectors, vector, f"executable raster vector {vector[:2]}", failures)

    require(vk_common, "Div64_32_32(numeratorHi, numeratorLo, denominator)",
            "Vulkan exact linear division", failures)
    require(dx_shader, "Div64_32_32(numeratorHi, numeratorLo, denominator)",
            "DX12 exact linear division", failures)
    require(metal_span, "numerator / ulong(denominator)",
            "Metal exact linear division", failures)
    forbid(metal_span, "3u << 24u", "Metal reciprocal linear approximation", failures)

    for name, source in (("Vulkan", vk_depth), ("DX12", dx_shader)):
        require(source, "0x00400010", f"{name} front-facing tie rule", failures)
        require(source, "tileDepth <= dstDepth", f"{name} front-facing <=", failures)
        require(source, "srcAttr |= 1", f"{name} back-facing destination attr", failures)
        require(source, "DispCnt & (1", f"{name} alpha blend enable", failures)

    require(metal_depth, "0x00400010u", "Metal front-facing tie rule", failures)
    require(metal_depth, "sourceDepth <= destinationDepth",
            "Metal front-facing <=", failures)
    require(metal_depth, "facingView ? 0u : (1u << 4u)",
            "Metal back-facing destination attr", failures)
    require(metal_depth, "alphaBlendEnabled", "Metal alpha blend enable", failures)
    require(metal_depth, "continuationState[gid]",
            "Metal shadow continuation state", failures)
    require(metal_depth, "(destinationAttr & 0xFu) == 0u",
            "Metal all-edge second-layer depth test", failures)
    require(metal_depth, "mp_compute_correct_accepted_coverage",
            "Metal accepted-pixel AA correction", failures)
    require(metal_depth, "resultWinner",
            "Metal AA winning-layer selection", failures)
    require(metal_textured, "MPMaxVariants = 2048u",
            "Metal textured variant capacity", failures)
    require(metal_textured, "u = int(short(u));",
            "Metal signed-16 texture coordinate narrowing", failures)
    require(metal_textured, "max(span.X0, 0)",
            "Metal clipped left-edge coverage origin", failures)
    require(metal_textured, "max(max(span.InsideStart, span.InsideEnd), 0)",
            "Metal overlapping right-edge coverage origin", failures)
    require(metal_cpp, "State->HiresCoordinates && State->ScaleFactor > 1",
            "Metal native quantized coordinate selection", failures)
    require(metal_cpp, "RasterReference.SetBetterPolygons(betterPolygons);",
            "Metal Compute polygon option is fallback-only", failures)
    forbid(metal_cpp, "State->BetterPolygons",
           "Metal Compute production polygon-splitting state", failures)
    require(video_settings,
            "ui->cbBetterPolygons->setEnabled(openGLRenderer || metalRasterRenderer);",
            "Metal Compute polygon-splitting UI gate", failures)
    require(video_settings, "MetalComputeBetterPolygonsDescription",
            "Metal Compute polygon-splitting UI explanation", failures)
    require(video_settings,
            "computeRenderer || metalRenderer || vulkanRenderer || dx12Renderer",
            "Metal high-resolution-coordinate UI gate", failures)
    require(metal_span, "swapped ? 0 : 31", "Metal swapped vertical coverage", failures)

    require(vk_interp, "swappedEdges", "Vulkan swapped edge path", failures)
    require(dx_shader, "swappedEdges", "DX12 swapped edge path", failures)
    require(vk_edge, "swapped ? 0 : 31", "Vulkan swapped vertical coverage", failures)
    require(dx_shader, "swapped ? 0 : 31", "DX12 swapped vertical coverage", failures)
    require(vk_interp, "polyalpha < 31U && (DispCnt & (1U << 3))",
            "Vulkan translucent edge condition", failures)
    require(dx_shader, "polyalpha < 31u && (DispCnt & (1u << 3))",
            "DX12 translucent edge condition", failures)

    require(vk_bin, "localIdx < 32 && localPolygonIdx < int(pc.TexWidth)",
            "Vulkan 48-lane ballot guard", failures)
    require(dx_shader, "localIdx < 32 && uint(localPolygonIdx) < TexWidth",
            "DX12 48-lane ballot guard", failures)
    require(vk_depth, "BlendContinuationState[resultOffset]",
            "Vulkan batch shadow continuation", failures)
    require(dx_shader, "BlendContinuationState[resultOffset]",
            "DX12 batch shadow continuation", failures)
    forbid(vk_bin, "keepCount", "Vulkan work-tile layer drop", failures)
    forbid(dx_shader, "keepCount", "DX12 work-tile layer drop", failures)
    forbid(vk_bin, "workOffset >= uint(MaxWorkTiles)",
           "Vulkan work-tile overflow discard", failures)
    forbid(dx_shader, "workOffset >= uint(MaxWorkTiles)",
           "DX12 work-tile overflow discard", failures)
    require(vk_probe, "YSpanIndicesPerScale = 192 * 2048",
            "Vulkan device-probe span budget", failures)
    require(vk_probe, "MaxVariants = 2048", "Vulkan device-probe variant budget", failures)

    if failures:
        print("Software raster parity audit FAILED:")
        for failure in failures:
            print(f"- {failure}")
        return 1

    print("PASS: confirmed Software parity and bounded hot-path rules are ratcheted for Metal, Vulkan and DX12")
    return 0


if __name__ == "__main__":
    sys.exit(main())
