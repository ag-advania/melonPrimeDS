// MelonPrimeDS - shared Metal texture-cache loader for raster and compute 3D renderers

#ifndef GPU3D_TEXCACHE_METAL_H
#define GPU3D_TEXCACHE_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include <cstdio>

#include "GPU3D_Texcache.h"

namespace melonDS
{

class TexcacheMetalLoader
{
public:
    explicit TexcacheMetalLoader(id<MTLDevice> device) noexcept
        : Device(device)
    {
    }

    id<MTLTexture> GenerateTexture(u32 width, u32 height, u32 layers)
    {
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
        // ARC releases the strong id<MTLTexture> references held by Texcache.
    }

private:
    id<MTLDevice> Device = nil;
};

using TexcacheMetal = Texcache<TexcacheMetalLoader, id<MTLTexture>>;

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU3D_TEXCACHE_METAL_H
