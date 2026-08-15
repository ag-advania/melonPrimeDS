// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1
// MELONPRIME_METAL_COMPUTE_COMPLETE_DEPTH_BLEND_V1
// MELONPRIME_METAL_COMPUTE_SUMMARY_FREE_PRODUCTION_KERNELS_V1
// Standalone MSL library for the Phase 8C texture-capable tile raster stage.
// The shared span-math fragment (GPU3D_MetalComputeSpanMath.inc) is prepended
// to this source at library-creation time. It owns SpanSetupX, the
// XSpanSetup_* flags and the DS fixed-point interpolation helpers.
static constexpr const char* kMetalComputeTexturedSource = R"MSL(

constant uint MPMaxVariants = 2048u;
constant uint MPVariantWorkCountStart = 0u;

struct MPSpanBinConfig
{
    uint numPolygons, numVariants, numSetupIndices;
    uint screenWidth, screenHeight, tileSize, tilesPerLine, tileLines;
    uint coarseTileCountX, coarseTileCountY, coarseTileW, coarseTileH;
    uint maxWorkTiles, binStride, polygonGroups;
    uint alphaRef, dispCnt, tileWorkCapacity, wBuffer;
    uint firstPolygon, batchPolygonCount;
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
    uint FacingView;
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
    texture2d_array<float, access::read> capture,
    uint layer,
    int x,
    int y,
    uint logicalWidth,
    uint logicalHeight)
{
    if (layer >= capture.get_array_size()) return mp_zero_texel();
    const uint physicalWidth = max(capture.get_width(), 1u);
    const uint physicalHeight = max(capture.get_height(), 1u);
    const uint scaleX = max(physicalWidth / max(logicalWidth, 1u), 1u);
    const uint scaleY = max(physicalHeight / max(logicalHeight, 1u), 1u);
    const uint2 coord = uint2(
        min(uint(max(x, 0)) * scaleX + scaleX / 2u, physicalWidth - 1u),
        min(uint(max(y, 0)) * scaleY + scaleY / 2u, physicalHeight - 1u));
    const float4 sampled = capture.read(coord, layer);
    MPTexel out;
    out.r = min(uint(sampled.r * 63.0f + 0.5f), 63u);
    out.g = min(uint(sampled.g * 63.0f + 0.5f), 63u);
    out.b = min(uint(sampled.b * 63.0f + 0.5f), 63u);
    out.a = min(uint(sampled.a * 31.0f + 0.5f), 31u);
    return out;
}

static inline MPTexel mp_decode_texture(
    MPVariantMeta variant,
    int rawX,
    int rawY,
    device const uchar* textureMemory,
    device const uchar* texturePalette,
    texture2d_array<float, access::read> capture128,
    texture2d_array<float, access::read> capture256)
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
    device const SpanSetupX* xSpans [[buffer(2)]],
    device const uint2* workDescs [[buffer(3)]],
    device const MPVariantMeta* variants [[buffer(4)]],
    device uint* colorTiles [[buffer(5)]],
    device uint* depthTiles [[buffer(6)]],
    device uint* attrTiles [[buffer(7)]],
    constant MPSpanBinConfig& config [[buffer(9)]],
    device const uchar* textureMemory [[buffer(10)]],
    device const uchar* texturePalette [[buffer(11)]],
    device const uint* toonTable [[buffer(12)]],
    texture2d_array<float, access::read> capture128 [[texture(0)]],
    texture2d_array<float, access::read> capture256 [[texture(1)]],
    uint3 groupID [[threadgroup_position_in_grid]],
    uint3 localID [[thread_position_in_threadgroup]])
{
    // One threadgroup per work item, one thread per tile pixel, matching the
    // OpenGL compute rasteriser's TileSize x TileSize workgroup. Every early
    // return below is uniform across the threadgroup and the kernel has no
    // barriers, so divergent exits are safe.
    const uint gid = groupID.x;
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
    const uint localX = localID.x;
    const uint localY = localID.y;
    if (localX >= config.tileSize || localY >= config.tileSize) return;

    const uint tilePixel = tileBase + localY * config.tileSize + localX;
    colorTiles[tilePixel] = 0u;
    depthTiles[tilePixel] = 0u;
    attrTiles[tilePixel] = 0u;

    const uint tileX = work.x & 0xFFFFu;
    const uint tileY = work.x >> 16u;
    // Wireframe polygons (alpha 0) keep alpha 0 through texture blending and
    // are forced opaque only at the very end, matching the OpenGL compute
    // rasteriser; substituting 31 earlier changes modulated texture alpha.
    const uint polyAlpha = (polygon.Attr >> 16u) & 0x1Fu;
    const bool highlightMode = (config.dispCnt & (1u << 1u)) != 0u;
    const bool wBuffer = config.wBuffer != 0u;

    {
        const uint pixelY = tileY + localY;
        if (pixelY >= config.screenHeight || int(pixelY) < polygon.YTop || int(pixelY) >= polygon.YBot)
            return;
        const int spanOffsetSigned = int(pixelY) - polygon.YTop;
        if (spanOffsetSigned < 0 || polygon.FirstXSpan + uint(spanOffsetSigned) >= config.numSetupIndices)
            return;

        const SpanSetupX span = xSpans[polygon.FirstXSpan + uint(spanOffsetSigned)];

        {
            const uint pixelX = tileX + localX;
            if (pixelX >= config.screenWidth) return;
            const int x = int(pixelX);
            if (x < span.X0 || x >= span.X1) return;

            // Wireframe suppression lives in the FillInside flag produced by
            // InterpSpans, exactly as in the OpenGL compute rasteriser.
            const bool insideLeft = x < span.InsideStart;
            const bool insideRight = x >= span.InsideEnd;
            const bool insideBody = !insideLeft && !insideRight;
            const bool fill =
                (insideLeft && (span.Flags & XSpanSetup_FillLeft) != 0u) ||
                (insideRight && (span.Flags & XSpanSetup_FillRight) != 0u) ||
                (insideBody && (span.Flags & XSpanSetup_FillInside) != 0u);
            if (!fill) return;

            uint attr = 0u;
            if (int(pixelY) == polygon.YTop) attr |= 0x4u;
            else if (int(pixelY) == polygon.YBot - 1) attr |= 0x8u;

            if (insideLeft)
            {
                attr |= 0x1u;
                int cov = span.EdgeCovL;
                if (cov < 0)
                {
                    // Software's scanline loop starts at the first visible
                    // pixel; clipped edge pixels do not consume coverage.
                    const int coverageStart = max(span.X0, 0);
                    const int xcov =
                        span.CovLInitial +
                        (span.EdgeCovL & 0x3FF) * (x - coverageStart);
                    cov = min(xcov >> 5, 31);
                }
                attr |= uint(cov) << 8u;
            }
            else if (insideRight)
            {
                attr |= 0x2u;
                int cov = span.EdgeCovR;
                if (cov < 0)
                {
                    // The imperative Software loops carry x through the left
                    // edge and body. When narrow edges overlap, the right
                    // coverage therefore starts after both intervals, not at
                    // InsideEnd. Clipping similarly starts coverage at x=0.
                    const int coverageStart =
                        max(max(span.InsideStart, span.InsideEnd), 0);
                    const int xcov =
                        span.CovRInitial +
                        (span.EdgeCovR & 0x3FF) * (x - coverageStart);
                    cov = max(0x1F - (xcov >> 5), 0);
                }
                attr |= uint(cov) << 8u;
            }
            else if ((config.dispCnt & (1u << 4u)) != 0u &&
                     (attr & 0xFu) != 0u)
            {
                // Software gives top/bottom-only AA edge pixels full
                // coverage and preserves the covered layer underneath.
                attr |= 0x1Fu << 8u;
            }

            uint depth;
            int u = span.TexcoordU0;
            int v = span.TexcoordV0;
            int vr = span.ColorR0;
            int vg = span.ColorG0;
            int vb = span.ColorB0;

            if (span.X0 == span.X1)
            {
                depth = uint(span.Z0);
            }
            else
            {
                const int ifactor = mp_calc_factor_x(span, x);
                const int idiff = span.X1 - span.X0;
                const int i = x - span.X0;

                depth = wBuffer
                    ? mp_interp_z_wbuffer(
                          span.Z0, span.Z1, ifactor, MPFactorShiftX, false)
                    : mp_interp_z_zbuffer(
                          span.Z0, span.Z1, i, span.XRecip, idiff, false);

                if ((span.Flags & XSpanSetup_Linear) == 0u)
                {
                    u = mp_interp_attr_persp(
                        span.TexcoordU0, span.TexcoordU1, ifactor, MPFactorShiftX);
                    v = mp_interp_attr_persp(
                        span.TexcoordV0, span.TexcoordV1, ifactor, MPFactorShiftX);
                    vr = mp_interp_attr_persp(
                        span.ColorR0, span.ColorR1, ifactor, MPFactorShiftX);
                    vg = mp_interp_attr_persp(
                        span.ColorG0, span.ColorG1, ifactor, MPFactorShiftX);
                    vb = mp_interp_attr_persp(
                        span.ColorB0, span.ColorB1, ifactor, MPFactorShiftX);
                }
                else
                {
                    u = mp_interp_attr_linear(
                        span.TexcoordU0, span.TexcoordU1, i, span.XRecip, idiff, false);
                    v = mp_interp_attr_linear(
                        span.TexcoordV0, span.TexcoordV1, i, span.XRecip, idiff, false);
                    vr = mp_interp_attr_linear(
                        span.ColorR0, span.ColorR1, i, span.XRecip, idiff, false);
                    vg = mp_interp_attr_linear(
                        span.ColorG0, span.ColorG1, i, span.XRecip, idiff, false);
                    vb = mp_interp_attr_linear(
                        span.ColorB0, span.ColorB1, i, span.XRecip, idiff, false);
                }
            }

            if (shadowMask)
            {
                // DS shadow-mask polygons contribute only coverage/depth. The
                // complete depth-blend pass consumes 0xFFFFFFFF as the mask
                // marker and maintains the two-layer stencil state.
                colorTiles[tilePixel] = 0xFFFFFFFFu;
                depthTiles[tilePixel] = depth;
                attrTiles[tilePixel] = 0u;
                return;
            }

            vr = clamp(vr >> 3, 0, 63);
            vg = clamp(vg >> 3, 0, 63);
            vb = clamp(vb >> 3, 0, 63);
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
                // Software narrows interpolated S/T to signed 16-bit before
                // converting from 12.4 fixed point. Preserve that wraparound
                // at texture-coordinate boundaries.
                u = int(short(u));
                v = int(short(v));
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
            if (polyAlpha == 0u) a = 31u;
            if (a <= config.alphaRef) return;

            const uint color = min(r, 63u) | (min(g, 63u) << 8u) |
                               (min(b, 63u) << 16u) | (min(a, 31u) << 24u);
            colorTiles[tilePixel] = color;
            depthTiles[tilePixel] = depth;
            attrTiles[tilePixel] = attr;

        }
    }

}
)MSL";
