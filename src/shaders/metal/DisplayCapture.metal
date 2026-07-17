// MelonPrimeDS -- Metal shader. Extracted from src/GPU_MetalCaptureMethods.inc (kMetalDisplayCaptureShaderSource) by
// PR-14 (MSL asset/metallib). Content is unchanged from the embedded
// R"MSL(...)MSL" literal; only the physical location moved so it can
// be compiled ahead-of-time into melonPrimeDS.metallib.
#include <metal_stdlib>
using namespace metal;

// MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1
// Canonical capture authority is DS-native RGB5551 in R16Uint textures.
// Scale-expanded RGBA8 arrays are no longer the storage format.

struct CaptureLineConfig
{
    uint enabled;
    uint dispCntA;
    uint captureCnt;
    int srcAXOffset;
    int srcBCaptureLayer;
    uint srcBCaptureSize;
    uint srcBNativeY;
    uint pad0;
};

struct CaptureDispatchConfig
{
    uint scale;
    uint lineOffset;
    uint2 pad0;
};

static inline float4 mp_unpack_rgba8(uint packed)
{
    return float4(
        float(packed & 0xFFu),
        float((packed >> 8u) & 0xFFu),
        float((packed >> 16u) & 0xFFu),
        float((packed >> 24u) & 0xFFu)) / 255.0;
}

static inline float4 mp_unpack_rgb5551(ushort value)
{
    uint r5 = value & 0x1Fu;
    uint g5 = (value >> 5u) & 0x1Fu;
    uint b5 = (value >> 10u) & 0x1Fu;
    uint a1 = (value >> 15u) & 1u;
    float r = float((r5 << 3u) | (r5 >> 2u)) / 255.0;
    float g = float((g5 << 3u) | (g5 >> 2u)) / 255.0;
    float b = float((b5 << 3u) | (b5 >> 2u)) / 255.0;
    return float4(r, g, b, float(a1));
}

static inline ushort mp_pack_rgb5551_components(uint3 rgb5, uint a1)
{
    return ushort(
        (rgb5.x & 0x1Fu) |
        ((rgb5.y & 0x1Fu) << 5u) |
        ((rgb5.z & 0x1Fu) << 10u) |
        ((a1 & 1u) << 15u));
}

static inline uint3 mp_float_to_rgb5(float4 color)
{
    uint4 a8 = uint4(clamp(color, 0.0, 1.0) * 255.0);
    return a8.rgb >> 3u;
}

static inline uint mp_float_to_a1(float4 color)
{
    return uint4(clamp(color, 0.0, 1.0) * 255.0).a > 0u ? 1u : 0u;
}

static inline float4 mp_capture_source_b(
    constant CaptureLineConfig& lineConfig,
    uint nativeX,
    device const uint* auxVram,
    device const uint* auxFifo,
    texture2d_array<ushort, access::read> snapshot128,
    texture2d_array<ushort, access::read> snapshot256)
{
    const bool fifo = (lineConfig.captureCnt & (1u << 25u)) != 0u;
    if (fifo)
        return mp_unpack_rgba8(auxFifo[(lineConfig.srcBNativeY * 256u) + nativeX]);

    if (lineConfig.srcBCaptureLayer < 0)
        return mp_unpack_rgba8(auxVram[(lineConfig.srcBNativeY * 256u) + nativeX]);

    uint layer = uint(lineConfig.srcBCaptureLayer);
    if (lineConfig.srcBCaptureSize == 0u)
    {
        uint startBlock = layer & 3u;
        uint blockBaseY = startBlock * 64u;
        uint relativeY = (lineConfig.srcBNativeY + 256u - blockBaseY) & 0xFFu;
        uint linearIndex = relativeY * 256u + nativeX;
        uint capX = linearIndex & 0x7Fu;
        uint capY = linearIndex >> 7u;
        if (capY >= 128u)
            return float4(0.0);

        uint2 coord = min(uint2(capX, capY), uint2(
            max(snapshot128.get_width(), 1u) - 1u,
            max(snapshot128.get_height(), 1u) - 1u));
        return mp_unpack_rgb5551(snapshot128.read(coord, layer).r);
    }

    uint2 coord = min(uint2(nativeX, lineConfig.srcBNativeY), uint2(
        max(snapshot256.get_width(), 1u) - 1u,
        max(snapshot256.get_height(), 1u) - 1u));
    return mp_unpack_rgb5551(snapshot256.read(coord, layer >> 2u).r);
}

kernel void mp_metal_display_capture(
    constant CaptureLineConfig* lines [[buffer(0)]],
    constant CaptureDispatchConfig& dispatchConfig [[buffer(1)]],
    device const uint* auxVram [[buffer(2)]],
    device const uint* auxFifo [[buffer(3)]],
    texture2d<float, access::read> engineA2D [[texture(0)]],
    texture2d<float, access::read> engineA3D [[texture(1)]],
    texture2d_array<ushort, access::read> snapshot128 [[texture(2)]],
    texture2d_array<ushort, access::read> snapshot256 [[texture(3)]],
    texture2d_array<ushort, access::write> capture128 [[texture(4)]],
    texture2d_array<ushort, access::write> capture256 [[texture(5)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint scale = max(dispatchConfig.scale, 1u);
    uint nativeX = gid.x;
    uint nativeLine = gid.y + dispatchConfig.lineOffset;
    if (nativeX >= 256u || nativeLine >= 192u)
        return;

    constant CaptureLineConfig& config = lines[nativeLine];
    if (config.enabled == 0u)
        return;

    uint captureSize = (config.captureCnt >> 20u) & 3u;
    uint dstWidth = captureSize == 0u ? 128u : 256u;
    uint dstHeight = captureSize == 0u ? 128u : 64u * captureSize;
    if (nativeX >= dstWidth || nativeLine >= dstHeight)
        return;

    uint srcA = (config.captureCnt >> 24u) & 1u;
    float4 colorA = float4(0.0);
    if (srcA == 0u)
    {
        uint2 coord = uint2(
            nativeX * scale + scale / 2u,
            nativeLine * scale + scale / 2u);
        coord = min(coord, uint2(
            max(engineA2D.get_width(), 1u) - 1u,
            max(engineA2D.get_height(), 1u) - 1u));
        colorA = engineA2D.read(coord);
    }
    else
    {
        int sourceX = int(nativeX) + config.srcAXOffset;
        if (sourceX >= 0 && sourceX < 256)
        {
            uint2 coord = uint2(
                uint(sourceX) * scale + scale / 2u,
                nativeLine * scale + scale / 2u);
            coord = min(coord, uint2(
                max(engineA3D.get_width(), 1u) - 1u,
                max(engineA3D.get_height(), 1u) - 1u));
            colorA = engineA3D.read(coord);
        }
    }

    float4 colorB = mp_capture_source_b(
        config, nativeX, auxVram, auxFifo, snapshot128, snapshot256);

    uint dstMode = (config.captureCnt >> 29u) & 3u;
    ushort output;
    if (dstMode == 0u)
    {
        output = mp_pack_rgb5551_components(
            mp_float_to_rgb5(colorA), mp_float_to_a1(colorA));
    }
    else if (dstMode == 1u)
    {
        output = mp_pack_rgb5551_components(
            mp_float_to_rgb5(colorB), mp_float_to_a1(colorB));
    }
    else
    {
        uint eva = min(config.captureCnt & 0x1Fu, 16u);
        uint evb = min((config.captureCnt >> 8u) & 0x1Fu, 16u);
        uint3 a5 = mp_float_to_rgb5(colorA);
        uint3 b5 = mp_float_to_rgb5(colorB);
        uint alphaA = mp_float_to_a1(colorA);
        uint alphaB = mp_float_to_a1(colorB);
        uint3 out5 = min(
            ((a5 * alphaA * eva) + (b5 * alphaB * evb) + 8u) >> 4u,
            uint3(31u));
        uint outAlpha =
            ((eva > 0u) ? alphaA : 0u) |
            ((evb > 0u) ? alphaB : 0u);
        output = mp_pack_rgb5551_components(out5, outAlpha);
    }

    ushort4 packed = ushort4(output, 0u, 0u, 0u);
    uint dstBank = (config.captureCnt >> 16u) & 3u;
    uint dstOffset = (config.captureCnt >> 18u) & 3u;
    if (captureSize == 0u)
    {
        uint layer = (dstBank << 2u) | dstOffset;
        capture128.write(packed, uint2(nativeX, nativeLine), layer);
    }
    else
    {
        uint dstNativeY = (dstOffset * 64u + nativeLine) & 0xFFu;
        capture256.write(packed, uint2(nativeX, dstNativeY), dstBank);
    }
}
