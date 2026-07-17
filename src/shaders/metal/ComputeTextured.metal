// MelonPrimeDS -- Metal shader. Extracted from src/GPU3D_MetalComputeTexturedShaders.inc (kMetalComputeTexturedSource) by
// PR-14 (MSL asset/metallib). Content is unchanged from the embedded
// R"MSL(...)MSL" literal; only the physical location moved so it can
// be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;

constant uint MPMaxVariants = 256u;
constant uint MPVariantWorkCountStart = 0u;
constant uint MPTileSummaryRasterised = 0u;
constant uint MPTileSummaryTextured = 1u;
constant uint MPTileSummaryShadowMask = 2u;
constant uint MPTileSummarySkippedCapacity = 3u;
constant uint MPTileSummaryCoveredPixels = 4u;
constant uint MPTileSummaryColorHash = 5u;
constant uint MPTileSummaryDepthMin = 6u;
constant uint MPTileSummaryDepthMax = 7u;
constant uint MPXSpanLinear = 1u << 0u;
constant uint MPXSpanFillInside = 1u << 1u;
constant uint MPXSpanFillLeft = 1u << 2u;
constant uint MPXSpanFillRight = 1u << 3u;

struct MPSpanBinConfig
{
    uint numPolygons, numVariants, numSetupIndices;
    uint screenWidth, screenHeight, tileSize, tilesPerLine, tileLines;
    uint coarseTileCountX, coarseTileCountY, coarseTileW, coarseTileH;
    uint maxWorkTiles, binStride, coarseBinStride, polygonGroups;
    uint alphaRef, dispCnt, tileWorkCapacity, wBuffer;
};

struct MPSpanSetupX
{
    int X0, X1;
    int EdgeLenL, EdgeLenR, EdgeCovL, EdgeCovR;
    int XRecip;
    uint Flags;
    int Z0, Z1, W0, W1;
    int ColorR0, ColorG0, ColorB0;
    int ColorR1, ColorG1, ColorB1;
    int TexcoordU0, TexcoordV0;
    int TexcoordU1, TexcoordV1;
    int CovLInitial, CovRInitial;
};

struct MPRenderPolygon
{
    uint FirstXSpan;
    int YTop, YBot;
    int XMin, XMax;
    int XMinY, XMaxY;
    uint Variant;
    uint Attr;
    float TextureLayer;
};

struct MPVariantMeta
{
    uint Textured;
    uint BlendMode;
    uint TexParam;
    uint TexPalette;
    uint CaptureKind;
    uint CaptureLayer;
    uint CaptureYOffset;
    uint Reserved;
};

struct MPTexel { uint r, g, b, a; };

static inline MPTexel mp_zero_texel()
{
    MPTexel value;
    value.r = value.g = value.b = value.a = 0u;
    return value;
}

static inline uint mp_read8(device const uchar* data, uint mask, uint addr)
{
    return uint(data[addr & mask]);
}

static inline uint mp_read16(device const uchar* data, uint mask, uint addr)
{
    return mp_read8(data, mask, addr) |
           (mp_read8(data, mask, addr + 1u) << 8u);
}

static inline uint mp_read32(device const uchar* data, uint mask, uint addr)
{
    return mp_read16(data, mask, addr) |
           (mp_read16(data, mask, addr + 2u) << 16u);
}

static inline MPTexel mp_rgb555(uint value, uint alpha)
{
    MPTexel out;
    out.r = (value & 0x1Fu) << 1u;
    out.g = ((value >> 5u) & 0x1Fu) << 1u;
    out.b = ((value >> 10u) & 0x1Fu) << 1u;
    if (out.r != 0u) out.r++;
    if (out.g != 0u) out.g++;
    if (out.b != 0u) out.b++;
    out.a = alpha;
    return out;
}

static inline uint mp_mix_rgb555(uint a, uint b, uint wa, uint wb, uint shift)
{
    const uint r = ((((a & 0x1Fu) * wa) + ((b & 0x1Fu) * wb)) >> shift) & 0x1Fu;
    const uint g = (((((a >> 5u) & 0x1Fu) * wa) + (((b >> 5u) & 0x1Fu) * wb)) >> shift) & 0x1Fu;
    const uint bl = (((((a >> 10u) & 0x1Fu) * wa) + (((b >> 10u) & 0x1Fu) * wb)) >> shift) & 0x1Fu;
    return r | (g << 5u) | (bl << 10u);
}

static inline int mp_positive_mod(int value, int modulus)
{
    if (modulus <= 0) return 0;
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

static inline int mp_wrap_coordinate(int value, int size, bool repeat, bool mirror)
{
    if (size <= 1) return 0;
    if (!repeat) return clamp(value, 0, size - 1);
    if (!mirror) return mp_positive_mod(value, size);
    const int period = size * 2;
    const int wrapped = mp_positive_mod(value, period);
    return wrapped < size ? wrapped : period - 1 - wrapped;
}

static inline uint mp_linear_factor(const MPSpanSetupX span, int x)
{
    const int width = max(span.X1 - span.X0, 1);
    return min(uint(max(x - span.X0, 0)) * 256u / uint(width), 256u);
}

static inline uint mp_perspective_factor(const MPSpanSetupX span, int x)
{
    const int width = max(span.X1 - span.X0, 1);
    const uint offset = uint(clamp(x - span.X0, 0, width));
    const float w0 = float(max(span.W0, 0));
    const float w1 = float(max(span.W1, 0));
    const float denominator = float(offset) * w0 + float(uint(width) - offset) * w1;
    if (denominator <= 0.0f) return 0u;
    return min(uint((float(offset) * w0 * 256.0f) / denominator), 256u);
}

static inline int mp_interpolate(int a, int b, uint factor)
{
    return (a * int(256u - factor) + b * int(factor)) >> 8;
}

static inline uint mp_mul_factor_256(uint value, uint factor)
{
    return (value >> 8u) * factor + (((value & 0xFFu) * factor) >> 8u);
}

static inline uint mp_interpolate_depth(uint a, uint b, uint factor)
{
    if (a <= b) return a + mp_mul_factor_256(b - a, factor);
    return b + mp_mul_factor_256(a - b, 256u - factor);
}

static inline MPTexel mp_palette_texel(
    device const uchar* palette,
    uint paletteAddress,
    uint index,
    bool transparent)
{
    const uint value = mp_read16(palette, 0x1FFFFu, paletteAddress + index * 2u);
    return mp_rgb555(value, transparent ? 0u : 31u);
}

static inline MPTexel mp_sample_capture(
    texture2d_array<ushort, access::read> capture,
    uint layer,
    int x,
    int y,
    uint logicalWidth,
    uint logicalHeight)
{
    // MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1: direct RGB5551 -> MPTexel.
    (void)logicalWidth;
    (void)logicalHeight;
    if (layer >= capture.get_array_size()) return mp_zero_texel();
    const uint physicalWidth = max(capture.get_width(), 1u);
    const uint physicalHeight = max(capture.get_height(), 1u);
    const uint2 coord = uint2(
        min(uint(max(x, 0)), physicalWidth - 1u),
        min(uint(max(y, 0)), physicalHeight - 1u));
    const ushort value = capture.read(coord, layer).r;
    const uint r5 = value & 0x1Fu;
    const uint g5 = (value >> 5u) & 0x1Fu;
    const uint b5 = (value >> 10u) & 0x1Fu;
    const uint a1 = (value >> 15u) & 1u;
    MPTexel out;
    out.r = (r5 << 1u) | (r5 >> 4u);
    out.g = (g5 << 1u) | (g5 >> 4u);
    out.b = (b5 << 1u) | (b5 >> 4u);
    out.a = a1 * 31u;
    return out;
}

static inline MPTexel mp_decode_texture(
    MPVariantMeta variant,
    int rawX,
    int rawY,
    device const uchar* textureMemory,
    device const uchar* texturePalette,
    texture2d_array<ushort, access::read> capture128,
    texture2d_array<ushort, access::read> capture256)
{
    const uint texParam = variant.TexParam;
    const uint format = (texParam >> 26u) & 0x7u;
    const uint width = 8u << ((texParam >> 20u) & 0x7u);
    const uint height = 8u << ((texParam >> 23u) & 0x7u);
    const bool repeatS = (texParam & (1u << 16u)) != 0u;
    const bool repeatT = (texParam & (1u << 17u)) != 0u;
    const bool mirrorS = (texParam & (1u << 18u)) != 0u;
    const bool mirrorT = (texParam & (1u << 19u)) != 0u;

    int sampleY = rawY;
    if (variant.CaptureKind != 0u) sampleY += int(variant.CaptureYOffset);
    const int x = mp_wrap_coordinate(rawX, int(width), repeatS, mirrorS);
    const int y = mp_wrap_coordinate(sampleY, int(height), repeatT, mirrorT);

    if (variant.CaptureKind == 1u)
        return mp_sample_capture(capture128, variant.CaptureLayer, x, y, 128u, 128u);
    if (variant.CaptureKind == 2u)
        return mp_sample_capture(capture256, variant.CaptureLayer, x, y, 256u, 256u);

    const uint pixel = uint(x) + uint(y) * width;
    const uint textureAddress = (texParam & 0xFFFFu) * 8u;
    uint paletteAddress = variant.TexPalette * 16u;

    if (format == 1u)
    {
        const uint value = mp_read8(textureMemory, 0x7FFFFu, textureAddress + pixel);
        MPTexel out = mp_palette_texel(texturePalette, paletteAddress, value & 0x1Fu, false);
        const uint alpha3 = value >> 5u;
        out.a = alpha3 * 4u + alpha3 / 2u;
        return out;
    }
    if (format == 2u)
    {
        const uint value = mp_read8(textureMemory, 0x7FFFFu, textureAddress + (pixel >> 2u));
        const uint index = (value >> ((pixel & 3u) * 2u)) & 0x3u;
        paletteAddress >>= 1u;
        return mp_palette_texel(texturePalette, paletteAddress, index,
            (texParam & (1u << 29u)) != 0u && index == 0u);
    }
    if (format == 3u)
    {
        const uint value = mp_read8(textureMemory, 0x7FFFFu, textureAddress + (pixel >> 1u));
        const uint index = (value >> ((pixel & 1u) * 4u)) & 0xFu;
        return mp_palette_texel(texturePalette, paletteAddress, index,
            (texParam & (1u << 29u)) != 0u && index == 0u);
    }
    if (format == 4u)
    {
        const uint index = mp_read8(textureMemory, 0x7FFFFu, textureAddress + pixel);
        return mp_palette_texel(texturePalette, paletteAddress, index,
            (texParam & (1u << 29u)) != 0u && index == 0u);
    }
    if (format == 5u)
    {
        const uint blocksPerRow = max(width >> 2u, 1u);
        const uint block = (uint(x) >> 2u) + (uint(y) >> 2u) * blocksPerRow;
        const uint indices = mp_read32(textureMemory, 0x7FFFFu, textureAddress + block * 4u);
        const uint shift = (((uint(y) & 3u) * 4u) + (uint(x) & 3u)) * 2u;
        const uint index = (indices >> shift) & 0x3u;
        uint auxiliaryAddress = 0x20000u + ((textureAddress & 0x1FFFCu) >> 1u);
        if (textureAddress >= 0x40000u) auxiliaryAddress += 0x10000u;
        const uint auxiliary = mp_read16(textureMemory, 0x7FFFFu, auxiliaryAddress + block * 2u);
        const uint blockPalette = paletteAddress + (auxiliary & 0x3FFFu) * 4u;
        uint colors[4];
        colors[0] = mp_read16(texturePalette, 0x1FFFFu, blockPalette + 0u) | 0x8000u;
        colors[1] = mp_read16(texturePalette, 0x1FFFFu, blockPalette + 2u) | 0x8000u;
        colors[2] = mp_read16(texturePalette, 0x1FFFFu, blockPalette + 4u) | 0x8000u;
        colors[3] = mp_read16(texturePalette, 0x1FFFFu, blockPalette + 6u) | 0x8000u;
        switch ((auxiliary >> 14u) & 0x3u)
        {
        case 0u: colors[3] = 0u; break;
        case 1u:
            colors[2] = mp_mix_rgb555(colors[0], colors[1], 1u, 1u, 1u) | 0x8000u;
            colors[3] = 0u;
            break;
        case 2u: break;
        default:
            colors[2] = mp_mix_rgb555(colors[0], colors[1], 5u, 3u, 3u) | 0x8000u;
            colors[3] = mp_mix_rgb555(colors[0], colors[1], 3u, 5u, 3u) | 0x8000u;
            break;
        }
        return mp_rgb555(colors[index], (colors[index] & 0x8000u) != 0u ? 31u : 0u);
    }
    if (format == 6u)
    {
        const uint value = mp_read8(textureMemory, 0x7FFFFu, textureAddress + pixel);
        MPTexel out = mp_palette_texel(texturePalette, paletteAddress, value & 0x7u, false);
        out.a = value >> 3u;
        return out;
    }
    if (format == 7u)
    {
        const uint value = mp_read16(textureMemory, 0x7FFFFu, textureAddress + pixel * 2u);
        return mp_rgb555(value, (value & 0x8000u) != 0u ? 31u : 0u);
    }
    return mp_zero_texel();
}

kernel void mp_compute_rasterise_texture_variants(
    device atomic_uint* header [[buffer(0)]],
    device const MPRenderPolygon* polygons [[buffer(1)]],
    device const MPSpanSetupX* xSpans [[buffer(2)]],
    device const uint2* workDescs [[buffer(3)]],
    device const MPVariantMeta* variants [[buffer(4)]],
    device uint* colorTiles [[buffer(5)]],
    device uint* depthTiles [[buffer(6)]],
    device uint* attrTiles [[buffer(7)]],
    constant MPSpanBinConfig& config [[buffer(9)]],
    device const uchar* textureMemory [[buffer(10)]],
    device const uchar* texturePalette [[buffer(11)]],
    device const uint* toonTable [[buffer(12)]],
    texture2d_array<ushort, access::read> capture128 [[texture(0)]],
    texture2d_array<ushort, access::read> capture256 [[texture(1)]],
    uint gid [[thread_position_in_grid]])
{
    const uint rawWorkCount = atomic_load_explicit(
        &header[MPVariantWorkCountStart + 3u], memory_order_relaxed);
    const uint workCount = min(rawWorkCount, config.maxWorkTiles);
    if (gid >= workCount) return;

    const uint2 work = workDescs[config.maxWorkTiles + gid];
    const uint polygonIndex = work.y & 0x7FFu;
    const uint originalWorkIndex = work.y >> 11u;
    if (originalWorkIndex >= config.tileWorkCapacity) return;
    if (polygonIndex >= config.numPolygons || config.numVariants == 0u) return;

    const MPRenderPolygon polygon = polygons[polygonIndex];
    const uint variantIndex = min(polygon.Variant, config.numVariants - 1u);
    const MPVariantMeta variant = variants[variantIndex];
    const bool shadowMask = variant.BlendMode == 4u;

    const uint tileArea = config.tileSize * config.tileSize;
    const uint tileBase = originalWorkIndex * tileArea;
    for (uint pixel = 0u; pixel < tileArea; pixel++)
    {
        colorTiles[tileBase + pixel] = 0u;
        depthTiles[tileBase + pixel] = 0u;
        attrTiles[tileBase + pixel] = 0u;
    }

    const uint tileX = work.x & 0xFFFFu;
    const uint tileY = work.x >> 16u;
    const uint polyAlphaRaw = (polygon.Attr >> 16u) & 0x1Fu;
    const bool wireframe = polyAlphaRaw == 0u;
    const uint polyAlpha = wireframe ? 31u : polyAlphaRaw;
    const bool highlightMode = (config.dispCnt & (1u << 1u)) != 0u;

    for (uint localY = 0u; localY < config.tileSize; localY++)
    {
        const uint pixelY = tileY + localY;
        if (pixelY >= config.screenHeight || int(pixelY) < polygon.YTop || int(pixelY) >= polygon.YBot)
            continue;
        const int spanOffsetSigned = int(pixelY) - polygon.YTop;
        if (spanOffsetSigned < 0 || polygon.FirstXSpan + uint(spanOffsetSigned) >= config.numSetupIndices)
            continue;

        const MPSpanSetupX span = xSpans[polygon.FirstXSpan + uint(spanOffsetSigned)];
        const int insideStart = min(span.X0 + max(span.EdgeLenL, 0), span.X1);
        const int insideEnd = min(span.X1 - max(span.EdgeLenR, 0), span.X1);

        for (uint localX = 0u; localX < config.tileSize; localX++)
        {
            const uint pixelX = tileX + localX;
            if (pixelX >= config.screenWidth) continue;
            const int x = int(pixelX);
            if (x < span.X0 || x >= span.X1) continue;

            const bool insideLeft = x < insideStart;
            const bool insideRight = x >= insideEnd;
            const bool insideBody = !insideLeft && !insideRight;
            bool fill =
                (insideLeft && (span.Flags & MPXSpanFillLeft) != 0u) ||
                (insideRight && (span.Flags & MPXSpanFillRight) != 0u) ||
                (insideBody && (span.Flags & MPXSpanFillInside) != 0u);
            if (wireframe && insideBody && int(pixelY) != polygon.YTop && int(pixelY) != polygon.YBot - 1)
                fill = false;
            if (!fill) continue;

            uint attr = 0u;
            if (int(pixelY) == polygon.YTop) attr |= 0x4u;
            else if (int(pixelY) == polygon.YBot - 1) attr |= 0x8u;
            if (insideLeft) attr |= 0x1u | (31u << 8u);
            else if (insideRight) attr |= 0x2u | (31u << 8u);

            const uint linearFactor = mp_linear_factor(span, x);
            const uint perspectiveFactor = (span.Flags & MPXSpanLinear) != 0u
                ? linearFactor : mp_perspective_factor(span, x);
            const uint depthFactor = config.wBuffer != 0u ? perspectiveFactor : linearFactor;
            const uint z0 = uint(max(span.Z0, 0));
            const uint z1 = uint(max(span.Z1, 0));
            const uint depth = mp_interpolate_depth(z0, z1, depthFactor);
            const uint tilePixel = tileBase + localY * config.tileSize + localX;

            if (shadowMask)
            {
                // DS shadow-mask polygons contribute only coverage/depth. The
                // complete depth-blend pass consumes 0xFFFFFFFF as the mask
                // marker and maintains the two-layer stencil state.
                const uint color = 0xFFFFFFFFu;
                colorTiles[tilePixel] = color;
                depthTiles[tilePixel] = depth;
                attrTiles[tilePixel] = 0u;

                continue;
            }

            int vr = mp_interpolate(span.ColorR0, span.ColorR1, perspectiveFactor) >> 3;
            int vg = mp_interpolate(span.ColorG0, span.ColorG1, perspectiveFactor) >> 3;
            int vb = mp_interpolate(span.ColorB0, span.ColorB1, perspectiveFactor) >> 3;
            vr = clamp(vr, 0, 63); vg = clamp(vg, 0, 63); vb = clamp(vb, 0, 63);
            const int originalVertexR = vr;

            if (variant.BlendMode == 2u)
            {
                if (highlightMode) { vg = vr; vb = vr; }
                else
                {
                    const uint toon = toonTable[min(uint(vr >> 1), 31u)];
                    vr = int(toon & 0x3Fu);
                    vg = int((toon >> 8u) & 0x3Fu);
                    vb = int((toon >> 16u) & 0x3Fu);
                }
            }

            uint r = uint(vr), g = uint(vg), b = uint(vb), a = polyAlpha;
            if (variant.Textured != 0u)
            {
                const int u = mp_interpolate(span.TexcoordU0, span.TexcoordU1, perspectiveFactor);
                const int v = mp_interpolate(span.TexcoordV0, span.TexcoordV1, perspectiveFactor);
                const MPTexel texel = mp_decode_texture(
                    variant, u >> 4, v >> 4,
                    textureMemory, texturePalette, capture128, capture256);

                if (variant.BlendMode == 1u || variant.BlendMode == 3u)
                {
                    if (texel.a == 31u) { r = texel.r; g = texel.g; b = texel.b; }
                    else if (texel.a != 0u)
                    {
                        r = ((texel.r * texel.a) + (r * (31u - texel.a))) >> 5u;
                        g = ((texel.g * texel.a) + (g * (31u - texel.a))) >> 5u;
                        b = ((texel.b * texel.a) + (b * (31u - texel.a))) >> 5u;
                    }
                    a = polyAlpha;
                }
                else
                {
                    r = ((texel.r + 1u) * (r + 1u) - 1u) >> 6u;
                    g = ((texel.g + 1u) * (g + 1u) - 1u) >> 6u;
                    b = ((texel.b + 1u) * (b + 1u) - 1u) >> 6u;
                    a = ((texel.a + 1u) * (polyAlpha + 1u) - 1u) >> 5u;
                }
            }

            if (variant.BlendMode == 2u && highlightMode)
            {
                const uint toon = toonTable[min(uint(originalVertexR >> 1), 31u)];
                r = min(r + (toon & 0x3Fu), 63u);
                g = min(g + ((toon >> 8u) & 0x3Fu), 63u);
                b = min(b + ((toon >> 16u) & 0x3Fu), 63u);
            }
            if (a <= config.alphaRef) continue;

            const uint color = min(r, 63u) | (min(g, 63u) << 8u) |
                               (min(b, 63u) << 16u) | (min(a, 31u) << 24u);
            colorTiles[tilePixel] = color;
            depthTiles[tilePixel] = depth;
            attrTiles[tilePixel] = attr;

        }
    }

}
