// MelonPrimeDS - experimental Metal 3D renderer scaffold (Metal-plan Phase 8)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU3D_Metal.h"

#include <cstdio>

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
    State->ColorTarget = [State->Device newTextureWithDescriptor:colorDesc];

    MTLTextureDescriptor* depthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    depthDesc.usage = MTLTextureUsageRenderTarget;
    depthDesc.storageMode = MTLStorageModePrivate;
    State->DepthStencilTarget = [State->Device newTextureWithDescriptor:depthDesc];

    MTLTextureDescriptor* attrDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    attrDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    attrDesc.storageMode = MTLStorageModePrivate;
    State->AttrTarget = [State->Device newTextureWithDescriptor:attrDesc];

    if (!State->ColorTarget || !State->DepthStencilTarget || !State->AttrTarget)
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer3D: failed to allocate render targets\n");
        return false;
    }

    return ClearNativeTarget();
}

bool MetalRenderer3D::ClearNativeTarget()
{
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

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
