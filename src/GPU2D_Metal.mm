// MelonPrimeDS - experimental Metal 2D renderer scaffold (Metal-plan Phase 4)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU2D_Metal.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace melonDS
{

namespace
{
constexpr int kScreenW = 256;
constexpr int kScreenH = 192;
constexpr int kBGLayerCount = 22;
}

struct MetalRenderer2D::Metal2DState
{
    id<MTLDevice> Device = nil;
    id<MTLTexture> OutputTex = nil;
    id<MTLTexture> OBJLayerTex = nil;
    id<MTLTexture> OBJDepthTex = nil;
    id<MTLTexture> AllBGLayerTex[kBGLayerCount] = {};
    int Scale = 0;
    bool LoggedFirstAllocation = false;
};

MetalRenderer2D::MetalRenderer2D(melonDS::GPU2D& gpu2D) noexcept
    : GPU2D(gpu2D),
      State(std::make_unique<Metal2DState>())
{
}

MetalRenderer2D::~MetalRenderer2D() = default;

bool MetalRenderer2D::Configure(void* preferredDevice, int scale) noexcept
{
    if (!State)
        return false;

    Metal2DState& state = *State;
    id<MTLDevice> preferredMetalDevice = preferredDevice ? (__bridge id<MTLDevice>)preferredDevice : nil;
    if (preferredMetalDevice && state.Device && state.Device != preferredMetalDevice)
    {
        state.OutputTex = nil;
        state.OBJLayerTex = nil;
        state.OBJDepthTex = nil;
        for (id<MTLTexture>& texture : state.AllBGLayerTex)
            texture = nil;
        state.Scale = 0;
        state.Device = preferredMetalDevice;
    }

    if (!state.Device)
    {
        state.Device = preferredMetalDevice ? preferredMetalDevice : MTLCreateSystemDefaultDevice();
        if (!state.Device)
        {
            std::fprintf(stderr, "[MelonPrime] metal 2d: failed to create Metal device\n");
            return false;
        }
    }

    ScaleFactor = std::max(1, scale);
    if (state.OutputTex && state.OBJLayerTex && state.OBJDepthTex &&
        state.AllBGLayerTex[0] && state.Scale == ScaleFactor)
    {
        return true;
    }

    const NSUInteger width = static_cast<NSUInteger>(kScreenW * ScaleFactor);
    const NSUInteger height = static_cast<NSUInteger>(kScreenH * ScaleFactor);

    MTLTextureDescriptor* outputDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    outputDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    outputDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> newOutput = [state.Device newTextureWithDescriptor:outputDesc];
    MTLTextureDescriptor* objLayerDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objLayerDesc.textureType = MTLTextureType2DArray;
    objLayerDesc.arrayLength = 2;
    objLayerDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objLayerDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJLayer = [state.Device newTextureWithDescriptor:objLayerDesc];

    MTLTextureDescriptor* objDepthDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    objDepthDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    objDepthDesc.storageMode = MTLStorageModePrivate;
    id<MTLTexture> newOBJDepth = [state.Device newTextureWithDescriptor:objDepthDesc];

    id<MTLTexture> newBGLayers[kBGLayerCount] = {};
    const uint16_t bgSizes[8][3] = {
        {128, 128, 2},
        {256, 256, 4},
        {256, 512, 4},
        {512, 256, 4},
        {512, 512, 4},
        {512, 1024, 1},
        {1024, 512, 1},
        {1024, 1024, 2},
    };

    int bgLayer = 0;
    for (const auto& size : bgSizes)
    {
        for (int variant = 0; variant < size[2]; variant++)
        {
            MTLTextureDescriptor* bgDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                                    width:size[0]
                                                                   height:size[1]
                                                                mipmapped:NO];
            bgDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            bgDesc.storageMode = MTLStorageModePrivate;
            newBGLayers[bgLayer++] = [state.Device newTextureWithDescriptor:bgDesc];
        }
    }

    bool bgLayersReady = bgLayer == kBGLayerCount;
    for (id<MTLTexture> texture : newBGLayers)
        bgLayersReady = bgLayersReady && texture;

    if (!newOutput || !newOBJLayer || !newOBJDepth || !bgLayersReady)
    {
        std::fprintf(stderr, "[MelonPrime] metal 2d: failed to allocate scaffold targets for engine %u\n", GPU2D.Num);
        return false;
    }

    state.OutputTex = newOutput;
    state.OBJLayerTex = newOBJLayer;
    state.OBJDepthTex = newOBJDepth;
    for (int i = 0; i < kBGLayerCount; i++)
        state.AllBGLayerTex[i] = newBGLayers[i];
    state.Scale = ScaleFactor;

    if (!state.LoggedFirstAllocation)
    {
        state.LoggedFirstAllocation = true;
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: scaffold targets engine=%u scale=%d size=%zux%zu bgLayers=%d objLayers=2 visible=0\n",
            GPU2D.Num,
            state.Scale,
            static_cast<size_t>(width),
            static_cast<size_t>(height),
            kBGLayerCount);
    }

    return true;
}

void MetalRenderer2D::Reset() noexcept
{
    if (!State)
        return;

    State->OutputTex = nil;
    State->OBJLayerTex = nil;
    State->OBJDepthTex = nil;
    for (id<MTLTexture>& texture : State->AllBGLayerTex)
        texture = nil;
    State->Scale = 0;
}

void* MetalRenderer2D::GetOutputTexture() const noexcept
{
    if (!State || !State->OutputTex)
        return nullptr;
    return (__bridge void*)State->OutputTex;
}

void* MetalRenderer2D::GetOBJLayerTexture() const noexcept
{
    if (!State || !State->OBJLayerTex)
        return nullptr;
    return (__bridge void*)State->OBJLayerTex;
}

void* MetalRenderer2D::GetOBJDepthTexture() const noexcept
{
    if (!State || !State->OBJDepthTex)
        return nullptr;
    return (__bridge void*)State->OBJDepthTex;
}

void* MetalRenderer2D::GetBGLayerTexture(int index) const noexcept
{
    if (!State || index < 0 || index >= kBGLayerCount || !State->AllBGLayerTex[index])
        return nullptr;
    return (__bridge void*)State->AllBGLayerTex[index];
}

int MetalRenderer2D::GetScaleFactor() const noexcept
{
    return ScaleFactor;
}

int MetalRenderer2D::GetTargetWidth() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.width) : 0;
}

int MetalRenderer2D::GetTargetHeight() const noexcept
{
    return State && State->OutputTex ? static_cast<int>(State->OutputTex.height) : 0;
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
