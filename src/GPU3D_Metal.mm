// MelonPrimeDS - experimental Metal 3D renderer scaffold (Metal-plan Phase 8)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_Metal.h"
#include "GPU3D_Texcache.h"

#include <chrono>
#include <array>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

namespace melonDS
{

namespace {

using MetalPerfClock = std::chrono::steady_clock;

struct MetalPerfFrame
{
    double FrameMs = 0.0;
    double Native3DMs = 0.0;
    double TexcacheMs = 0.0;
    double WaitMs = 0.0;
    uint64_t UploadBytes = 0;
    uint32_t Groups = 0;
    uint32_t Draws = 0;
    uint32_t ConsideredPolygons = 0;
    uint32_t TexturedPolygons = 0;
    uint32_t Scale = 1;
    uint32_t TargetWidth = 0;
    uint32_t TargetHeight = 0;
    bool CpuRendererFallback = false;
};

struct MetalPerfAccumulator
{
    uint32_t Frames = 0;
    double FrameMs = 0.0;
    double Native3DMs = 0.0;
    double TexcacheMs = 0.0;
    double WaitMs = 0.0;
    uint64_t UploadBytes = 0;
    uint64_t Groups = 0;
    uint64_t Draws = 0;
    uint64_t ConsideredPolygons = 0;
    uint64_t TexturedPolygons = 0;
    uint32_t LastScale = 1;
    uint32_t LastTargetWidth = 0;
    uint32_t LastTargetHeight = 0;
    uint32_t SoftwareDelegateFrames = 0;
};

MetalPerfFrame* gCurrentMetalPerfFrame = nullptr;

bool MetalPerfEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_PERF");
        return env && env[0] == '1';
    }();
    return enabled;
}

bool MetalDiagEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_DIAG");
        return env && env[0] == '1';
    }();
    return enabled;
}

bool MetalDiagSolidNative3DEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_DIAG_SOLID_NATIVE3D");
        return env && env[0] == '1';
    }();
    return enabled;
}

bool MetalUseNativeGetLine()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_GETLINE_SOURCE");
        return !(env && std::strcmp(env, "soft") == 0);
    }();
    return enabled;
}

bool MetalGetLineDiffEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_GETLINE_DIFF");
        return env && env[0] == '1';
    }();
    return enabled;
}

double MetalPerfElapsedMs(MetalPerfClock::time_point start, MetalPerfClock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

void MetalPerfAddWait(MetalPerfClock::time_point start, MetalPerfClock::time_point end)
{
    if (gCurrentMetalPerfFrame)
        gCurrentMetalPerfFrame->WaitMs += MetalPerfElapsedMs(start, end);
}

void MetalPerfSubmitFrame(const MetalPerfFrame& frame)
{
    if (!MetalPerfEnabled())
        return;

    static MetalPerfAccumulator acc;
    acc.Frames++;
    acc.FrameMs += frame.FrameMs;
    acc.Native3DMs += frame.Native3DMs;
    acc.TexcacheMs += frame.TexcacheMs;
    acc.WaitMs += frame.WaitMs;
    acc.UploadBytes += frame.UploadBytes;
    acc.Groups += frame.Groups;
    acc.Draws += frame.Draws;
    acc.ConsideredPolygons += frame.ConsideredPolygons;
    acc.TexturedPolygons += frame.TexturedPolygons;
    acc.LastScale = frame.Scale;
    acc.LastTargetWidth = frame.TargetWidth;
    acc.LastTargetHeight = frame.TargetHeight;
    if (frame.CpuRendererFallback)
        acc.SoftwareDelegateFrames++;

    constexpr uint32_t kReportFrames = 600;
    if (acc.Frames < kReportFrames)
        return;

    const double frames = static_cast<double>(acc.Frames);
    std::fprintf(stderr,
        "[MelonPrime] metal renderer3D: perf frames=%u scale=%u target=%ux%u "
        "avgFrameMs=%.3f native3dMs=%.3f avgTexcacheMs=%.3f "
        "uploadBytes=%.0f avgGroups=%.2f avgDraws=%.2f avgWaitMs=%.3f "
        "avgConsideredPolys=%.2f avgTexturedPolys=%.2f softwareDelegate=%u/%u\n",
        acc.Frames,
        acc.LastScale,
        acc.LastTargetWidth,
        acc.LastTargetHeight,
        acc.FrameMs / frames,
        acc.Native3DMs / frames,
        acc.TexcacheMs / frames,
        static_cast<double>(acc.UploadBytes) / frames,
        static_cast<double>(acc.Groups) / frames,
        static_cast<double>(acc.Draws) / frames,
        acc.WaitMs / frames,
        static_cast<double>(acc.ConsideredPolygons) / frames,
        static_cast<double>(acc.TexturedPolygons) / frames,
        acc.SoftwareDelegateFrames,
        acc.Frames);

    acc = {};
}

void MetalPerfLogTargetResize(int scale, NSUInteger width, NSUInteger height)
{
    static int lastScale = 0;
    static NSUInteger lastWidth = 0;
    static NSUInteger lastHeight = 0;
    if (scale == lastScale && width == lastWidth && height == lastHeight)
        return;

    lastScale = scale;
    lastWidth = width;
    lastHeight = height;
    std::fprintf(stderr,
        "[MelonPrime] metal renderer3D: native 3D source scale=%d size=%zux%zu\n",
        scale,
        static_cast<size_t>(width),
        static_cast<size_t>(height));
}

struct MetalTextureReadbackSummary
{
    uint64_t NonzeroPixels = 0;
    uint64_t Checksum = 1469598103934665603ull;
    int FirstNonzeroX = -1;
    int FirstNonzeroY = -1;
    uint8_t FirstNonzeroBGRA[4] = {};
    bool Valid = false;
};

MetalTextureReadbackSummary ReadbackBGRA8Texture(id<MTLCommandQueue> queue, id<MTLTexture> texture, NSUInteger slice)
{
    MetalTextureReadbackSummary summary;
    if (!queue || !texture)
        return summary;

    const NSUInteger width = texture.width;
    const NSUInteger height = texture.height;
    const NSUInteger bytesPerRow = width * 4;
    id<MTLDevice> device = texture.device;
    id<MTLBuffer> buffer = [device newBufferWithLength:bytesPerRow * height
                                               options:MTLResourceStorageModeShared];
    id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];
    if (!buffer || !commandBuffer)
        return summary;

    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    if (!blit)
        return summary;

    [blit copyFromTexture:texture
              sourceSlice:slice
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                 toBuffer:buffer
        destinationOffset:0
   destinationBytesPerRow:bytesPerRow
 destinationBytesPerImage:bytesPerRow * height];
    [blit endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted)
        return summary;

    const uint8_t* pixels = static_cast<const uint8_t*>([buffer contents]);
    constexpr uint64_t kFnvPrime = 1099511628211ull;
    for (NSUInteger y = 0; y < height; y++)
    {
        const uint8_t* row = pixels + y * bytesPerRow;
        for (NSUInteger x = 0; x < width; x++)
        {
            const uint8_t* px = row + x * 4;
            for (int c = 0; c < 4; c++)
            {
                summary.Checksum ^= px[c];
                summary.Checksum *= kFnvPrime;
            }
            if (px[0] || px[1] || px[2])
            {
                summary.NonzeroPixels++;
                if (summary.FirstNonzeroX < 0)
                {
                    summary.FirstNonzeroX = static_cast<int>(x);
                    summary.FirstNonzeroY = static_cast<int>(y);
                    for (int c = 0; c < 4; c++)
                        summary.FirstNonzeroBGRA[c] = px[c];
                }
            }
        }
    }
    summary.Valid = true;
    return summary;
}

} // namespace

// Phase 8 texturing (design doc S14 step 4). This loader is the Metal
// equivalent of TexcacheOpenGLLoader (GPU3D_TexcacheOpenGL.h/.cpp): the
// Texcache<> template (GPU3D_Texcache.h) owns 100% of the DS texture
// format decode logic (direct color, 4x4-compressed, A3I5/A5I3/A5I3,
// 2/4/8-bit paletted -- ConvertBitmapTexture/ConvertCompressedTexture/
// ConvertAXIYTexture/ConvertNColorsTexture, all backend-agnostic C++
// already compiled into `core` unconditionally) and only calls out to
// this loader's 3 methods for actual GPU resource management. Decoded
// texels arrive as RGB6A5 packed into 4 raw bytes/texel (outputFmt_RGB6A5
// -- see GPU3D_Texcache.h), matching exactly what TexcacheOpenGLLoader
// uploads as GL_RGBA8UI; the Metal equivalent is MTLPixelFormatRGBA8Uint,
// sampled in the shader as texture2d_array<uint> and normalized by
// /255,255,255,31 (5-bit alpha, not 8) to match 3DRenderFS.glsl.
class TexcacheMetalLoader
{
public:
    explicit TexcacheMetalLoader(id<MTLDevice> device) noexcept : Device(device) {}

    id<MTLTexture> GenerateTexture(u32 width, u32 height, u32 layers)
    {
        // metal_phase8_execution_instructions.md Priority 2: one-shot proof
        // that an integrated ROM run actually reaches native texture-array
        // allocation (as distinct from the standalone-harness verification
        // in §3k.1, which only ever allocated a synthetic array directly).
        static bool loggedFirst = false;
        if (!loggedFirst)
        {
            loggedFirst = true;
            std::fprintf(stderr,
                "[MelonPrime] metal texcache: first texture array allocation width=%u height=%u layers=%u\n",
                width, height, layers);
        }

        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        desc.textureType = MTLTextureType2DArray;
        desc.arrayLength = layers;
        desc.usage = MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModeShared;
        return [Device newTextureWithDescriptor:desc];
    }

    void UploadTexture(id<MTLTexture> handle, u32 width, u32 height, u32 layer, void* data)
    {
        if (!handle)
            return;

        static bool loggedFirst = false;
        if (!loggedFirst)
        {
            loggedFirst = true;
            std::fprintf(stderr,
                "[MelonPrime] metal texcache: first texture upload width=%u height=%u layer=%u\n",
                width, height, layer);
        }

        [handle replaceRegion:MTLRegionMake2D(0, 0, width, height)
                   mipmapLevel:0
                         slice:layer
                     withBytes:data
                   bytesPerRow:width * 4
                 bytesPerImage:width * height * 4];
    }

    void DeleteTexture(id<MTLTexture> /*handle*/)
    {
        // ARC releases the strong id<MTLTexture> ref held by Texcache<>'s
        // own TexArrays<>/FreeTextures<> vectors when they are cleared/
        // erased; there is no explicit destroy call in the Metal API.
    }

private:
    id<MTLDevice> Device = nil;
};

using TexcacheMetal = Texcache<TexcacheMetalLoader, id<MTLTexture>>;

struct MetalRenderer3D::MetalState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> CommandQueue = nil;
    id<MTLLibrary> ShaderLibrary = nil;
    id<MTLRenderPipelineState> ClearPipeline = nil;
    id<MTLDepthStencilState> ClearDepthStencil = nil;
    id<MTLTexture> ClearBitmapColor = nil;
    id<MTLTexture> ClearBitmapDepth = nil;
    std::array<u32, 256 * 256> ClearBitmapColorData {};
    std::array<u32, 256 * 256> ClearBitmapDepthData {};
    u8 ClearBitmapDirty = 0x3;
    id<MTLTexture> ColorTarget = nil;
    id<MTLTexture> DepthStencilTarget = nil;
    id<MTLTexture> AttrTarget = nil;
    id<MTLBuffer> NativeReadbackBuffer = nil;
    std::array<u32, 256 * 192> NativeLineBuffer {};
    u32 NativeScrolledLine[256] = {};
    bool NativeLineReady = false;

    // Phase 8 opaque-polygon pass (see RenderNativeOpaquePolygons()).
    id<MTLLibrary> OpaqueShaderLibrary = nil;
    id<MTLRenderPipelineState> OpaqueRenderPipelineZ = nil;       // standard depth-buffer polygons
    id<MTLRenderPipelineState> OpaqueRenderPipelineW = nil;       // DS W-buffered polygons
    id<MTLRenderPipelineState> TranslucentRenderPipelineZ = nil;  // blended depth-buffer polygons
    id<MTLRenderPipelineState> TranslucentRenderPipelineW = nil;  // blended W-buffer polygons
    id<MTLDepthStencilState> OpaqueDepthLessWrite = nil;
    id<MTLDepthStencilState> OpaqueDepthLessEqualWrite = nil;
    id<MTLDepthStencilState> TranslucentDepthLessWrite = nil;
    id<MTLDepthStencilState> TranslucentDepthLessEqualWrite = nil;
    id<MTLDepthStencilState> TranslucentDepthLessNoWrite = nil;
    id<MTLDepthStencilState> TranslucentDepthLessEqualNoWrite = nil;
    id<MTLLibrary> FinalPassShaderLibrary = nil;
    id<MTLRenderPipelineState> EdgePipeline = nil;
    id<MTLRenderPipelineState> FogColorPipeline = nil;
    id<MTLRenderPipelineState> FogAlphaPipeline = nil;
    // 3x3 matrix of [S mode][T mode], each axis independently one of
    // {clamp, repeat, mirror-repeat} -- matches the DS TexRepeat bits via
    // TexRepeatAddressModeIndex() (see below). Built once in
    // BuildOpaqueTextureSamplers(), indexed per draw group in
    // RenderNativeOpaquePolygons() since different polygons batched into
    // different groups can have different wrap modes (see
    // OpaqueDrawGroupKey::TexRepeat).
    id<MTLSamplerState> OpaqueTextureSamplers[3][3] = {};
    id<MTLTexture> DummyTexture = nil; // bound for untextured draws (static arg slot)
    std::unique_ptr<TexcacheMetal> Texcache; // constructed once Device exists
    Metal3DDiagnostics LastDiagnostics;
    bool LoggedNativeZeroAfterDraw = false;
};

// Maps one axis of the DS TexRepeat 4-bit field to an index into
// MetalState::OpaqueTextureSamplers, matching
// GLRenderer3D::SetupPolygonTexture()'s GL_REPEAT/GL_MIRRORED_REPEAT/
// GL_CLAMP_TO_EDGE derivation (GPU3D_OpenGL.cpp) exactly: index 0 = clamp
// (repeat bit clear), 1 = repeat (repeat bit set, mirror bit clear),
// 2 = mirror-repeat (both bits set).
static inline int TexRepeatAddressModeIndex(uint32_t texRepeat, int repeatBit, int mirrorBit)
{
    if (!((texRepeat >> repeatBit) & 1u))
        return 0; // clamp
    return ((texRepeat >> mirrorBit) & 1u) ? 2 : 1; // mirror-repeat : repeat
}

static constexpr const char* kMetal3DShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct ClearVertexOut
{
    float4 position [[position]];
    float2 texcoord;
};

vertex ClearVertexOut mp3d_clear_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };

    ClearVertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    out.texcoord = (positions[vertexID] + 1.0) * float2(0.5, 0.375);
    return out;
}

struct ClearBitmapConfig
{
    float2 offset;
    uint opaquePolyID;
    uint _pad;
};

struct ClearFragmentOut
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
    float depth [[depth(any)]];
};

fragment ClearFragmentOut mp3d_clear_fs(
    ClearVertexOut in [[stage_in]],
    constant ClearBitmapConfig& config [[buffer(0)]],
    texture2d<uint> colorTex [[texture(0)]],
    texture2d<uint> depthTex [[texture(1)]])
{
    uint2 coord = uint2(fract(in.texcoord + config.offset) * float2(256.0, 256.0));
    uint4 color = colorTex.read(coord);
    uint depth = depthTex.read(coord).r;

    ClearFragmentOut out;
    out.color = float4(color) / float4(63.0, 63.0, 63.0, 31.0);
    out.attr = float4(float(config.opaquePolyID) / 63.0,
                      0.0,
                      float(depth >> 24),
                      1.0);
    out.depth = float(depth & 0xFFFFFFu) / 16777216.0;
    return out;
}
)";

// Opaque-polygon pass (Phase 8 port-order steps 3-4, design doc S14).
// Vertex data is "pulled" from a raw packed uint buffer rather than bound
// via a MTLVertexDescriptor -- 7 words per vertex, matching the exact
// CPU-side packing GLRenderer3D::SetupVertex() writes (GPU3D_OpenGL.cpp)
// so the interpretation below can be checked word-for-word against that
// reference implementation:
//   word0 = x | (y << 16)                      -- screen-space position (native res)
//   word1 = z | (w << 16)                       -- 16-bit shifted Z, 16-bit W
//   word2 = (r>>1) | (g>>1)<<8 | (b>>1)<<16 | alpha<<24
//   word3 = texcoord.s (s16) | texcoord.t (s16) << 16  -- raw 4-bit-fixed DS texcoords
//   word4 = (polyAttr & 0x30, blend mode bits 4-5) | (zshift << 16)
//   word5 = texlayer index from Texcache<>::GetTexture() (0-63), or the
//           0xFFFF sentinel meaning "no texture" (see RenderNativeOpaquePolygons());
//           GL's variant also ORs a 0xFFFF0000 tag for VRAM-display-capture-
//           as-texture addressing in the upper 16 bits -- this pass does not
//           support that DS feature, so only the low 16 bits are ever meaningful
//   word6 = texwidth | (texheight << 16)        -- for texcoord normalization
//
// The position/depth math mirrors 3DRenderVS.glsl exactly, with one
// necessary change: GL's NDC z range is [-1,1] and a separate depth-range
// remap (glDepthRange(0,1)) folds that to [0,1] before the depth test.
// Metal has no such remap -- its NDC z *is* device depth [0,1] -- so the
// remap is folded into the shader itself: (z<<zshift)/8388608.0 - 1.0 (GL)
// becomes (z<<zshift)/16777216.0 (Metal), which is exactly the same value
// GL's own W-buffer path already computes for `fZ` (3DRenderVS.glsl line
// 36), since (X/8388608 - 1 + 1)/2 == X/16777216.
//
// Texturing mirrors 3DRenderFS.glsl's FinalColor(): every DS texture format
// is decoded by the shared Texcache<> template (GPU3D_Texcache.h, the same
// code GLRenderer3D uses) into a common RGB6A5-packed-as-4-raw-bytes
// representation before it ever reaches this shader, so there is no
// per-format branching here -- modulate/decal and the blendmode==2
// toon/highlight color substitution use the per-frame RenderDispCnt and
// toon table passed through OpaqueRenderConfig. Texture wrapping/mirroring
// (TexRepeat bits)
// is implemented at the sampler-state level (BuildOpaqueRenderPipelines()
// builds one MTLSamplerState per (S,T) address-mode combination,
// RenderNativeOpaquePolygons() selects the one matching each draw group's
// TexRepeat bits -- see TexRepeatAddressModeIndex()), matching
// GLRenderer3D::SetupPolygonTexture()'s GL_REPEAT/GL_MIRRORED_REPEAT/
// GL_CLAMP_TO_EDGE derivation.
static constexpr const char* kMetal3DOpaqueShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct OpaqueRenderConfig
{
    float2 screenSize;
    uint dispCnt;
    uint _pad;
    float4 toonTable[32];
};

struct OpaqueVertexOut
{
    float4 position [[position]];
    float4 color;
    float2 texcoord;
    float fz;                 // perspective-correct-interpolated W-buffer depth
    int texLayer [[flat]];    // 0xFFFF sentinel = no texture
    int blendMode [[flat]];   // (polyAttr >> 4) & 0x3
    uint polygonAttr [[flat]];
};

static inline OpaqueVertexOut BuildOpaqueVertex(
    uint vid,
    constant uint* words,
    constant OpaqueRenderConfig& config,
    bool wbuffer)
{
    uint base = vid * 7u;
    uint w0 = words[base + 0];
    uint w1 = words[base + 1];
    uint w2 = words[base + 2];
    uint w3 = words[base + 3];
    uint w4 = words[base + 4];
    uint w5 = words[base + 5];
    uint w6 = words[base + 6];

    uint x = w0 & 0xFFFFu;
    uint y = (w0 >> 16) & 0xFFFFu;
    uint z = w1 & 0xFFFFu;
    uint w = (w1 >> 16) & 0xFFFFu;
    uint zshift = (w4 >> 16) & 0x1Fu;

    float4 pos;
    pos.xy = ((float2(float(x), float(y)) * 2.0) / config.screenSize) - 1.0;
    float zndc = float(z << zshift) / 16777216.0;
    pos.z = wbuffer ? 0.0 : zndc;
    pos.w = float(w) / 65536.0;
    pos.xyz *= pos.w;

    float r = float((w2 >> 0) & 0xFFu);
    float g = float((w2 >> 8) & 0xFFu);
    float b = float((w2 >> 16) & 0xFFu);
    float a = float((w2 >> 24) & 0xFFu);

    // DS texcoords are signed 4-bit-fixed (s16 raw units); sign-extend
    // each packed 16-bit half before converting to the fixed-point value.
    short rawS = short(w3 & 0xFFFFu);
    short rawT = short((w3 >> 16) & 0xFFFFu);
    uint texWidth = w6 & 0xFFFFu;
    uint texHeight = (w6 >> 16) & 0xFFFFu;
    float2 texFactor = 1.0 / (16.0 * float2(float(max(texWidth, 1u)), float(max(texHeight, 1u))));

    OpaqueVertexOut out;
    out.position = pos;
    out.color = float4(r, g, b, a) / float4(255.0, 255.0, 255.0, 31.0);
    out.texcoord = float2(float(rawS), float(rawT)) * texFactor;
    out.fz = zndc;
    out.texLayer = int(w5 & 0xFFFFu);
    out.blendMode = int((w4 >> 4) & 0x3u);
    out.polygonAttr = w4;
    return out;
}

vertex OpaqueVertexOut mp3d_opaque_vs_z(
    uint vid [[vertex_id]],
    constant uint* words [[buffer(0)]],
    constant OpaqueRenderConfig& config [[buffer(1)]])
{
    return BuildOpaqueVertex(vid, words, config, false);
}

vertex OpaqueVertexOut mp3d_opaque_vs_w(
    uint vid [[vertex_id]],
    constant uint* words [[buffer(0)]],
    constant OpaqueRenderConfig& config [[buffer(1)]])
{
    return BuildOpaqueVertex(vid, words, config, true);
}

static inline float4 OpaqueFinalColor(
    OpaqueVertexOut in [[stage_in]],
    texture2d_array<uint> tex,
    sampler smp,
    constant OpaqueRenderConfig& config)
{
    float4 vcol = in.color;
    float4 toon = config.toonTable[uint(clamp(vcol.r * 31.0, 0.0, 31.0))];
    bool toonOrHighlight = in.blendMode == 2;
    bool highlight = toonOrHighlight && ((config.dispCnt & (1u << 1)) != 0u);
    if (toonOrHighlight)
    {
        if (highlight)
            vcol.gb = vcol.rr;
        else
            vcol.rgb = toon.rgb;
    }

    float4 outColor;
    if (in.texLayer == 0xFFFF)
    {
        outColor = vcol;
    }
    else
    {
        uint4 raw = tex.sample(smp, in.texcoord, uint(in.texLayer));
        float4 tcol = float4(raw) / float4(63.0, 63.0, 63.0, 31.0);

        if ((in.blendMode & 1) != 0)
        {
            // decal
            float3 rgb = (tcol.rgb * tcol.a) + (vcol.rgb * (1.0 - tcol.a));
            outColor = float4(rgb, vcol.a);
        }
        else
        {
            // modulate
            outColor = vcol * tcol;
        }
    }

    if (highlight)
        outColor.rgb = min(outColor.rgb + toon.rgb, float3(1.0));
    return outColor;
}

struct OpaqueFragmentOut
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
};

// Bring-up visible geometry pass: this is still not DS translucent/shadow
// parity, but it deliberately draws non-shadow triangle polygons even when
// their alpha is below the fully-opaque threshold. Otherwise games whose
// visible scene is mostly translucent-class geometry look like a black native
// 3D target, which is useless for validating the Metal path.
fragment OpaqueFragmentOut mp3d_opaque_fs_z(
    OpaqueVertexOut in [[stage_in]],
    texture2d_array<uint> tex [[texture(0)]],
    sampler smp [[sampler(0)]],
    constant OpaqueRenderConfig& config [[buffer(1)]])
{
    float4 col = OpaqueFinalColor(in, tex, smp, config);
    if (col.a < (0.5 / 31.0))
        discard_fragment();

    OpaqueFragmentOut out;
    out.color = col;
    if ((in.polygonAttr & (1u << 31)) != 0u)
    {
        out.attr = float4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        out.attr = float4(float((in.polygonAttr >> 24) & 0x3Fu) / 63.0,
                          ((config.dispCnt & (1u << 5)) != 0u) ? 1.0 : 0.0,
                          float((in.polygonAttr >> 15) & 0x1u),
                          1.0);
    }
    return out;
}

struct OpaqueFragmentOutW
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
    float depth [[depth(any)]];
};

fragment OpaqueFragmentOutW mp3d_opaque_fs_w(
    OpaqueVertexOut in [[stage_in]],
    texture2d_array<uint> tex [[texture(0)]],
    sampler smp [[sampler(0)]],
    constant OpaqueRenderConfig& config [[buffer(1)]])
{
    float4 col = OpaqueFinalColor(in, tex, smp, config);
    if (col.a < (0.5 / 31.0))
        discard_fragment();

    OpaqueFragmentOutW out;
    out.color = col;
    if ((in.polygonAttr & (1u << 31)) != 0u)
    {
        out.attr = float4(0.0, 0.0, 0.0, 1.0);
    }
    else
    {
        out.attr = float4(float((in.polygonAttr >> 24) & 0x3Fu) / 63.0,
                          ((config.dispCnt & (1u << 5)) != 0u) ? 1.0 : 0.0,
                          float((in.polygonAttr >> 15) & 0x1u),
                          1.0);
    }
    out.depth = in.fz;
    return out;
}
)";

static constexpr const char* kMetal3DFinalPassShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct FinalVertexOut
{
    float4 position [[position]];
};

vertex FinalVertexOut mp3d_final_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };

    FinalVertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

struct FinalPassConfig
{
    uint dispCnt;
    uint fogOffset;
    uint fogShift;
    uint _pad;
    float4 edgeColors[8];
    float4 fogDensity[34];
};

static inline int2 ClampCoord(int2 coord)
{
    return int2(clamp(coord.x, 0, 255), clamp(coord.y, 0, 191));
}

static inline bool EdgeNeighborIsGood(
    texture2d<float> attrTex,
    depth2d<float> depthTex,
    int2 coord,
    int refPolyID,
    float refDepth)
{
    int2 clamped = ClampCoord(coord);
    uint2 ucoord = uint2(clamped);
    float4 attr = attrTex.read(ucoord);
    int polyID = int(attr.r * 63.0);
    float depth = depthTex.read(ucoord);
    return polyID != refPolyID && refDepth < depth;
}

fragment float4 mp3d_edge_fs(
    FinalVertexOut in [[stage_in]],
    constant FinalPassConfig& config [[buffer(0)]],
    depth2d<float> depthTex [[texture(0)]],
    texture2d<float> attrTex [[texture(1)]])
{
    int2 coord = int2(in.position.xy);
    uint2 ucoord = uint2(coord);
    float4 attr = attrTex.read(ucoord);
    if (attr.g == 0.0)
        return float4(0.0);

    float depth = depthTex.read(ucoord);
    int polyID = int(attr.r * 63.0);
    if (EdgeNeighborIsGood(attrTex, depthTex, coord + int2(0, -1), polyID, depth) ||
        EdgeNeighborIsGood(attrTex, depthTex, coord + int2(0,  1), polyID, depth) ||
        EdgeNeighborIsGood(attrTex, depthTex, coord + int2(-1, 0), polyID, depth) ||
        EdgeNeighborIsGood(attrTex, depthTex, coord + int2( 1, 0), polyID, depth))
    {
        float4 color = config.edgeColors[uint(polyID >> 3)];
        color.a = ((config.dispCnt & (1u << 4)) != 0u) ? 0.5 : 1.0;
        return color;
    }
    return float4(0.0);
}

fragment float4 mp3d_fog_fs(
    FinalVertexOut in [[stage_in]],
    constant FinalPassConfig& config [[buffer(0)]],
    depth2d<float> depthTex [[texture(0)]],
    texture2d<float> attrTex [[texture(1)]])
{
    uint2 coord = uint2(in.position.xy);
    float4 attr = attrTex.read(coord);
    if (attr.b == 0.0)
        return float4(0.0);

    float depth = depthTex.read(coord);
    uint idepth = uint(depth * 16777216.0);
    uint densityID;
    uint densityFrac;
    if (idepth < config.fogOffset)
    {
        densityID = 0;
        densityFrac = 0;
    }
    else
    {
        uint udepth = idepth - config.fogOffset;
        udepth = (udepth >> 2) << config.fogShift;
        densityID = udepth >> 17;
        if (densityID >= 32)
        {
            densityID = 32;
            densityFrac = 0;
        }
        else
        {
            densityFrac = udepth & 0x1FFFFu;
        }
    }

    float density = mix(config.fogDensity[densityID].x,
                        config.fogDensity[densityID + 1].x,
                        float(densityFrac) / 131072.0);
    return float4(density);
}
)";

MetalRenderer3D::MetalRenderer3D(melonDS::GPU3D& gpu3D, SoftRenderer& parent) noexcept
    : Renderer3D(gpu3D),
      Delegate(gpu3D, parent),
      State(std::make_unique<MetalState>())
{
}

MetalRenderer3D::~MetalRenderer3D() = default;

bool MetalRenderer3D::Init()
{
    if (!CreateDeviceObjects())
        return false;

    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.Reset();
    std::fprintf(stderr,
        "[MelonPrime] metal renderer3D: native Metal device/queue/targets initialized; getLineSource=%s diff=%u\n",
        MetalUseNativeGetLine() ? "native" : "soft",
        MetalGetLineDiffEnabled() ? 1u : 0u);
    return true;
}

void MetalRenderer3D::Reset()
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.Reset();
    if (State && State->Texcache)
        State->Texcache->Reset();
    if (State)
        State->NativeLineReady = false;
    if (State && State->Device)
        ClearNativeTarget();
}

void MetalRenderer3D::SetThreaded(bool threaded) noexcept
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.SetThreaded(threaded);
}

bool MetalRenderer3D::IsThreaded() const noexcept
{
    return Delegate.IsThreaded();
}

void MetalRenderer3D::SetScaleFactor(int scale) noexcept
{
    if (scale < 1)
        scale = 1;
    if (scale == ScaleFactor)
        return;

    ScaleFactor = scale;
    if (State && State->Device)
        ResizeTargets();
}

void MetalRenderer3D::RenderFrame()
{
    MetalPerfFrame perfFrame;
    MetalPerfFrame* previousPerfFrame = gCurrentMetalPerfFrame;
    const bool perfEnabled = MetalPerfEnabled();
    const auto frameStart = perfEnabled ? MetalPerfClock::now() : MetalPerfClock::time_point {};
    if (perfEnabled)
    {
        perfFrame.Scale = static_cast<uint32_t>(ScaleFactor);
        if (State && State->ColorTarget)
        {
            perfFrame.TargetWidth = static_cast<uint32_t>(State->ColorTarget.width);
            perfFrame.TargetHeight = static_cast<uint32_t>(State->ColorTarget.height);
        }
        gCurrentMetalPerfFrame = &perfFrame;
    }

    // metal_phase8_execution_instructions.md Priority 2: one-shot proof the
    // integrated frame loop (a real ROM running through EmuThread ->
    // updateRenderer() -> GPU3D::RenderFrame()) actually reaches this native
    // path, as distinct from the standalone-harness verification in §3j/3k
    // (which only ever exercised the shader/pipeline logic in isolation).
    static bool loggedFirstRenderFrame = false;
    if (!loggedFirstRenderFrame)
    {
        loggedFirstRenderFrame = true;
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: first integrated RenderFrame\n");
    }

    const bool needsSoftwareDelegate = !MetalUseNativeGetLine() || MetalGetLineDiffEnabled();
    if (needsSoftwareDelegate)
    {
        Delegate.RenderFrame();
        perfFrame.CpuRendererFallback = true;
    }

    // Native Metal 3D submission every frame. The 1x ColorTarget is read back
    // into SoftRenderer3D's scanline format and consumed by the existing soft
    // 2D compositor via GetLine().
    if (State && State->Device)
    {
        u8 clearBitmapDirty = 0;
        const auto texcacheStart = MetalPerfEnabled() ? MetalPerfClock::now() : MetalPerfClock::time_point {};
        if (State->Texcache)
            State->Texcache->Update(clearBitmapDirty);
        if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
            gCurrentMetalPerfFrame->TexcacheMs += MetalPerfElapsedMs(texcacheStart, MetalPerfClock::now());
        UpdateClearBitmapTextures(clearBitmapDirty);

        ClearNativeTarget();
        const auto nativeStart = perfEnabled ? MetalPerfClock::now() : MetalPerfClock::time_point {};
        if (MetalDiagSolidNative3DEnabled())
            DrawSolidNative3DDiagnostic();
        else
            RenderNativeOpaquePolygons();
        RenderFinalPostPass();
        ReadbackNativeColorTargetToLineBuffer();
        if (perfEnabled)
            perfFrame.Native3DMs += MetalPerfElapsedMs(nativeStart, MetalPerfClock::now());
    }

    if (perfEnabled)
    {
        perfFrame.FrameMs = MetalPerfElapsedMs(frameStart, MetalPerfClock::now());
        gCurrentMetalPerfFrame = previousPerfFrame;
        MetalPerfSubmitFrame(perfFrame);
    }
}

void MetalRenderer3D::FinishRendering()
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.FinishRendering();
}

void MetalRenderer3D::RestartFrame()
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.RestartFrame();
}

u32* MetalRenderer3D::GetLine(int line)
{
    const bool useNative = MetalUseNativeGetLine();
    u32* softLine = nullptr;
    if (!useNative || MetalGetLineDiffEnabled())
        softLine = Delegate.GetLine(line);

    if (!useNative)
        return softLine;

    if (!State || !State->NativeLineReady || GPU3D.AbortFrame)
    {
        static u32 zeroLine[256] = {};
        return zeroLine;
    }

    u32* rawline = &State->NativeLineBuffer[static_cast<size_t>(line) * 256u];

    if (MetalGetLineDiffEnabled() && softLine)
    {
        static uint64_t diffPixels = 0;
        static uint64_t totalPixels = 0;
        for (int x = 0; x < 256; x++)
        {
            if (rawline[x] != softLine[x])
                diffPixels++;
        }
        totalPixels += 256;
        if (line == 191)
        {
            static uint64_t frames = 0;
            frames++;
            if (frames <= 3 || (frames % 60) == 0)
            {
                std::fprintf(stderr,
                    "[MelonPrime] metal getline diff: frames=%llu diffPixels=%llu totalPixels=%llu\n",
                    static_cast<unsigned long long>(frames),
                    static_cast<unsigned long long>(diffPixels),
                    static_cast<unsigned long long>(totalPixels));
            }
            diffPixels = 0;
            totalPixels = 0;
        }
    }

    const u16 xpos = GPU3D.RenderXPos;
    if (xpos == 0)
        return rawline;

    if (xpos & 0x100)
    {
        int i = 0, j = xpos;
        for (; j < 512; i++, j++)
            State->NativeScrolledLine[i] = 0;
        for (j = 0; i < 256; i++, j++)
            State->NativeScrolledLine[i] = rawline[j];
    }
    else
    {
        int i = 0, j = xpos;
        for (; j < 256; i++, j++)
            State->NativeScrolledLine[i] = rawline[j];
        for (; i < 256; i++)
            State->NativeScrolledLine[i] = 0;
    }

    return State->NativeScrolledLine;
}

void* MetalRenderer3D::GetColorTargetTexture() const noexcept
{
    return State ? (__bridge void*)State->ColorTarget : nullptr;
}

int MetalRenderer3D::GetTargetWidth() const noexcept
{
    return State && State->ColorTarget ? static_cast<int>(State->ColorTarget.width) : 0;
}

int MetalRenderer3D::GetTargetHeight() const noexcept
{
    return State && State->ColorTarget ? static_cast<int>(State->ColorTarget.height) : 0;
}

int MetalRenderer3D::GetScaleFactor() const noexcept
{
    return ScaleFactor;
}

Metal3DDiagnostics MetalRenderer3D::GetLastDiagnostics() const noexcept
{
    return State ? State->LastDiagnostics : Metal3DDiagnostics {};
}

void MetalRenderer3D::SetupRenderThread()
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.SetupRenderThread();
}

void MetalRenderer3D::EnableRenderThread()
{
    if (!MetalUseNativeGetLine() || MetalGetLineDiffEnabled())
        Delegate.EnableRenderThread();
}

bool MetalRenderer3D::CreateDeviceObjects()
{
    State->Device = MTLCreateSystemDefaultDevice();
    if (!State->Device)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: no Metal device available\n");
        return false;
    }

    State->CommandQueue = [State->Device newCommandQueue];
    if (!State->CommandQueue)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create command queue\n");
        return false;
    }

    if (!BuildClearPipeline())
        return false;

    if (!CreateClearBitmapTextures())
        return false;

    if (!BuildOpaqueRenderPipelines())
        return false;

    if (!BuildFinalPassPipelines())
        return false;

    // Priority 4 (metal_phase8_execution_instructions.md): one sampler per
    // (S mode, T mode) combination, matching GLRenderer3D::
    // SetupPolygonTexture()'s GL_REPEAT/GL_MIRRORED_REPEAT/GL_CLAMP_TO_EDGE
    // derivation from the DS TexRepeat bits exactly (see
    // TexRepeatAddressModeIndex() above). This used to be a single
    // always-clamp sampler; every real DS TexRepeat combination is now
    // represented.
    static constexpr MTLSamplerAddressMode kAddressModesByIndex[3] = {
        MTLSamplerAddressModeClampToEdge,
        MTLSamplerAddressModeRepeat,
        MTLSamplerAddressModeMirrorRepeat,
    };
    for (int sIdx = 0; sIdx < 3; sIdx++)
    {
        for (int tIdx = 0; tIdx < 3; tIdx++)
        {
            MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
            samplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
            samplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
            samplerDesc.sAddressMode = kAddressModesByIndex[sIdx];
            samplerDesc.tAddressMode = kAddressModesByIndex[tIdx];
            State->OpaqueTextureSamplers[sIdx][tIdx] =
                [State->Device newSamplerStateWithDescriptor:samplerDesc];
            if (!State->OpaqueTextureSamplers[sIdx][tIdx])
            {
                std::fprintf(stderr,
                    "[MelonPrime] metal renderer3D: failed to create opaque texture sampler (s=%d t=%d)\n",
                    sIdx, tIdx);
                return false;
            }
        }
    }

    // A texture argument is bound statically per-draw in Metal even when
    // the shader's runtime branch never samples it (untextured polygons) --
    // this 1x1 array texture satisfies that requirement without needing a
    // separate untextured pipeline/shader variant.
    MTLTextureDescriptor* dummyDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                                           width:1
                                                          height:1
                                                       mipmapped:NO];
    dummyDesc.textureType = MTLTextureType2DArray;
    dummyDesc.arrayLength = 1;
    dummyDesc.usage = MTLTextureUsageShaderRead;
    dummyDesc.storageMode = MTLStorageModeShared;
    State->DummyTexture = [State->Device newTextureWithDescriptor:dummyDesc];
    if (!State->DummyTexture)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create dummy texture\n");
        return false;
    }

    State->Texcache = std::make_unique<TexcacheMetal>(GPU, TexcacheMetalLoader(State->Device));

    return ResizeTargets();
}

bool MetalRenderer3D::BuildClearPipeline()
{
    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetal3DShaderSource];
    State->ShaderLibrary = [State->Device newLibraryWithSource:source options:nil error:&error];
    if (!State->ShaderLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to compile clear shaders: %s\n", message);
        return false;
    }

    id<MTLFunction> vertex = [State->ShaderLibrary newFunctionWithName:@"mp3d_clear_vs"];
    id<MTLFunction> fragment = [State->ShaderLibrary newFunctionWithName:@"mp3d_clear_fs"];
    if (!vertex || !fragment)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: clear shader entry point missing\n");
        return false;
    }

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.label = @"MelonPrime Metal 3D Clear Pipeline";
    desc.vertexFunction = vertex;
    desc.fragmentFunction = fragment;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
    desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

    error = nil;
    State->ClearPipeline = [State->Device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!State->ClearPipeline)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create clear pipeline: %s\n", message);
        return false;
    }

    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
    dsDesc.depthWriteEnabled = YES;
    State->ClearDepthStencil = [State->Device newDepthStencilStateWithDescriptor:dsDesc];
    if (!State->ClearDepthStencil)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create clear depth/stencil state\n");
        return false;
    }

    return true;
}

bool MetalRenderer3D::BuildOpaqueRenderPipelines()
{
    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetal3DOpaqueShaderSource];
    State->OpaqueShaderLibrary = [State->Device newLibraryWithSource:source options:nil error:&error];
    if (!State->OpaqueShaderLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to compile opaque shaders: %s\n", message);
        return false;
    }

    id<MTLFunction> vsZ = [State->OpaqueShaderLibrary newFunctionWithName:@"mp3d_opaque_vs_z"];
    id<MTLFunction> vsW = [State->OpaqueShaderLibrary newFunctionWithName:@"mp3d_opaque_vs_w"];
    id<MTLFunction> fsZ = [State->OpaqueShaderLibrary newFunctionWithName:@"mp3d_opaque_fs_z"];
    id<MTLFunction> fsW = [State->OpaqueShaderLibrary newFunctionWithName:@"mp3d_opaque_fs_w"];
    if (!vsZ || !vsW || !fsZ || !fsW)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: opaque shader entry point missing\n");
        return false;
    }

    auto createPipeline = [&](NSString* label, id<MTLFunction> vertex, id<MTLFunction> fragment, bool blended) -> id<MTLRenderPipelineState> {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.label = label;
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fragment;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        if (blended)
        {
            desc.colorAttachments[0].blendingEnabled = YES;
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
            desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
            desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
            desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
            desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationMax;
        }
        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline =
            [State->Device newRenderPipelineStateWithDescriptor:desc error:&pipelineError];
        if (!pipeline)
        {
            const char* message = pipelineError ? [[pipelineError localizedDescription] UTF8String] : "unknown error";
            std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create %s: %s\n",
                [label UTF8String], message);
        }
        return pipeline;
    };

    State->OpaqueRenderPipelineZ = createPipeline(@"MelonPrime Metal 3D Opaque Pipeline (Z-buffer)", vsZ, fsZ, false);
    if (!State->OpaqueRenderPipelineZ)
    {
        return false;
    }

    State->OpaqueRenderPipelineW = createPipeline(@"MelonPrime Metal 3D Opaque Pipeline (W-buffer)", vsW, fsW, false);
    if (!State->OpaqueRenderPipelineW)
    {
        return false;
    }

    State->TranslucentRenderPipelineZ =
        createPipeline(@"MelonPrime Metal 3D Translucent Pipeline (Z-buffer)", vsZ, fsZ, true);
    State->TranslucentRenderPipelineW =
        createPipeline(@"MelonPrime Metal 3D Translucent Pipeline (W-buffer)", vsW, fsW, true);
    if (!State->TranslucentRenderPipelineZ || !State->TranslucentRenderPipelineW)
        return false;

    auto createStencilDepthState = [&](MTLCompareFunction depthCompare, BOOL depthWrite, bool translucent)
        -> id<MTLDepthStencilState> {
        MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
        dsDesc.depthCompareFunction = depthCompare;
        dsDesc.depthWriteEnabled = depthWrite;

        MTLStencilDescriptor* stencil = [[MTLStencilDescriptor alloc] init];
        stencil.stencilFailureOperation = MTLStencilOperationKeep;
        stencil.depthFailureOperation = MTLStencilOperationKeep;
        stencil.depthStencilPassOperation = MTLStencilOperationReplace;
        if (translucent)
        {
            stencil.stencilCompareFunction = MTLCompareFunctionNotEqual;
            stencil.readMask = 0x7F;
            stencil.writeMask = 0x7F;
        }
        else
        {
            stencil.stencilCompareFunction = MTLCompareFunctionAlways;
            stencil.readMask = 0xFF;
            stencil.writeMask = 0xFF;
        }
        dsDesc.frontFaceStencil = stencil;
        dsDesc.backFaceStencil = stencil;
        return [State->Device newDepthStencilStateWithDescriptor:dsDesc];
    };
    State->OpaqueDepthLessWrite =
        createStencilDepthState(MTLCompareFunctionLess, YES, false);
    State->OpaqueDepthLessEqualWrite =
        createStencilDepthState(MTLCompareFunctionLessEqual, YES, false);
    State->TranslucentDepthLessWrite =
        createStencilDepthState(MTLCompareFunctionLess, YES, true);
    State->TranslucentDepthLessEqualWrite =
        createStencilDepthState(MTLCompareFunctionLessEqual, YES, true);
    State->TranslucentDepthLessNoWrite =
        createStencilDepthState(MTLCompareFunctionLess, NO, true);
    State->TranslucentDepthLessEqualNoWrite =
        createStencilDepthState(MTLCompareFunctionLessEqual, NO, true);
    if (!State->OpaqueDepthLessWrite || !State->OpaqueDepthLessEqualWrite ||
        !State->TranslucentDepthLessWrite || !State->TranslucentDepthLessEqualWrite ||
        !State->TranslucentDepthLessNoWrite || !State->TranslucentDepthLessEqualNoWrite)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create depth/stencil state variants\n");
        return false;
    }

    return true;
}

bool MetalRenderer3D::BuildFinalPassPipelines()
{
    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetal3DFinalPassShaderSource];
    State->FinalPassShaderLibrary = [State->Device newLibraryWithSource:source options:nil error:&error];
    if (!State->FinalPassShaderLibrary)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to compile final-pass shaders: %s\n", message);
        return false;
    }

    id<MTLFunction> vertex = [State->FinalPassShaderLibrary newFunctionWithName:@"mp3d_final_vs"];
    id<MTLFunction> edgeFragment = [State->FinalPassShaderLibrary newFunctionWithName:@"mp3d_edge_fs"];
    id<MTLFunction> fogFragment = [State->FinalPassShaderLibrary newFunctionWithName:@"mp3d_fog_fs"];
    if (!vertex || !edgeFragment || !fogFragment)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: final-pass shader entry point missing\n");
        return false;
    }

    MTLRenderPipelineDescriptor* edgeDesc = [[MTLRenderPipelineDescriptor alloc] init];
    edgeDesc.label = @"MelonPrime Metal 3D Edge Pipeline";
    edgeDesc.vertexFunction = vertex;
    edgeDesc.fragmentFunction = edgeFragment;
    edgeDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    edgeDesc.colorAttachments[0].blendingEnabled = YES;
    edgeDesc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    edgeDesc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    edgeDesc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
    edgeDesc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorZero;
    edgeDesc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOne;
    edgeDesc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
    NSError* edgePipelineError = nil;
    State->EdgePipeline = [State->Device newRenderPipelineStateWithDescriptor:edgeDesc error:&edgePipelineError];
    if (!State->EdgePipeline)
    {
        const char* message = edgePipelineError ? [[edgePipelineError localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create edge pipeline: %s\n", message);
        return false;
    }

    auto createFogPipeline = [&](NSString* label, bool colorFog) -> id<MTLRenderPipelineState> {
        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.label = label;
        desc.vertexFunction = vertex;
        desc.fragmentFunction = fogFragment;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[0].blendingEnabled = YES;
        desc.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
        desc.colorAttachments[0].alphaBlendOperation = MTLBlendOperationAdd;
        if (colorFog)
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorBlendColor;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        }
        else
        {
            desc.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorZero;
            desc.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOne;
        }
        desc.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorBlendAlpha;
        desc.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;

        NSError* pipelineError = nil;
        id<MTLRenderPipelineState> pipeline =
            [State->Device newRenderPipelineStateWithDescriptor:desc error:&pipelineError];
        if (!pipeline)
        {
            const char* message = pipelineError ? [[pipelineError localizedDescription] UTF8String] : "unknown error";
            std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create %s: %s\n",
                [label UTF8String], message);
        }
        return pipeline;
    };

    State->FogColorPipeline = createFogPipeline(@"MelonPrime Metal 3D Fog Color Pipeline", true);
    State->FogAlphaPipeline = createFogPipeline(@"MelonPrime Metal 3D Fog Alpha Pipeline", false);
    return State->EdgePipeline && State->FogColorPipeline && State->FogAlphaPipeline;
}

bool MetalRenderer3D::CreateClearBitmapTextures()
{
    if (!State || !State->Device)
        return false;

    MTLTextureDescriptor* colorDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                                           width:256
                                                          height:256
                                                       mipmapped:NO];
    colorDesc.usage = MTLTextureUsageShaderRead;
    colorDesc.storageMode = MTLStorageModeShared;
    State->ClearBitmapColor = [State->Device newTextureWithDescriptor:colorDesc];

    MTLTextureDescriptor* depthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Uint
                                                           width:256
                                                          height:256
                                                       mipmapped:NO];
    depthDesc.usage = MTLTextureUsageShaderRead;
    depthDesc.storageMode = MTLStorageModeShared;
    State->ClearBitmapDepth = [State->Device newTextureWithDescriptor:depthDesc];

    if (!State->ClearBitmapColor || !State->ClearBitmapDepth)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create clear bitmap textures\n");
        return false;
    }

    State->ClearBitmapDirty = 0x3;
    return true;
}

bool MetalRenderer3D::UpdateClearBitmapTextures(u8 clrBitmapDirty)
{
    if (!State || !State->ClearBitmapColor || !State->ClearBitmapDepth)
        return false;

    State->ClearBitmapDirty |= clrBitmapDirty;
    if (!(GPU3D.RenderDispCnt & (1u << 14)))
        return true;

    const MTLRegion region = MTLRegionMake2D(0, 0, 256, 256);
    if (State->ClearBitmapDirty & (1u << 0))
    {
        const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x40000]);
        for (size_t i = 0; i < State->ClearBitmapColorData.size(); i++)
        {
            const u16 color = vram[i];
            u32 r = (color << 1) & 0x3E; if (r) r++;
            u32 g = (color >> 4) & 0x3E; if (g) g++;
            u32 b = (color >> 9) & 0x3E; if (b) b++;
            u32 a = (color & 0x8000) ? 31 : 0;
            State->ClearBitmapColorData[i] = r | (g << 8) | (b << 16) | (a << 24);
        }
        [State->ClearBitmapColor replaceRegion:region
                                    mipmapLevel:0
                                      withBytes:State->ClearBitmapColorData.data()
                                    bytesPerRow:256 * sizeof(u32)];
    }

    if (State->ClearBitmapDirty & (1u << 1))
    {
        const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x60000]);
        for (size_t i = 0; i < State->ClearBitmapDepthData.size(); i++)
        {
            const u16 value = vram[i];
            const u32 depth = ((value & 0x7FFF) * 0x200) + 0x1FF;
            const u32 fog = (value & 0x8000) << 9;
            State->ClearBitmapDepthData[i] = depth | fog;
        }
        [State->ClearBitmapDepth replaceRegion:region
                                    mipmapLevel:0
                                      withBytes:State->ClearBitmapDepthData.data()
                                    bytesPerRow:256 * sizeof(u32)];
    }

    State->ClearBitmapDirty = 0;
    return true;
}

bool MetalRenderer3D::ResizeTargets()
{
    const NSUInteger width = 256;
    const NSUInteger height = 192;

    MTLTextureDescriptor* colorDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    colorDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newColorTarget = [State->Device newTextureWithDescriptor:colorDesc];

    MTLTextureDescriptor* depthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    depthDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newDepthStencilTarget = [State->Device newTextureWithDescriptor:depthDesc];

    MTLTextureDescriptor* attrDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    attrDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    attrDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newAttrTarget = [State->Device newTextureWithDescriptor:attrDesc];

    if (!newColorTarget || !newDepthStencilTarget || !newAttrTarget)
    {
        // Commit nothing on partial failure: keep whatever targets (if any)
        // were already valid rather than leaving State with a mix of new
        // and stale textures of mismatched sizes.
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to allocate render targets\n");
        return false;
    }

    State->ColorTarget = newColorTarget;
    State->DepthStencilTarget = newDepthStencilTarget;
    State->AttrTarget = newAttrTarget;
    State->NativeReadbackBuffer =
        [State->Device newBufferWithLength:width * height * 4
                                   options:MTLResourceStorageModeShared];
    State->NativeLineReady = false;
    MetalPerfLogTargetResize(ScaleFactor, width, height);

    UpdateClearBitmapTextures(0);
    return State->NativeReadbackBuffer && ClearNativeTarget();
}

bool MetalRenderer3D::ClearNativeTarget()
{
    if (!State || !State->CommandQueue ||
        !State->ColorTarget || !State->DepthStencilTarget || !State->AttrTarget)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: ClearNativeTarget called before resources are ready\n");
        return false;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->ColorTarget;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto expand5To6 = [](u32 value) -> double {
        u32 expanded = (value << 1) & 0x3E;
        if (expanded)
            expanded++;
        return static_cast<double>(expanded) / 63.0;
    };
    const double clearR = expand5To6(GPU3D.RenderClearAttr1 & 0x1F);
    const double clearG = expand5To6((GPU3D.RenderClearAttr1 >> 5) & 0x1F);
    const double clearB = expand5To6((GPU3D.RenderClearAttr1 >> 10) & 0x1F);
    const double clearA = static_cast<double>((GPU3D.RenderClearAttr1 >> 16) & 0x1F) / 31.0;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(clearR, clearG, clearB, clearA);

    pass.colorAttachments[1].texture = State->AttrTarget;
    pass.colorAttachments[1].loadAction = MTLLoadActionClear;
    pass.colorAttachments[1].storeAction = MTLStoreActionStore;
    const double clearPolyID = static_cast<double>((GPU3D.RenderClearAttr1 >> 24) & 0x3F) / 63.0;
    const double clearFogFlag = static_cast<double>((GPU3D.RenderClearAttr1 >> 15) & 0x1);
    pass.colorAttachments[1].clearColor = MTLClearColorMake(clearPolyID, 0.0, clearFogFlag, 1.0);

    pass.depthAttachment.texture = State->DepthStencilTarget;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    const u32 clearZ = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
    pass.depthAttachment.clearDepth = static_cast<double>(clearZ) / 16777215.0;

    pass.stencilAttachment.texture = State->DepthStencilTarget;
    pass.stencilAttachment.loadAction = MTLLoadActionClear;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.clearStencil = 0xFF;

    id<MTLCommandBuffer> commandBuffer = [State->CommandQueue commandBuffer];
    if (!commandBuffer)
        return false;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return false;

    if ((GPU3D.RenderDispCnt & (1u << 14)) &&
        State->ClearPipeline && State->ClearDepthStencil &&
        State->ClearBitmapColor && State->ClearBitmapDepth)
    {
        struct ClearBitmapConfigCpu
        {
            float offset[2];
            uint32_t opaquePolyID;
            uint32_t pad;
        } config = {};
        config.offset[0] = static_cast<float>((GPU3D.RenderClearAttr2 >> 16) & 0xFF) / 256.0f;
        config.offset[1] = static_cast<float>((GPU3D.RenderClearAttr2 >> 24) & 0xFF) / 256.0f;
        config.opaquePolyID = (GPU3D.RenderClearAttr1 >> 24) & 0x3F;

        [encoder setRenderPipelineState:State->ClearPipeline];
        [encoder setDepthStencilState:State->ClearDepthStencil];
        [encoder setFragmentBytes:&config length:sizeof(config) atIndex:0];
        [encoder setFragmentTexture:State->ClearBitmapColor atIndex:0];
        [encoder setFragmentTexture:State->ClearBitmapDepth atIndex:1];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }

    [encoder endEncoding];
    [commandBuffer commit];
    const auto waitStart = MetalPerfEnabled() ? MetalPerfClock::now() : MetalPerfClock::time_point {};
    [commandBuffer waitUntilCompleted];
    if (MetalPerfEnabled())
        MetalPerfAddWait(waitStart, MetalPerfClock::now());
    return commandBuffer.status == MTLCommandBufferStatusCompleted;
}

bool MetalRenderer3D::RenderFinalPostPass()
{
    if (!(GPU3D.RenderDispCnt & ((1u << 5) | (1u << 7))))
        return true;
    if (!State || !State->CommandQueue || !State->ColorTarget || !State->DepthStencilTarget ||
        !State->AttrTarget || !State->EdgePipeline || !State->FogColorPipeline || !State->FogAlphaPipeline)
        return false;

    struct FinalPassConfigCpu
    {
        uint32_t dispCnt;
        uint32_t fogOffset;
        uint32_t fogShift;
        uint32_t pad;
        float edgeColors[8][4];
        float fogDensity[34][4];
    } config = {};
    config.dispCnt = GPU3D.RenderDispCnt;
    config.fogOffset = GPU3D.RenderFogOffset;
    config.fogShift = GPU3D.RenderFogShift;
    for (int i = 0; i < 8; i++)
    {
        const u16 color = GPU3D.RenderEdgeTable[i];
        config.edgeColors[i][0] = static_cast<float>(color & 0x1F) / 31.0f;
        config.edgeColors[i][1] = static_cast<float>((color >> 5) & 0x1F) / 31.0f;
        config.edgeColors[i][2] = static_cast<float>((color >> 10) & 0x1F) / 31.0f;
        config.edgeColors[i][3] = 1.0f;
    }
    for (int i = 0; i < 34; i++)
        config.fogDensity[i][0] = static_cast<float>(GPU3D.RenderFogDensityTable[i]) / 127.0f;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->ColorTarget;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [State->CommandQueue commandBuffer];
    if (!commandBuffer)
        return false;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return false;

    [encoder setFragmentBytes:&config length:sizeof(config) atIndex:0];
    [encoder setFragmentTexture:State->DepthStencilTarget atIndex:0];
    [encoder setFragmentTexture:State->AttrTarget atIndex:1];

    if (GPU3D.RenderDispCnt & (1u << 5))
    {
        [encoder setRenderPipelineState:State->EdgePipeline];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }

    if (GPU3D.RenderDispCnt & (1u << 7))
    {
        const bool alphaOnlyFog = (GPU3D.RenderDispCnt & (1u << 6)) != 0;
        [encoder setRenderPipelineState:(alphaOnlyFog ? State->FogAlphaPipeline : State->FogColorPipeline)];

        const u32 fogColor = GPU3D.RenderFogColor;
        [encoder setBlendColorRed:static_cast<float>(fogColor & 0x1F) / 31.0f
                            green:static_cast<float>((fogColor >> 5) & 0x1F) / 31.0f
                             blue:static_cast<float>((fogColor >> 10) & 0x1F) / 31.0f
                            alpha:static_cast<float>((fogColor >> 16) & 0x1F) / 31.0f];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    }
    [encoder endEncoding];

    [commandBuffer commit];
    const auto waitStart = MetalPerfEnabled() ? MetalPerfClock::now() : MetalPerfClock::time_point {};
    [commandBuffer waitUntilCompleted];
    if (MetalPerfEnabled())
        MetalPerfAddWait(waitStart, MetalPerfClock::now());
    return commandBuffer.status == MTLCommandBufferStatusCompleted;
}

bool MetalRenderer3D::ReadbackNativeColorTargetToLineBuffer()
{
    if (!State || !State->CommandQueue || !State->ColorTarget || !State->NativeReadbackBuffer)
        return false;

    id<MTLCommandBuffer> commandBuffer = [State->CommandQueue commandBuffer];
    if (!commandBuffer)
        return false;

    id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
    if (!blit)
        return false;

    [blit copyFromTexture:State->ColorTarget
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(256, 192, 1)
                 toBuffer:State->NativeReadbackBuffer
        destinationOffset:0
   destinationBytesPerRow:256 * 4
 destinationBytesPerImage:256 * 192 * 4];
    [blit endEncoding];
    [commandBuffer commit];
    const auto waitStart = MetalPerfEnabled() ? MetalPerfClock::now() : MetalPerfClock::time_point {};
    [commandBuffer waitUntilCompleted];
    if (MetalPerfEnabled())
        MetalPerfAddWait(waitStart, MetalPerfClock::now());
    if (commandBuffer.status != MTLCommandBufferStatusCompleted)
        return false;

    const uint8_t* pixels = static_cast<const uint8_t*>([State->NativeReadbackBuffer contents]);
    for (int y = 0; y < 192; y++)
    {
        for (int x = 0; x < 256; x++)
        {
            const uint8_t* px = pixels + ((y * 256 + x) * 4);
            const u32 b = (static_cast<u32>(px[0]) * 63u + 127u) / 255u;
            const u32 g = (static_cast<u32>(px[1]) * 63u + 127u) / 255u;
            const u32 r = (static_cast<u32>(px[2]) * 63u + 127u) / 255u;
            const u32 a = (static_cast<u32>(px[3]) * 31u + 127u) / 255u;
            State->NativeLineBuffer[static_cast<size_t>(y) * 256u + x] =
                r | (g << 8) | (b << 16) | (a << 24);
        }
    }
    State->NativeLineReady = true;
    return true;
}

bool MetalRenderer3D::DrawSolidNative3DDiagnostic()
{
    if (!State || !State->CommandQueue || !State->ColorTarget || !State->AttrTarget || !State->DepthStencilTarget)
        return false;

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->ColorTarget;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.85, 0.35, 1.0);

    pass.colorAttachments[1].texture = State->AttrTarget;
    pass.colorAttachments[1].loadAction = MTLLoadActionClear;
    pass.colorAttachments[1].storeAction = MTLStoreActionStore;
    pass.colorAttachments[1].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    pass.depthAttachment.texture = State->DepthStencilTarget;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 1.0;

    pass.stencilAttachment.texture = State->DepthStencilTarget;
    pass.stencilAttachment.loadAction = MTLLoadActionClear;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.clearStencil = 0xFF;

    id<MTLCommandBuffer> commandBuffer = [State->CommandQueue commandBuffer];
    if (!commandBuffer)
        return false;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return false;
    [encoder setViewport:(MTLViewport){0.0, 0.0,
        static_cast<double>(State->ColorTarget.width),
        static_cast<double>(State->ColorTarget.height),
        0.0, 1.0}];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted)
        return false;

    State->LastDiagnostics = {};
    if (MetalDiagEnabled())
    {
        const MetalTextureReadbackSummary summary =
            ReadbackBGRA8Texture(State->CommandQueue, State->ColorTarget, 0);
        State->LastDiagnostics.NonzeroPixels = summary.NonzeroPixels;
        State->LastDiagnostics.Checksum = summary.Checksum;
        State->LastDiagnostics.FirstNonzeroX = summary.FirstNonzeroX;
        State->LastDiagnostics.FirstNonzeroY = summary.FirstNonzeroY;
        for (int c = 0; c < 4; c++)
            State->LastDiagnostics.FirstNonzeroBGRA[c] = summary.FirstNonzeroBGRA[c];
    }
    else
    {
        State->LastDiagnostics.NonzeroPixels =
            static_cast<uint64_t>(State->ColorTarget.width) * static_cast<uint64_t>(State->ColorTarget.height);
    }

    static uint64_t solidFrames = 0;
    solidFrames++;
    if (solidFrames <= 3 || (solidFrames % 60) == 0)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal 3d diag: solid native3D frame=%llu nonzero=%llu target=%zux%zu\n",
            static_cast<unsigned long long>(solidFrames),
            static_cast<unsigned long long>(State->LastDiagnostics.NonzeroPixels),
            static_cast<size_t>(State->ColorTarget.width),
            static_cast<size_t>(State->ColorTarget.height));
    }

    return true;
}

namespace {

// Groups adjacent polygons by (WBuffer, texture identity, TexRepeat), matching
// GLRenderer3D's RenderPolygonBatch()/RenderKey run model. Keeping adjacency
// instead of coalescing the whole frame preserves RenderPolygonRAM order.
struct OpaqueDrawGroupKey
{
    bool WBuffer;
    const void* TexturePtr; // nullptr = untextured; otherwise (__bridge) id<MTLTexture>
    // DS TexRepeat 4-bit field (repeat-S, repeat-T, mirror-S, mirror-T),
    // matching GLRenderer3D::RenderPolygonBatch()'s own TexRepeat-gated
    // grouping (GPU3D_OpenGL.cpp:836). Untextured polygons always use 0
    // here regardless of their raw TexRepeat bits, since no sampling
    // happens for them -- letting the wrap mode vary would only fragment
    // the untextured group into multiple identical draw calls for no
    // reason.
    uint32_t TexRepeat;
    uint32_t PolyID;
    bool Translucent;
    bool DepthEqual;
    bool DepthWrite;
    bool Line;

    bool operator==(const OpaqueDrawGroupKey& other) const noexcept
    {
        return WBuffer == other.WBuffer && TexturePtr == other.TexturePtr &&
               TexRepeat == other.TexRepeat && PolyID == other.PolyID &&
               Translucent == other.Translucent &&
               DepthEqual == other.DepthEqual && DepthWrite == other.DepthWrite &&
               Line == other.Line;
    }
};

struct OpaqueDrawGroup
{
    OpaqueDrawGroupKey Key {};
    id<MTLTexture> Texture = nil; // nil = untextured
    std::vector<uint16_t> Indices;
};

} // namespace

// Phase 8 port-order steps 2-4 (design doc S14: "vertex/index upload",
// "opaque polygons", texturing). Builds a packed vertex buffer plus one
// index buffer per adjacent (WBuffer, texture, TexRepeat) group from GPU3D::RenderPolygonRAM
// every frame, matching GLRenderer3D::BuildPolygons()/SetupVertex()'s data
// layout and !BetterPolygons fan triangulation exactly (GPU3D_OpenGL.cpp),
// resolves textures through the shared Texcache<> template (GPU3D_Texcache.h
// -- the same DS-format-decode logic GLRenderer3D uses, just with a Metal
// resource loader instead of a GL one), then issues one drawIndexedPrimitives
// per non-empty group.
//
// Explicitly NOT implemented yet (tracked in melonprime-metal-backend-plan.md
// Phase 8 remainder; see also the scope note above kMetal3DOpaqueShaderSource):
//   - shadow masks/shadows, VRAM-display-capture-as-texture
//   - "BetterPolygons" alternate triangulation
//   - clear bitmap, edge marking, fog, and the final GL-style post-pass
//
// ScaleFactor controls the Metal render-target size. DS screen-space
// coordinates remain native 256x192 and are mapped to NDC, so rasterization
// covers the full scaled render target. Full GL parity is still incomplete
// because shadow masks, clear bitmap, edge/fog/final post-processing, and
// hires 2D/3D composition are not finished.
void MetalRenderer3D::RenderNativeOpaquePolygons()
{
    if (!State || !State->Device || !State->CommandQueue ||
        !State->OpaqueRenderPipelineZ || !State->OpaqueRenderPipelineW ||
        !State->TranslucentRenderPipelineZ || !State->TranslucentRenderPipelineW ||
        !State->OpaqueDepthLessWrite || !State->OpaqueDepthLessEqualWrite ||
        !State->TranslucentDepthLessWrite || !State->TranslucentDepthLessEqualWrite ||
        !State->TranslucentDepthLessNoWrite || !State->TranslucentDepthLessEqualNoWrite ||
        !State->OpaqueTextureSamplers[0][0] || !State->DummyTexture || !State->Texcache ||
        !State->ColorTarget || !State->DepthStencilTarget || !State->AttrTarget)
    {
        return;
    }
    State->LastDiagnostics = {};

    const u32 numPolygons = GPU3D.RenderNumPolygons;
    if (numPolygons == 0)
        return;

    std::vector<uint32_t> vertexWords;
    vertexWords.reserve(static_cast<size_t>(numPolygons) * 4 * 7);
    std::vector<OpaqueDrawGroup> groups;

    // u16 indices cap the addressable vertex count at 65535; GPU3D::VertexRAM
    // itself is sized 6144*2, so a real frame cannot come close to this --
    // this is a defensive guard, not an expected limitation in practice.
    constexpr size_t kMaxVertices = 0xFFFFu;

    // metal_phase8_execution_instructions.md Priority 2: one-shot proof that
    // this path is actually traversing real GPU3D::RenderPolygonRAM state
    // from an integrated ROM run, with counts to sanity-check against what
    // the scene should contain -- not just "it didn't crash".
    static bool loggedFirstOpaquePass = false;
    u32 consideredPolygons = 0;
    u32 texturedPolygons = 0;
    u32 drawCount = 0;

    for (u32 i = 0; i < numPolygons; i++)
    {
        const Polygon* poly = GPU3D.RenderPolygonRAM[i];
        if (!poly)
            continue;
        if (poly->Type != 0 && poly->Type != 1) continue;
        if (poly->IsShadowMask || poly->IsShadow) continue;
        if (poly->Degenerate)         continue;
        if (poly->NumVertices < 3)    continue;

        const size_t vertexBase = vertexWords.size() / 7;
        if (vertexBase + poly->NumVertices > kMaxVertices)
            break;

        consideredPolygons++;

        const uint32_t alpha = (static_cast<uint32_t>(poly->Attr) >> 16) & 0x1Fu;

        // Texture resolution: same entry point GLRenderer3D::BuildPolygons()
        // uses (Texcache<>::GetTexture()), called unconditionally per
        // textured polygon here rather than only on texparam/texpal change --
        // GetTexture() is a cache lookup either way, so this trades GL's
        // micro-optimization for simpler, still-correct code.
        id<MTLTexture> texture = nil;
        uint32_t texLayerWord = 0xFFFFu; // sentinel: no texture
        uint32_t texDimsWord = 0;
        const u32 textype = (poly->TexParam >> 26) & 0x7u;
        if (textype != 0)
        {
            id<MTLTexture> handle = nil;
            u32 layer = 0;
            u32* unusedVariantHelper = nullptr;
            State->Texcache->GetTexture(poly->TexParam, poly->TexPalette, handle, layer, unusedVariantHelper);
            if (handle)
            {
                texture = handle;
                texLayerWord = layer & 0xFFFFu;
                texDimsWord = TextureWidth(poly->TexParam) | (TextureHeight(poly->TexParam) << 16);
                texturedPolygons++;
            }
        }

        for (u32 v = 0; v < poly->NumVertices; v++)
        {
            const Vertex* vtx = poly->Vertices[v];

            uint32_t z = static_cast<uint32_t>(poly->FinalZ[v]);
            const uint32_t w = static_cast<uint32_t>(poly->FinalW[v]);
            uint32_t zshift = 0;
            while (z > 0xFFFFu) { z >>= 1; zshift++; }

            const uint32_t x = static_cast<uint32_t>(vtx->FinalPosition[0]) & 0xFFFFu;
            const uint32_t y = static_cast<uint32_t>(vtx->FinalPosition[1]) & 0xFFFFu;

            const uint32_t r = static_cast<uint32_t>(vtx->FinalColor[0] >> 1) & 0xFFu;
            const uint32_t g = static_cast<uint32_t>(vtx->FinalColor[1] >> 1) & 0xFFu;
            const uint32_t b = static_cast<uint32_t>(vtx->FinalColor[2] >> 1) & 0xFFu;

            const uint32_t texS = static_cast<uint16_t>(vtx->TexCoords[0]);
            const uint32_t texT = static_cast<uint16_t>(vtx->TexCoords[1]);

            vertexWords.push_back(x | (y << 16));
            vertexWords.push_back(z | (w << 16));
            uint32_t shaderAttr =
                (static_cast<uint32_t>(poly->Attr) & ((0x3Fu << 24) | (1u << 15) | 0x30u)) |
                (zshift << 16);
            if (poly->Translucent)
                shaderAttr |= (1u << 31);

            vertexWords.push_back(r | (g << 8) | (b << 16) | (alpha << 24));
            vertexWords.push_back(texS | (texT << 16));
            vertexWords.push_back(shaderAttr);
            vertexWords.push_back(texLayerWord);
            vertexWords.push_back(texDimsWord);
        }

        const uint32_t texRepeatForKey = texture ? ((static_cast<uint32_t>(poly->TexParam) >> 16) & 0xFu) : 0u;
        const bool translucent = poly->Translucent;
        const bool depthEqual = (poly->Attr & (1u << 14)) != 0;
        const bool depthWrite = !translucent || ((poly->Attr & (1u << 11)) != 0);
        const uint32_t polyID = (static_cast<uint32_t>(poly->Attr) >> 24) & 0x3Fu;
        const bool line = poly->Type == 1;
        const OpaqueDrawGroupKey key {
            poly->WBuffer,
            (__bridge const void*)texture,
            texRepeatForKey,
            polyID,
            translucent,
            depthEqual,
            depthWrite,
            line
        };
        if (groups.empty() || !(groups.back().Key == key))
        {
            OpaqueDrawGroup newGroup;
            newGroup.Key = key;
            newGroup.Texture = texture;
            groups.push_back(std::move(newGroup));
        }
        OpaqueDrawGroup& group = groups.back();
        if (line)
        {
            u32 selected[2] = {};
            u32 selectedCount = 0;
            u32 lastX = 0;
            u32 lastY = 0;
            for (u32 v = 0; v < poly->NumVertices && selectedCount < 2; v++)
            {
                const Vertex* vtx = poly->Vertices[v];
                const u32 x = static_cast<uint32_t>(vtx->FinalPosition[0]);
                const u32 y = static_cast<uint32_t>(vtx->FinalPosition[1]);
                if (selectedCount > 0 && lastX == x && lastY == y)
                    continue;
                lastX = x;
                lastY = y;
                selected[selectedCount++] = v;
            }
            if (selectedCount == 2)
            {
                group.Indices.push_back(static_cast<uint16_t>(vertexBase + selected[0]));
                group.Indices.push_back(static_cast<uint16_t>(vertexBase + selected[1]));
            }
        }
        else
        {
            for (u32 t = 2; t < poly->NumVertices; t++)
            {
                group.Indices.push_back(static_cast<uint16_t>(vertexBase + 0));
                group.Indices.push_back(static_cast<uint16_t>(vertexBase + t - 1));
                group.Indices.push_back(static_cast<uint16_t>(vertexBase + t));
            }
        }
    }

    if (!loggedFirstOpaquePass)
    {
        loggedFirstOpaquePass = true;
        std::fprintf(stderr,
            "[MelonPrime] metal renderer3D: first visible pass polygons=%u considered=%u textured=%u groups=%zu vertexWords=%zu\n",
            numPolygons, consideredPolygons, texturedPolygons, groups.size(), vertexWords.size());
    }
    // The very first RenderFrame() is frequently a firmware boot/logo screen
    // built entirely from translucent fade polygons, which this pass filters
    // out -- "considered=0" there is expected, not a bug. Log again the
    // first time a frame actually has opaque geometry to render, so the
    // integrated-ROM verification has a second, more representative data
    // point once the game's own content (title screen, gameplay) is reached.
    static bool loggedFirstNonEmptyOpaquePass = false;
    if (!loggedFirstNonEmptyOpaquePass && consideredPolygons > 0)
    {
        loggedFirstNonEmptyOpaquePass = true;
        std::fprintf(stderr,
            "[MelonPrime] metal renderer3D: first non-empty visible pass polygons=%u considered=%u textured=%u groups=%zu vertexWords=%zu\n",
            numPolygons, consideredPolygons, texturedPolygons, groups.size(), vertexWords.size());
    }

    if (vertexWords.empty() || groups.empty())
    {
        State->LastDiagnostics.ConsideredPolygons = consideredPolygons;
        State->LastDiagnostics.TexturedPolygons = texturedPolygons;
        State->LastDiagnostics.Groups = static_cast<uint32_t>(groups.size());
        State->LastDiagnostics.Draws = 0;
        if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
        {
            gCurrentMetalPerfFrame->ConsideredPolygons += consideredPolygons;
            gCurrentMetalPerfFrame->TexturedPolygons += texturedPolygons;
            gCurrentMetalPerfFrame->Groups += static_cast<uint32_t>(groups.size());
        }
        return;
    }

    id<MTLBuffer> vertexBuffer =
        [State->Device newBufferWithBytes:vertexWords.data()
                                    length:vertexWords.size() * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    if (!vertexBuffer)
        return;
    if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
        gCurrentMetalPerfFrame->UploadBytes += vertexWords.size() * sizeof(uint32_t);

    // Always the native 256x192 DS coordinate space -- FinalPosition is used
    // unconditionally above regardless of ScaleFactor (see the scope note).
    struct OpaqueRenderConfigCpu
    {
        float screenSize[2];
        uint32_t dispCnt;
        uint32_t pad;
        float toonTable[32][4];
    } config = {};
    config.screenSize[0] = 256.0f;
    config.screenSize[1] = 192.0f;
    config.dispCnt = GPU3D.RenderDispCnt;
    for (int i = 0; i < 32; i++)
    {
        const u16 color = GPU3D.RenderToonTable[i];
        config.toonTable[i][0] = static_cast<float>(color & 0x1F) / 31.0f;
        config.toonTable[i][1] = static_cast<float>((color >> 5) & 0x1F) / 31.0f;
        config.toonTable[i][2] = static_cast<float>((color >> 10) & 0x1F) / 31.0f;
        config.toonTable[i][3] = 1.0f;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->ColorTarget;
    pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[1].texture = State->AttrTarget;
    pass.colorAttachments[1].loadAction = MTLLoadActionLoad;
    pass.colorAttachments[1].storeAction = MTLStoreActionStore;
    pass.depthAttachment.texture = State->DepthStencilTarget;
    pass.depthAttachment.loadAction = MTLLoadActionLoad;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.stencilAttachment.texture = State->DepthStencilTarget;
    pass.stencilAttachment.loadAction = MTLLoadActionLoad;
    pass.stencilAttachment.storeAction = MTLStoreActionStore;

    id<MTLCommandBuffer> commandBuffer = [State->CommandQueue commandBuffer];
    if (!commandBuffer)
        return;

    id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
    if (!encoder)
        return;

    [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&config length:sizeof(config) atIndex:1];
    [encoder setFragmentBytes:&config length:sizeof(config) atIndex:1];
    [encoder setViewport:(MTLViewport){0.0, 0.0,
        static_cast<double>(State->ColorTarget.width),
        static_cast<double>(State->ColorTarget.height),
        0.0, 1.0}];

    for (auto& group : groups)
    {
        if (group.Indices.empty())
            continue;

        id<MTLBuffer> indexBuffer =
            [State->Device newBufferWithBytes:group.Indices.data()
                                        length:group.Indices.size() * sizeof(uint16_t)
                                       options:MTLResourceStorageModeShared];
        if (!indexBuffer)
            continue;
        if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
            gCurrentMetalPerfFrame->UploadBytes += group.Indices.size() * sizeof(uint16_t);

        // Priority 4: select the sampler matching this group's DS TexRepeat
        // bits (repeat-S=bit0, repeat-T=bit1, mirror-S=bit2, mirror-T=bit3),
        // exactly like GLRenderer3D::SetupPolygonTexture(). Untextured
        // groups always carry TexRepeat=0 (clamp/clamp), which is never
        // sampled anyway.
        const int sIdx = TexRepeatAddressModeIndex(group.Key.TexRepeat, 0, 2);
        const int tIdx = TexRepeatAddressModeIndex(group.Key.TexRepeat, 1, 3);

        id<MTLRenderPipelineState> pipeline = nil;
        if (group.Key.Translucent)
            pipeline = group.Key.WBuffer ? State->TranslucentRenderPipelineW : State->TranslucentRenderPipelineZ;
        else
            pipeline = group.Key.WBuffer ? State->OpaqueRenderPipelineW : State->OpaqueRenderPipelineZ;

        id<MTLDepthStencilState> depthState = nil;
        if (group.Key.Translucent)
        {
            if (group.Key.DepthEqual)
                depthState = group.Key.DepthWrite ? State->TranslucentDepthLessEqualWrite
                                                  : State->TranslucentDepthLessEqualNoWrite;
            else
                depthState = group.Key.DepthWrite ? State->TranslucentDepthLessWrite
                                                  : State->TranslucentDepthLessNoWrite;
        }
        else
        {
            depthState = group.Key.DepthEqual ? State->OpaqueDepthLessEqualWrite
                                              : State->OpaqueDepthLessWrite;
        }

        [encoder setRenderPipelineState:pipeline];
        [encoder setDepthStencilState:depthState];
        [encoder setStencilReferenceValue:(group.Key.Translucent ? (0x40u | group.Key.PolyID)
                                                                  : group.Key.PolyID)];
        [encoder setFragmentTexture:(group.Texture ? group.Texture : State->DummyTexture) atIndex:0];
        [encoder setFragmentSamplerState:State->OpaqueTextureSamplers[sIdx][tIdx] atIndex:0];
        [encoder drawIndexedPrimitives:(group.Key.Line ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle)
                             indexCount:group.Indices.size()
                              indexType:MTLIndexTypeUInt16
                            indexBuffer:indexBuffer
                      indexBufferOffset:0];
        drawCount++;
        if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
            gCurrentMetalPerfFrame->Draws++;
    }

    [encoder endEncoding];
    [commandBuffer commit];
    const auto waitStart = MetalPerfEnabled() ? MetalPerfClock::now() : MetalPerfClock::time_point {};
    [commandBuffer waitUntilCompleted];
    if (MetalPerfEnabled())
        MetalPerfAddWait(waitStart, MetalPerfClock::now());

    if (MetalPerfEnabled() && gCurrentMetalPerfFrame)
    {
        gCurrentMetalPerfFrame->ConsideredPolygons += consideredPolygons;
        gCurrentMetalPerfFrame->TexturedPolygons += texturedPolygons;
        gCurrentMetalPerfFrame->Groups += static_cast<uint32_t>(groups.size());
    }

    if (MetalDiagEnabled())
    {
        const MetalTextureReadbackSummary summary =
            ReadbackBGRA8Texture(State->CommandQueue, State->ColorTarget, 0);
        State->LastDiagnostics.NonzeroPixels = summary.NonzeroPixels;
        State->LastDiagnostics.Checksum = summary.Checksum;
        State->LastDiagnostics.FirstNonzeroX = summary.FirstNonzeroX;
        State->LastDiagnostics.FirstNonzeroY = summary.FirstNonzeroY;
        for (int c = 0; c < 4; c++)
            State->LastDiagnostics.FirstNonzeroBGRA[c] = summary.FirstNonzeroBGRA[c];
        State->LastDiagnostics.ConsideredPolygons = consideredPolygons;
        State->LastDiagnostics.TexturedPolygons = texturedPolygons;
        State->LastDiagnostics.Groups = static_cast<uint32_t>(groups.size());
        State->LastDiagnostics.Draws = drawCount;

        static uint64_t diagFrames = 0;
        diagFrames++;
        const bool zeroAfterDraw = drawCount > 0 && summary.Valid && summary.NonzeroPixels == 0;
        if (diagFrames <= 3 || (diagFrames % 60) == 0 || (zeroAfterDraw && !State->LoggedNativeZeroAfterDraw))
        {
            if (zeroAfterDraw)
                State->LoggedNativeZeroAfterDraw = true;
            std::fprintf(stderr,
                "[MelonPrime] metal 3d diag: nonzero=%llu first=(%d,%d) "
                "firstBGRA=%02x,%02x,%02x,%02x checksum=0x%016llx "
                "considered=%u textured=%u groups=%zu draws=%u valid=%u\n",
                static_cast<unsigned long long>(summary.NonzeroPixels),
                summary.FirstNonzeroX,
                summary.FirstNonzeroY,
                summary.FirstNonzeroBGRA[0],
                summary.FirstNonzeroBGRA[1],
                summary.FirstNonzeroBGRA[2],
                summary.FirstNonzeroBGRA[3],
                static_cast<unsigned long long>(summary.Checksum),
                consideredPolygons,
                texturedPolygons,
                groups.size(),
                drawCount,
                summary.Valid ? 1u : 0u);
        }
    }
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
