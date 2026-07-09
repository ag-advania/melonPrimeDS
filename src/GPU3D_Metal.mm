// MelonPrimeDS - experimental Metal 3D renderer scaffold (Metal-plan Phase 8)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_Metal.h"

#include <cstdio>
#include <cstdint>
#include <vector>

namespace melonDS
{

struct MetalRenderer3D::MetalState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> CommandQueue = nil;
    id<MTLLibrary> ShaderLibrary = nil;
    id<MTLRenderPipelineState> ClearPipeline = nil;
    id<MTLDepthStencilState> ClearDepthStencil = nil;
    id<MTLTexture> ColorTarget = nil;
    id<MTLTexture> DepthStencilTarget = nil;
    id<MTLTexture> AttrTarget = nil;

    // Phase 8 opaque-polygon pass (see RenderNativeOpaquePolygons()).
    id<MTLLibrary> OpaqueShaderLibrary = nil;
    id<MTLRenderPipelineState> OpaqueRenderPipelineZ = nil; // standard depth-buffer polygons
    id<MTLRenderPipelineState> OpaqueRenderPipelineW = nil; // DS W-buffered polygons
    id<MTLDepthStencilState> OpaqueDepthState = nil;        // shared by both variants
};

static constexpr const char* kMetal3DShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct ClearVertexOut
{
    float4 position [[position]];
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
    return out;
}

struct ClearFragmentOut
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
};

fragment ClearFragmentOut mp3d_clear_fs()
{
    ClearFragmentOut out;
    out.color = float4(0.0, 0.0, 0.0, 1.0);
    out.attr = float4(0.0, 0.0, 0.0, 0.0);
    return out;
}
)";

// Opaque-polygon pass (Phase 8 port-order step 3, design doc S14). Vertex
// data is "pulled" from a raw packed uint buffer rather than bound via a
// MTLVertexDescriptor -- 7 words per vertex, matching the exact CPU-side
// packing GLRenderer3D::SetupVertex() writes (GPU3D_OpenGL.cpp) so the
// interpretation below can be checked word-for-word against that reference
// implementation:
//   word0 = x | (y << 16)                      -- screen-space position (native res)
//   word1 = z | (w << 16)                       -- 16-bit shifted Z, 16-bit W
//   word2 = (r>>1) | (g>>1)<<8 | (b>>1)<<16 | alpha<<24
//   word3 = texcoord (unused -- texture cache not ported yet)
//   word4 = zshift << 16                        -- vtxattr bits unused here
//   word5 = texlayer (unused)
//   word6 = tex width|height (unused)
//
// The position/depth math mirrors 3DRenderVS.glsl exactly, with one
// necessary change: GL's NDC z range is [-1,1] and a separate depth-range
// remap (glDepthRange(0,1)) folds that to [0,1] before the depth test.
// Metal has no such remap -- its NDC z *is* device depth [0,1] -- so the
// remap is folded into the shader itself: (z<<zshift)/8388608.0 - 1.0 (GL)
// becomes (z<<zshift)/16777216.0 (Metal), which is exactly the same value
// GL's own W-buffer path already computes for `fZ` (3DRenderVS.glsl line
// 36), since (X/8388608 - 1 + 1)/2 == X/16777216.
static constexpr const char* kMetal3DOpaqueShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct OpaqueRenderConfig
{
    float2 screenSize;
};

struct OpaqueVertexOut
{
    float4 position [[position]];
    float4 color;
    float fz; // perspective-correct-interpolated W-buffer depth (see mp3d_opaque_fs_w)
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
    uint w4 = words[base + 4];

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

    OpaqueVertexOut out;
    out.position = pos;
    out.color = float4(r, g, b, a) / float4(255.0, 255.0, 255.0, 31.0);
    out.fz = zndc;
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

struct OpaqueFragmentOut
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
};

// Matches 3DRenderFS.glsl's opaque branch: fully-opaque-only (DS treats
// polygon alpha < 31/31 with the "need opaque" attribute bit as translucent
// -- that bit is not carried by this pass yet, so this uses GL's simpler
// unconditional opaque threshold, discarding anything not fully solid).
fragment OpaqueFragmentOut mp3d_opaque_fs_z(OpaqueVertexOut in [[stage_in]])
{
    if (in.color.a < (30.5 / 31.0))
        discard_fragment();

    OpaqueFragmentOut out;
    out.color = float4(in.color.rgb, 1.0);
    out.attr = float4(0.0, 0.0, 0.0, 1.0);
    return out;
}

struct OpaqueFragmentOutW
{
    float4 color [[color(0)]];
    float4 attr [[color(1)]];
    float depth [[depth(any)]];
};

fragment OpaqueFragmentOutW mp3d_opaque_fs_w(OpaqueVertexOut in [[stage_in]])
{
    if (in.color.a < (30.5 / 31.0))
        discard_fragment();

    OpaqueFragmentOutW out;
    out.color = float4(in.color.rgb, 1.0);
    out.attr = float4(0.0, 0.0, 0.0, 1.0);
    out.depth = in.fz;
    return out;
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

    Delegate.Reset();
    std::fprintf(stderr,
        "[MelonPrime] metal renderer3D: native Metal device/queue/targets initialized; software raster delegate still active\n");
    return true;
}

void MetalRenderer3D::Reset()
{
    Delegate.Reset();
    if (State && State->Device)
        ClearNativeTarget();
}

void MetalRenderer3D::SetThreaded(bool threaded) noexcept
{
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
    Delegate.RenderFrame();

    // Native Metal "shadow" pass: real GPU geometry submission every frame,
    // executed alongside (not instead of) the software delegate. Its output
    // is not yet consumed by GetLine()/the presenter -- see the scope note
    // on RenderNativeOpaquePolygons() -- so this cannot regress what is
    // displayed even if something here is wrong; it exists to build and
    // exercise the native draw path ahead of GetLine() integration.
    if (State && State->Device)
    {
        ClearNativeTarget();
        RenderNativeOpaquePolygons();
    }
}

void MetalRenderer3D::FinishRendering()
{
    Delegate.FinishRendering();
}

void MetalRenderer3D::RestartFrame()
{
    Delegate.RestartFrame();
}

u32* MetalRenderer3D::GetLine(int line)
{
    return Delegate.GetLine(line);
}

void MetalRenderer3D::SetupRenderThread()
{
    Delegate.SetupRenderThread();
}

void MetalRenderer3D::EnableRenderThread()
{
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

    if (!BuildOpaqueRenderPipelines())
        return false;

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

    MTLRenderPipelineDescriptor* descZ = [[MTLRenderPipelineDescriptor alloc] init];
    descZ.label = @"MelonPrime Metal 3D Opaque Pipeline (Z-buffer)";
    descZ.vertexFunction = vsZ;
    descZ.fragmentFunction = fsZ;
    descZ.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descZ.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
    descZ.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    descZ.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

    error = nil;
    State->OpaqueRenderPipelineZ = [State->Device newRenderPipelineStateWithDescriptor:descZ error:&error];
    if (!State->OpaqueRenderPipelineZ)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create opaque Z pipeline: %s\n", message);
        return false;
    }

    MTLRenderPipelineDescriptor* descW = [[MTLRenderPipelineDescriptor alloc] init];
    descW.label = @"MelonPrime Metal 3D Opaque Pipeline (W-buffer)";
    descW.vertexFunction = vsW;
    descW.fragmentFunction = fsW;
    descW.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    descW.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
    descW.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
    descW.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

    error = nil;
    State->OpaqueRenderPipelineW = [State->Device newRenderPipelineStateWithDescriptor:descW error:&error];
    if (!State->OpaqueRenderPipelineW)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create opaque W pipeline: %s\n", message);
        return false;
    }

    // Both Z- and W-buffer variants compare/write depth identically -- the
    // only difference is *where the depth value comes from* (rasterizer-
    // interpolated position.z for Z-buffer, explicit [[depth(any)]] shader
    // output for W-buffer), so one shared state covers both pipelines.
    MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
    dsDesc.depthCompareFunction = MTLCompareFunctionLess;
    dsDesc.depthWriteEnabled = YES;
    State->OpaqueDepthState = [State->Device newDepthStencilStateWithDescriptor:dsDesc];
    if (!State->OpaqueDepthState)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to create opaque depth/stencil state\n");
        return false;
    }

    return true;
}

bool MetalRenderer3D::ResizeTargets()
{
    const NSUInteger width = static_cast<NSUInteger>(256 * ScaleFactor);
    const NSUInteger height = static_cast<NSUInteger>(192 * ScaleFactor);

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
    depthDesc.usage = MTLTextureUsageRenderTarget;
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

    return ClearNativeTarget();
}

bool MetalRenderer3D::ClearNativeTarget()
{
    if (!State || !State->CommandQueue || !State->ClearPipeline || !State->ClearDepthStencil ||
        !State->ColorTarget || !State->DepthStencilTarget || !State->AttrTarget)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: ClearNativeTarget called before resources are ready\n");
        return false;
    }

    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = State->ColorTarget;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

    pass.colorAttachments[1].texture = State->AttrTarget;
    pass.colorAttachments[1].loadAction = MTLLoadActionClear;
    pass.colorAttachments[1].storeAction = MTLStoreActionStore;
    pass.colorAttachments[1].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 0.0);

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

    [encoder setRenderPipelineState:State->ClearPipeline];
    [encoder setDepthStencilState:State->ClearDepthStencil];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    return commandBuffer.status == MTLCommandBufferStatusCompleted;
}

// Phase 8 port-order step 2/3 (design doc S14: "vertex/index upload" then
// "opaque polygons"). Builds a packed vertex buffer plus two index buffers
// (Z-buffer group, W-buffer group) from GPU3D::RenderPolygonRAM every
// frame, matching GLRenderer3D::BuildPolygons()/SetupVertex()'s data
// layout and !BetterPolygons fan triangulation exactly (GPU3D_OpenGL.cpp),
// then issues one drawIndexedPrimitives per non-empty group.
//
// Explicitly NOT implemented yet (tracked in melonprime-metal-backend-plan.md
// Phase 8 remainder):
//   - texturing (the texture cache -- GPU3D_TexcacheOpenGL.h -- is a large
//     separate subsystem; every polygon here renders with its vertex color
//     only, which is visibly wrong for the majority of real DS 3D content)
//   - translucent polygons, shadow masks/shadows, line-type polygons
//   - the depth-func-equal attribute bit (bit14) -- always MTLCompareFunctionLess
//   - "BetterPolygons" alternate triangulation
//   - hi-res scale factor (always renders at native 256x192 regardless of
//     ScaleFactor; ResizeTargets() may allocate a larger target, but this
//     pass only ever fills its native-resolution corner)
//   - edge marking, fog, and the final composite pass
//   - feeding this output to GetLine()/the presenter at all
void MetalRenderer3D::RenderNativeOpaquePolygons()
{
    if (!State || !State->Device || !State->CommandQueue ||
        !State->OpaqueRenderPipelineZ || !State->OpaqueRenderPipelineW || !State->OpaqueDepthState ||
        !State->ColorTarget || !State->DepthStencilTarget || !State->AttrTarget)
    {
        return;
    }

    const u32 numPolygons = GPU3D.RenderNumPolygons;
    if (numPolygons == 0)
        return;

    std::vector<uint32_t> vertexWords;
    std::vector<uint16_t> indicesZ;
    std::vector<uint16_t> indicesW;
    vertexWords.reserve(static_cast<size_t>(numPolygons) * 4 * 7);

    // u16 indices cap the addressable vertex count at 65535; GPU3D::VertexRAM
    // itself is sized 6144*2, so a real frame cannot come close to this --
    // this is a defensive guard, not an expected limitation in practice.
    constexpr size_t kMaxVertices = 0xFFFFu;

    for (u32 i = 0; i < numPolygons; i++)
    {
        const Polygon* poly = GPU3D.RenderPolygonRAM[i];
        if (!poly)
            continue;
        if (poly->Type != 0)          continue; // line polygons: not ported yet
        if (poly->Translucent)        continue; // opaque pass only
        if (poly->IsShadowMask || poly->IsShadow) continue;
        if (poly->Degenerate)         continue;
        if (poly->NumVertices < 3)    continue;

        const size_t vertexBase = vertexWords.size() / 7;
        if (vertexBase + poly->NumVertices > kMaxVertices)
            break;

        const uint32_t alpha = (static_cast<uint32_t>(poly->Attr) >> 16) & 0x1Fu;

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

            vertexWords.push_back(x | (y << 16));
            vertexWords.push_back(z | (w << 16));
            vertexWords.push_back(r | (g << 8) | (b << 16) | (alpha << 24));
            vertexWords.push_back(0); // texcoord -- unused (no texture cache yet)
            vertexWords.push_back(zshift << 16); // vtxattr bits unused here
            vertexWords.push_back(0); // texlayer -- unused
            vertexWords.push_back(0); // tex width|height -- unused
        }

        std::vector<uint16_t>& indices = poly->WBuffer ? indicesW : indicesZ;
        for (u32 t = 2; t < poly->NumVertices; t++)
        {
            indices.push_back(static_cast<uint16_t>(vertexBase + 0));
            indices.push_back(static_cast<uint16_t>(vertexBase + t - 1));
            indices.push_back(static_cast<uint16_t>(vertexBase + t));
        }
    }

    if (vertexWords.empty() || (indicesZ.empty() && indicesW.empty()))
        return;

    id<MTLBuffer> vertexBuffer =
        [State->Device newBufferWithBytes:vertexWords.data()
                                    length:vertexWords.size() * sizeof(uint32_t)
                                   options:MTLResourceStorageModeShared];
    if (!vertexBuffer)
        return;

    id<MTLBuffer> indexBufferZ = nil;
    if (!indicesZ.empty())
    {
        indexBufferZ = [State->Device newBufferWithBytes:indicesZ.data()
                                                    length:indicesZ.size() * sizeof(uint16_t)
                                                   options:MTLResourceStorageModeShared];
        if (!indexBufferZ)
            return;
    }

    id<MTLBuffer> indexBufferW = nil;
    if (!indicesW.empty())
    {
        indexBufferW = [State->Device newBufferWithBytes:indicesW.data()
                                                    length:indicesW.size() * sizeof(uint16_t)
                                                   options:MTLResourceStorageModeShared];
        if (!indexBufferW)
            return;
    }

    // Always the native 256x192 DS coordinate space -- FinalPosition is used
    // unconditionally above regardless of ScaleFactor (see the scope note).
    struct { float screenSize[2]; } config = { { 256.0f, 192.0f } };

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

    [encoder setDepthStencilState:State->OpaqueDepthState];
    [encoder setVertexBuffer:vertexBuffer offset:0 atIndex:0];
    [encoder setVertexBytes:&config length:sizeof(config) atIndex:1];

    if (indexBufferZ)
    {
        [encoder setRenderPipelineState:State->OpaqueRenderPipelineZ];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:indicesZ.size()
                              indexType:MTLIndexTypeUInt16
                            indexBuffer:indexBufferZ
                      indexBufferOffset:0];
    }
    if (indexBufferW)
    {
        [encoder setRenderPipelineState:State->OpaqueRenderPipelineW];
        [encoder drawIndexedPrimitives:MTLPrimitiveTypeTriangle
                             indexCount:indicesW.size()
                              indexType:MTLIndexTypeUInt16
                            indexBuffer:indexBufferW
                      indexBufferOffset:0];
    }

    [encoder endEncoding];
    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
