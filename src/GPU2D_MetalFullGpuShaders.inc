// Included inside GPU2D_Metal.mm's anonymous namespace.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_HUD_PARITY_V1
// MELONPRIME_METAL_2D_SCANLINE_COORD_PARITY_V1
static constexpr const char* kMetal2DFullGpuShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct BGConfig
{
    uint2 size;
    uint type;
    uint palOffset;
    uint tileOffset;
    uint mapOffset;
    uint clamp;
    uint pad0;
};

struct LayerConfig
{
    uint vramMask;
    uint3 pad0;
    BGConfig bgConfig[4];
};

struct OAMConfig
{
    int2 position;
    int2 flip;
    int2 size;
    int2 boundSize;
    uint objMode;
    uint type;
    uint palOffset;
    uint tileOffset;
    uint tileStride;
    uint rotscale;
    uint bgPrio;
    uint mosaic;
};

struct SpriteConfig
{
    uint vramMask;
    uint3 pad0;
    int4 rotscale[32];
    OAMConfig oam[128];
};

struct ScanlineState
{
    int4 bgOffset[4];
    int4 bgRotscale[2];
    uint backColor;
    uint winRegs;
    uint winMask;
    uint pad0;
    int4 winPos;
    uint4 bgMosaicEnable;
    int4 mosaicSize;

    // Per-visible-line compositor state. Sampling only the VBlank value can
    // keep BG0/3D alive while dropping the HUD BG/OBJ layers.
    uint4 bgPrio;
    uint enableOBJ;
    uint enable3D;
    uint blendCnt;
    uint blendEffect;
    uint4 blendCoef;
};

struct ScanlineConfig
{
    ScanlineState scanline[192];
};

struct SpriteScanlineConfig
{
    int mosaicLine[192];
};

struct CompositorConfig
{
    uint4 bgPrio;
    uint enableOBJ;
    uint enable3D;
    uint blendCnt;
    uint blendEffect;
    uint4 blendCoef;
};

static inline uint mp_vram_read8(
    texture2d<uint, access::read> vram,
    uint mask,
    uint addr)
{
    return vram.read(uint2(addr & 0x3ffu, (addr >> 10) & mask)).r;
}

static inline uint mp_vram_read16(
    texture2d<uint, access::read> vram,
    uint mask,
    uint addr)
{
    return mp_vram_read8(vram, mask, addr) |
           (mp_vram_read8(vram, mask, addr + 1u) << 8u);
}

static inline float4 mp_palette_read(
    texture2d<uint, access::read> palette,
    uint row,
    uint id,
    bool transparent)
{
    uint raw = palette.read(uint2(id & 0xffu, row)).r;
    float3 rgb = float3(
        float(raw & 0x1fu),
        float((raw >> 5u) & 0x1fu),
        float((raw >> 10u) & 0x1fu)) / 31.0;
    return float4(rgb, transparent ? 0.0 : 1.0);
}

struct SpriteVertexOut
{
    float4 position [[position]];
    float2 texcoord;
    float2 screenPosition;
    uint spriteIndex [[flat]];
};

vertex SpriteVertexOut mp2d_sprite_vs(
    uint vertexID [[vertex_id]],
    uint instanceID [[instance_id]],
    constant SpriteConfig& config [[buffer(0)]],
    constant uint& numSprites [[buffer(1)]])
{
    constexpr float2 unit[6] = {
        float2(0.0, 1.0),
        float2(1.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0),
        float2(0.0, 0.0),
        float2(1.0, 0.0),
    };

    SpriteVertexOut out;
    uint spriteCount = max(numSprites, 1u);
    uint sprite = instanceID % spriteCount;
    uint wrapCopy = instanceID / spriteCount;
    out.spriteIndex = sprite;
    out.texcoord = unit[vertexID];

    if (sprite >= numSprites)
    {
        out.position = float4(-2.0, -2.0, 1.0, 1.0);
        out.screenPosition = float2(-1024.0);
        return out;
    }

    constant OAMConfig& oam = config.oam[sprite];
    int2 position = oam.position;
    if (wrapCopy != 0u)
        position.y &= 0xff;

    int2 bound = max(oam.boundSize, int2(0));
    bool visible =
        bound.x > 0 && bound.y > 0 &&
        position.x < 256 && (position.x + bound.x) > 0 &&
        position.y < 192 && (position.y + bound.y) > 0;
    if (!visible)
    {
        out.position = float4(-2.0, -2.0, 1.0, 1.0);
        out.screenPosition = float2(-1024.0);
        return out;
    }

    float2 pixelPosition = float2(position) + unit[vertexID] * float2(bound);
    float2 ndc = (pixelPosition * 2.0 / float2(256.0, 192.0)) - 1.0;
    uint totalPriority = oam.bgPrio * 128u + sprite;
    float depth = float(totalPriority) / 512.0;

    out.position = float4(ndc, depth, 1.0);
    out.screenPosition = pixelPosition;
    if (oam.rotscale == 0xffffffffu)
    {
        float2 local = unit[vertexID] * float2(bound);
        out.texcoord = mix(local, float2(bound) - local, float2(oam.flip));
    }
    else
    {
        out.texcoord = unit[vertexID] * float2(bound) - float2(bound) * 0.5;
    }
    return out;
}

static inline float4 mp_sprite_pixel(
    uint sprite,
    int2 coord,
    constant SpriteConfig& config,
    texture2d<uint, access::read> vram,
    texture2d<uint, access::read> palette,
    texture2d_array<float> capture128,
    texture2d_array<float> capture256,
    sampler repeatSampler)
{
    constant OAMConfig& oam = config.oam[sprite];
    if (coord.x < 0 || coord.y < 0 ||
        coord.x >= oam.size.x || coord.y >= oam.size.y)
        return float4(0.0);

    if (oam.type == 0u)
    {
        uint tileOffset = oam.tileOffset +
            (uint(coord.x) >> 3u) * 32u +
            (uint(coord.y) >> 3u) * oam.tileStride +
            ((uint(coord.x) & 7u) >> 1u) +
            ((uint(coord.y) & 7u) << 2u);
        uint color = mp_vram_read8(vram, config.vramMask, tileOffset);
        color = ((coord.x & 1) != 0) ? (color >> 4u) : (color & 0xFu);
        color += oam.palOffset;
        return mp_palette_read(palette, 0u, color, (color & 0xFu) == 0u);
    }
    if (oam.type == 1u)
    {
        uint tileOffset = oam.tileOffset +
            (uint(coord.x) >> 3u) * 64u +
            (uint(coord.y) >> 3u) * oam.tileStride +
            (uint(coord.x) & 7u) +
            ((uint(coord.y) & 7u) << 3u);
        uint color = mp_vram_read8(vram, config.vramMask, tileOffset);
        return mp_palette_read(palette, oam.palOffset, color, color == 0u);
    }
    if (oam.type == 2u)
    {
        uint tileOffset = oam.tileOffset +
            uint(coord.x) * 2u +
            uint(coord.y) * oam.tileStride;
        uint color = mp_vram_read16(vram, config.vramMask, tileOffset);
        float3 rgb = float3(
            float((color << 1u) & 0x3Eu),
            float((color >> 4u) & 0x3Eu),
            float((color >> 9u) & 0x3Eu)) / 63.0;
        return float4(rgb, float(color >> 15u));
    }
    if (oam.type == 3u)
    {
        // 128x128 display capture used as a bitmap OBJ.
        float2 captureCoord = float2(coord);
        captureCoord += float2(
            float((oam.tileOffset >> 1u) & 0x7Fu),
            float((oam.tileOffset >> 8u) & 0x7Fu));
        return capture128.sample(
            repeatSampler,
            fract(captureCoord / 128.0),
            oam.tileStride);
    }
    if (oam.type == 4u)
    {
        // 256x256 display capture used as a bitmap OBJ.
        float2 captureCoord = float2(coord);
        captureCoord += float2(
            float((oam.tileOffset >> 1u) & 0xFFu),
            float((oam.tileOffset >> 9u) & 0xFFu));
        return capture256.sample(
            repeatSampler,
            fract(captureCoord / 256.0),
            oam.tileStride);
    }
    return float4(0.0);
}

struct SpriteFragmentOut
{
    float4 color [[color(0)]];
    float4 flags [[color(1)]];
};

fragment SpriteFragmentOut mp2d_sprite_fs(
    SpriteVertexOut in [[stage_in]],
    constant SpriteConfig& config [[buffer(0)]],
    constant SpriteScanlineConfig& scanlines [[buffer(1)]],
    constant uint& passMode [[buffer(2)]],
    texture2d<uint, access::read> vram [[texture(0)]],
    texture2d<uint, access::read> palette [[texture(1)]],
    texture2d_array<float> capture128 [[texture(2)]],
    texture2d_array<float> capture256 [[texture(3)]],
    sampler repeatSampler [[sampler(0)]])
{
    SpriteFragmentOut out;
    out.color = float4(0.0);
    out.flags = float4(0.0);

    uint sprite = in.spriteIndex;
    constant OAMConfig& oam = config.oam[sprite];

    float2 coord = in.texcoord;
    int screenLine = clamp(int(in.screenPosition.y), 0, 191);
    if (oam.mosaic != 0u)
    {
        int mosaicLine = scanlines.mosaicLine[screenLine];
        float yMin = (oam.rotscale == 0xffffffffu)
            ? 0.0
            : -float(oam.size.y) * 0.5;
        float mosaicY = coord.y - float(screenLine - mosaicLine);
        if (coord.y >= yMin)
            coord.y = max(mosaicY, yMin);
    }

    if (oam.rotscale != 0xffffffffu)
    {
        int4 r = config.rotscale[oam.rotscale & 31u];
        float2 transformed;
        transformed.x = (coord.x * float(r.x) + coord.y * float(r.z)) / 256.0;
        transformed.y = (coord.x * float(r.y) + coord.y * float(r.w)) / 256.0;
        coord = transformed + float2(oam.size) * 0.5;
    }

    if (passMode == 1u)
    {
        if (oam.objMode == 2u || oam.mosaic == 0u)
            discard_fragment();
        out.flags.g = 1.0 / 255.0;
        out.flags.a = float(oam.bgPrio) / 255.0;
        return out;
    }

    float4 color = mp_sprite_pixel(
        sprite, int2(floor(coord)),
        config, vram, palette,
        capture128, capture256, repeatSampler);
    if (color.a <= 0.0)
        discard_fragment();

    if (passMode == 2u)
    {
        if (oam.objMode != 2u)
            discard_fragment();
        out.flags.b = 1.0;
        return out;
    }

    if (oam.objMode == 2u)
        discard_fragment();

    if (oam.objMode == 1u)
        out.flags.r = 1.0 / 255.0;
    else if (oam.objMode == 3u)
    {
        color.a = float(oam.palOffset) / 31.0;
        out.flags.r = 2.0 / 255.0;
    }

    if (oam.mosaic != 0u)
        out.flags.g = 1.0 / 255.0;
    out.flags.a = float(oam.bgPrio) / 255.0;
    out.color = color;
    return out;
}

struct CompositeVertexOut
{
    float4 position [[position]];
};

vertex CompositeVertexOut mp2d_composite_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    CompositeVertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

static inline int3 mp_convert_color(uint color)
{
    return int3(
        int((color & 0x1Fu) << 1u),
        int(((color & 0x3E0u) >> 4u) | (color >> 15u)),
        int((color & 0x7C00u) >> 9u));
}

static inline float4 mp_sample_bg(
    uint layer,
    float2 coord,
    constant LayerConfig& layerConfig,
    texture2d<float> bg0,
    texture2d<float> bg1,
    texture2d<float> bg2,
    texture2d<float> bg3,
    sampler repeatSampler)
{
    constant BGConfig& bg = layerConfig.bgConfig[layer];
    if (bg.size.x == 0u || bg.size.y == 0u)
        return float4(0.0);

    float2 uv = coord / float2(bg.size);
    if (bg.clamp != 0u &&
        (uv.x < 0.0 || uv.y < 0.0 || uv.x >= 1.0 || uv.y >= 1.0))
        return float4(0.0);

    if (layer == 0u) return bg0.sample(repeatSampler, uv);
    if (layer == 1u) return bg1.sample(repeatSampler, uv);
    if (layer == 2u) return bg2.sample(repeatSampler, uv);
    return bg3.sample(repeatSampler, uv);
}

static inline float4 mp_calculate_bg(
    uint layer,
    float2 coord,
    uint2 outputCoord,
    uint line,
    constant LayerConfig& layerConfig,
    constant ScanlineConfig& scanlineConfig,
    texture2d<float> bg0,
    texture2d<float> bg1,
    texture2d<float> bg2,
    texture2d<float> bg3,
    texture2d<float, access::read> high3D,
    texture2d_array<float> capture128,
    texture2d_array<float> capture256,
    sampler repeatSampler,
    int mosaicX)
{
    constant BGConfig& bg = layerConfig.bgConfig[layer];
    constant ScanlineState& scan = scanlineConfig.scanline[line];

    if (layer == 0u && bg.type == 6u)
    {
        // OpenGL applies the latched 3D BG offset and uses transparent
        // border outside the native 256x192 source. The previous Metal path
        // bypassed the offset and clamped out-of-range pixels to the edge.
        const float2 bgPosition =
            float2(scan.bgOffset[layer].xy) + coord;
        if (bgPosition.x < 0.0 || bgPosition.y < 0.0 ||
            bgPosition.x >= 256.0 || bgPosition.y >= 192.0)
        {
            return float4(0.0);
        }

        const float2 textureScale = float2(
            float(max(high3D.get_width(), 1u)) / 256.0,
            float(max(high3D.get_height(), 1u)) / 192.0);
        const uint2 sampleCoord = uint2(
            floor(bgPosition * textureScale));
        const uint2 maxCoord = uint2(
            max(high3D.get_width(), 1u) - 1u,
            max(high3D.get_height(), 1u) - 1u);
        return high3D.read(min(sampleCoord, maxCoord));
    }

    int2 offset = scan.bgOffset[layer].xy;
    float2 bgPosition;
    if (layer >= 2u && bg.type >= 2u)
    {
        bgPosition = float2(offset) / 256.0;
        int4 r = scan.bgRotscale[layer - 2u];
        bgPosition += float2(
            (coord.x * float(r.x) + coord.y * float(r.z)) / 256.0,
            (coord.x * float(r.y) + coord.y * float(r.w)) / 256.0);
    }
    else
    {
        bgPosition = float2(offset) + coord;
    }

    if (scan.bgMosaicEnable[layer] != 0u)
        bgPosition = floor(bgPosition) - float2(float(mosaicX), 0.0);

    if (bg.type >= 7u)
    {
        bgPosition.y += float(bg.mapOffset);
        float2 uv = bgPosition / float2(bg.size);
        if (bg.clamp != 0u &&
            (uv.x < 0.0 || uv.y < 0.0 ||
             uv.x >= 1.0 || uv.y >= 1.0))
        {
            return float4(0.0);
        }

        if (bg.type == 7u)
            return capture128.sample(repeatSampler, uv, bg.tileOffset);
        return capture256.sample(repeatSampler, uv, bg.tileOffset);
    }

    return mp_sample_bg(
        layer, bgPosition, layerConfig,
        bg0, bg1, bg2, bg3, repeatSampler);
}

fragment float4 mp2d_composite_fs(
    CompositeVertexOut in [[stage_in]],
    constant LayerConfig& layerConfig [[buffer(0)]],
    constant ScanlineConfig& scanlineConfig [[buffer(1)]],
    constant CompositorConfig& compositor [[buffer(2)]],
    constant uint& scale [[buffer(3)]],
    texture2d<float> bg0 [[texture(0)]],
    texture2d<float> bg1 [[texture(1)]],
    texture2d<float> bg2 [[texture(2)]],
    texture2d<float> bg3 [[texture(3)]],
    texture2d<float, access::read> high3D [[texture(4)]],
    texture2d_array<float, access::read> objLayer [[texture(5)]],
    texture2d<int, access::read> mosaicTexture [[texture(6)]],
    texture2d_array<float> capture128 [[texture(7)]],
    texture2d_array<float> capture256 [[texture(8)]],
    sampler repeatSampler [[sampler(0)]])
{
    const uint safeScale = max(scale, 1u);
    const float safeScaleF = float(safeScale);
    const uint2 outputCoord = uint2(in.position.xy);

    // Match OpenGL 2DCompositorVS/FS exactly. ScanlineConfig already stores
    // BGYPos + line (or the affine internal reference for that line), so the
    // fragment must pass only the within-scanline Y fraction to BG sampling.
    // The old code passed nativeCoord.y again, producing BGYPos+line+line.
    const float2 nativeTexcoord =
        in.position.xy / safeScaleF;
    int2 nativeCoord = int2(floor(nativeTexcoord));
    nativeCoord.x = clamp(nativeCoord.x, 0, 255);
    nativeCoord.y = clamp(nativeCoord.y, 0, 191);
    const uint line = uint(nativeCoord.y);
    constant ScanlineState& scan =
        scanlineConfig.scanline[line];
    const float2 bgSampleCoord = float2(
        nativeTexcoord.x,
        fract(nativeTexcoord.y));

    int mosaicX = 0;
    if (scan.mosaicSize.x > 0)
    {
        int mx = clamp(scan.mosaicSize.x, 0, 15);
        mosaicX = mosaicTexture.read(uint2(uint(nativeCoord.x), uint(mx))).r;
    }

    float4 layers[6];
    for (uint i = 0u; i < 4u; i++)
    {
        layers[i] = mp_calculate_bg(
            i, bgSampleCoord, outputCoord, line,
            layerConfig, scanlineConfig,
            bg0, bg1, bg2, bg3, high3D,
            capture128, capture256,
            repeatSampler, mosaicX);
    }

    uint2 objCoord = min(outputCoord, uint2(
        max(objLayer.get_width(), 1u) - 1u,
        max(objLayer.get_height(), 1u) - 1u));
    layers[4] = objLayer.read(objCoord, 0u);
    layers[5] = objLayer.read(objCoord, 1u);
    int4 objFlags = int4(layers[5] * 255.0);

    int4 color1 = int4(mp_convert_color(scan.backColor), 0x20);
    int mask1 = 0x20;
    int4 color2 = int4(0);
    int mask2 = 0;
    bool specialCase = false;

    int windowMask = int(scan.winMask);
    bool insideWindow0;
    bool insideWindow1;
    int x = nativeCoord.x;

    if (x < scan.winPos.x) insideWindow0 = (windowMask & (1 << 0)) != 0;
    else if (x < scan.winPos.y) insideWindow0 = (windowMask & (1 << 1)) != 0;
    else insideWindow0 = (windowMask & (1 << 2)) != 0;

    if (x < scan.winPos.z) insideWindow1 = (windowMask & (1 << 3)) != 0;
    else if (x < scan.winPos.w) insideWindow1 = (windowMask & (1 << 4)) != 0;
    else insideWindow1 = (windowMask & (1 << 5)) != 0;

    uint windowSelect = scan.winRegs;
    if (objFlags.b > 0) windowSelect = scan.winRegs >> 8u;
    if (insideWindow1) windowSelect = scan.winRegs >> 16u;
    if (insideWindow0) windowSelect = scan.winRegs >> 24u;

    for (int priority = 3; priority >= 0; priority--)
    {
        for (int bg = 3; bg >= 0; bg--)
        {
            if (int(scan.bgPrio[bg]) == priority &&
                layers[bg].a > 0.0 &&
                (windowSelect & (1u << uint(bg))) != 0u)
            {
                color2 = color1;
                mask2 = mask1 << 8;
                color1 = int4(layers[bg] * 255.0) >> int4(2, 2, 2, 3);
                mask1 = 1 << bg;
                specialCase = bg == 0 && scan.enable3D != 0u;
            }
        }

        if (scan.enableOBJ != 0u &&
            objFlags.a == priority &&
            layers[4].a > 0.0 &&
            (windowSelect & (1u << 4u)) != 0u)
        {
            color2 = color1;
            mask2 = mask1 << 8;
            color1 = int4(layers[4] * 255.0) >> int4(2, 2, 2, 3);
            mask1 = 1 << 4;
            specialCase = objFlags.r != 0;
        }
    }

    int effect = 0;
    int eva = int(scan.blendCoef.x);
    int evb = int(scan.blendCoef.y);
    int evy = int(scan.blendCoef.z);

    if (specialCase && (int(scan.blendCnt) & mask2) != 0)
    {
        if (mask1 == (1 << 0))
        {
            effect = 4;
            eva = (color1.a & 0x1F) + 1;
            evb = 32 - eva;
        }
        else if (objFlags.r == 1)
        {
            effect = 1;
        }
        else
        {
            effect = 1;
            eva = color1.a;
            evb = 16 - eva;
        }
    }
    else if ((int(scan.blendCnt) & mask1) != 0 &&
             (windowSelect & (1u << 5u)) != 0u)
    {
        effect = int(scan.blendEffect);
        if (effect == 1 && (int(scan.blendCnt) & mask2) == 0)
            effect = 0;
    }

    if (effect == 1)
    {
        color1 = ((color1 * eva) + (color2 * evb) + 0x8) >> 4;
        color1 = min(color1, int4(0x3F));
    }
    else if (effect == 2)
    {
        color1 += (((int4(0x3F) - color1) * evy) + 0x8) >> 4;
    }
    else if (effect == 3)
    {
        color1 -= ((color1 * evy) + 0x7) >> 4;
    }
    else if (effect == 4)
    {
        color1 = ((color1 * eva) + (color2 * evb) + 0x10) >> 5;
    }

    int3 rgb = clamp(color1.rgb << 2, int3(0), int3(255));
    return float4(float3(rgb) / 255.0, 1.0);
}
)MSL";
