#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#include "MelonPrimeMetalFeatureCheck.h"

#import <Metal/Metal.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace MelonPrime::Metal {

namespace {

FeatureInfo ProbeFeatureInfo()
{
    FeatureInfo info;

    @autoreleasepool
    {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (!device)
        {
            info.unavailableReason = "MTLCreateSystemDefaultDevice() returned nil";
            return info;
        }

        info.hasDevice = true;
        info.isLowPower = device.isLowPower;
        info.isRemovable = device.isRemovable;
        info.hasUnifiedMemory = device.hasUnifiedMemory;
        info.recommendedMaxWorkingSetSize = device.recommendedMaxWorkingSetSize;
        info.deviceName = device.name ? [device.name UTF8String] : "";

        id<MTLCommandQueue> queue = [device newCommandQueue];
        if (!queue)
        {
            info.unavailableReason = "newCommandQueue failed";
            return info;
        }

        // Render pipeline smoke test matching the exact attachment shape
        // MetalRenderer3D::BuildClearPipeline() uses (GPU3D_Metal.mm):
        // color0=BGRA8Unorm (presenter format), color1=RGBA8Unorm (attribute
        // target), depth/stencil=Depth32Float_Stencil8 (combined format).
        // This used to probe only a single BGRA8 color attachment, which
        // could report "baseline supported" on a device/driver combination
        // that then failed inside the real Phase 8 pipeline creation --
        // SupportsRequiredBaseline() is documented as the gate every later
        // phase must check, so it needs to actually predict that outcome.
        NSError* error = nil;
        NSString* shaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VOut { float4 position [[position]]; };\n"
             "vertex VOut mp_probe_vs(uint vid [[vertex_id]]) {\n"
             "    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
             "    VOut out; out.position = float4(pos[vid], 0, 1); return out;\n"
             "}\n"
             "struct FOut { float4 color [[color(0)]]; float4 attr [[color(1)]]; };\n"
             "fragment FOut mp_probe_fs() {\n"
             "    FOut out; out.color = float4(0, 0, 0, 0); out.attr = float4(0, 0, 0, 0); return out;\n"
             "}\n";

        id<MTLLibrary> library = [device newLibraryWithSource:shaderSource options:nil error:&error];
        if (!library)
        {
            info.unavailableReason = "probe shader library compile failed";
            return info;
        }

        id<MTLFunction> vertexFn = [library newFunctionWithName:@"mp_probe_vs"];
        id<MTLFunction> fragmentFn = [library newFunctionWithName:@"mp_probe_fs"];
        if (!vertexFn || !fragmentFn)
        {
            info.unavailableReason = "probe shader functions missing after compile";
            return info;
        }

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vertexFn;
        desc.fragmentFunction = fragmentFn;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        desc.colorAttachments[1].pixelFormat = MTLPixelFormatRGBA8Unorm;
        desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;

        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            info.unavailableReason = "render pipeline state creation failed";
            return info;
        }

        MTLDepthStencilDescriptor* dsDesc = [[MTLDepthStencilDescriptor alloc] init];
        dsDesc.depthCompareFunction = MTLCompareFunctionAlways;
        dsDesc.depthWriteEnabled = YES;
        id<MTLDepthStencilState> depthStencilState =
            [device newDepthStencilStateWithDescriptor:dsDesc];
        if (!depthStencilState)
        {
            info.unavailableReason = "depth/stencil state creation failed";
            return info;
        }

        // Pipeline *descriptor* validation only proves the format
        // combination is legal for a render pipeline; actually allocating a
        // private-storage texture in the combined depth/stencil format
        // catches drivers where the format is legal to declare but not to
        // allocate. 1x1 so the probe stays cheap.
        MTLTextureDescriptor* depthTexDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float_Stencil8
                                                               width:1
                                                              height:1
                                                           mipmapped:NO];
        depthTexDesc.usage = MTLTextureUsageRenderTarget;
        depthTexDesc.storageMode = MTLStorageModePrivate;
        id<MTLTexture> depthProbeTexture = [device newTextureWithDescriptor:depthTexDesc];
        if (!depthProbeTexture)
        {
            info.unavailableReason = "Depth32Float_Stencil8 texture allocation failed";
            return info;
        }

        // Phase 8 texturing probe: TexcacheMetalLoader (GPU3D_Metal.mm) and
        // the opaque-polygon fragment shader both depend on
        // MTLTextureType2DArray + MTLPixelFormatRGBA8Uint allocation, upload,
        // and texture2d_array<uint> sampling through a nearest sampler. The
        // clear-pipeline probe above never exercises any of that, so a
        // device/driver that fails specifically at uint-array sampling would
        // have reported "baseline supported" and then failed inside the real
        // textured pass. This does a full round trip -- not just pipeline
        // creation -- so it actually predicts that outcome: allocate a 2x2
        // array texture, upload a known texel, sample it in a real draw, and
        // confirm the read-back pixel matches.
        MTLTextureDescriptor* texArrayDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Uint
                                                               width:2
                                                              height:2
                                                           mipmapped:NO];
        texArrayDesc.textureType = MTLTextureType2DArray;
        texArrayDesc.arrayLength = 1;
        texArrayDesc.usage = MTLTextureUsageShaderRead;
        texArrayDesc.storageMode = MTLStorageModeShared;
        id<MTLTexture> texArray = [device newTextureWithDescriptor:texArrayDesc];
        if (!texArray)
        {
            info.unavailableReason = "MTLTextureType2DArray + MTLPixelFormatRGBA8Uint allocation failed";
            return info;
        }

        uint8_t probeTexel[4] = { 32, 63, 0, 31 }; // arbitrary non-white/non-black RGB6A5 pattern
        uint8_t probeTexels[2 * 2 * 4];
        for (int i = 0; i < 4; i++)
            memcpy(&probeTexels[i * 4], probeTexel, 4);
        [texArray replaceRegion:MTLRegionMake2D(0, 0, 2, 2)
                     mipmapLevel:0
                           slice:0
                       withBytes:probeTexels
                     bytesPerRow:2 * 4
                   bytesPerImage:2 * 2 * 4];

        MTLSamplerDescriptor* samplerDesc = [[MTLSamplerDescriptor alloc] init];
        samplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
        samplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:samplerDesc];
        if (!sampler)
        {
            info.unavailableReason = "nearest/clamp sampler state creation failed";
            return info;
        }

        NSString* texSampleShaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VOut { float4 position [[position]]; };\n"
             "vertex VOut mp_probe_tex_vs(uint vid [[vertex_id]]) {\n"
             "    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
             "    VOut out; out.position = float4(pos[vid], 0, 1); return out;\n"
             "}\n"
             "fragment float4 mp_probe_tex_fs(texture2d_array<uint> tex [[texture(0)]],\n"
             "                                  sampler smp [[sampler(0)]]) {\n"
             "    uint4 raw = tex.sample(smp, float2(0.5, 0.5), 0u);\n"
             "    return float4(raw) / float4(63.0, 63.0, 63.0, 31.0);\n"
             "}\n";

        error = nil;
        id<MTLLibrary> texLibrary = [device newLibraryWithSource:texSampleShaderSource options:nil error:&error];
        if (!texLibrary)
        {
            const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
            info.unavailableReason = std::string("texture-array sampling probe shader compile failed: ") + message;
            return info;
        }

        id<MTLFunction> texVertexFn = [texLibrary newFunctionWithName:@"mp_probe_tex_vs"];
        id<MTLFunction> texFragmentFn = [texLibrary newFunctionWithName:@"mp_probe_tex_fs"];
        if (!texVertexFn || !texFragmentFn)
        {
            info.unavailableReason = "texture-array sampling probe shader entry point missing";
            return info;
        }

        MTLRenderPipelineDescriptor* texDesc = [[MTLRenderPipelineDescriptor alloc] init];
        texDesc.vertexFunction = texVertexFn;
        texDesc.fragmentFunction = texFragmentFn;
        texDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;

        error = nil;
        id<MTLRenderPipelineState> texPipeline =
            [device newRenderPipelineStateWithDescriptor:texDesc error:&error];
        if (!texPipeline)
        {
            const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
            info.unavailableReason = std::string("texture-array sampling pipeline creation failed: ") + message;
            return info;
        }

        MTLTextureDescriptor* colorDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:2
                                                              height:2
                                                           mipmapped:NO];
        colorDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        colorDesc.storageMode = MTLStorageModeShared;
        id<MTLTexture> colorTarget = [device newTextureWithDescriptor:colorDesc];
        if (!colorTarget)
        {
            info.unavailableReason = "texture-array sampling probe color target allocation failed";
            return info;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = colorTarget;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

        id<MTLCommandBuffer> texCommandBuffer = [queue commandBuffer];
        id<MTLRenderCommandEncoder> texEncoder =
            texCommandBuffer ? [texCommandBuffer renderCommandEncoderWithDescriptor:pass] : nil;
        if (!texCommandBuffer || !texEncoder)
        {
            info.unavailableReason = "texture-array sampling probe command encoder creation failed";
            return info;
        }

        [texEncoder setRenderPipelineState:texPipeline];
        [texEncoder setFragmentTexture:texArray atIndex:0];
        [texEncoder setFragmentSamplerState:sampler atIndex:0];
        [texEncoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [texEncoder endEncoding];
        [texCommandBuffer commit];
        [texCommandBuffer waitUntilCompleted];

        if (texCommandBuffer.status != MTLCommandBufferStatusCompleted)
        {
            info.unavailableReason = "texture-array sampling probe draw did not complete";
            return info;
        }

        uint8_t sampledPixel[4] = { 0, 0, 0, 0 };
        [colorTarget getBytes:sampledPixel bytesPerRow:4 fromRegion:MTLRegionMake2D(0, 0, 1, 1) mipmapLevel:0];
        // BGRA8Unorm byte order: B, G, R, A. Expected from probeTexel
        // (R=32/63, G=63/63, B=0/63, A=31/31): B=0, G=255, R=~130, A=255.
        const bool sampledCorrectly =
            sampledPixel[0] == 0 && sampledPixel[1] == 255 &&
            std::abs(static_cast<int>(sampledPixel[2]) - 130) <= 3 && sampledPixel[3] == 255;
        if (!sampledCorrectly)
        {
            info.unavailableReason = "texture-array sampling probe produced an unexpected pixel value";
            return info;
        }

        info.supportsTextureArraySampling = true;
        info.supportsRequiredBaseline = true;
    }

    return info;
}

FeatureInfo& MutableCachedInfo()
{
    static FeatureInfo s_info;
    return s_info;
}

std::once_flag& ProbeOnceFlag()
{
    static std::once_flag s_flag;
    return s_flag;
}

} // namespace

const FeatureInfo& CachedFeatureInfo()
{
    std::call_once(ProbeOnceFlag(), []() { MutableCachedInfo() = ProbeFeatureInfo(); });
    return MutableCachedInfo();
}

bool IsRuntimeAvailable()
{
    return CachedFeatureInfo().hasDevice;
}

bool SupportsRequiredBaseline()
{
    return CachedFeatureInfo().supportsRequiredBaseline;
}

void LogFeatureInfoOnce()
{
    static std::once_flag s_logFlag;
    std::call_once(s_logFlag, []() {
        const FeatureInfo& info = CachedFeatureInfo();
        if (info.supportsRequiredBaseline)
        {
            fprintf(stderr,
                "[MelonPrime] metal probe: device='%s' lowPower=%d removable=%d unifiedMemory=%d "
                "recommendedMaxWorkingSetSize=%llu textureArraySampling=%d\n",
                info.deviceName.c_str(), info.isLowPower, info.isRemovable, info.hasUnifiedMemory,
                static_cast<unsigned long long>(info.recommendedMaxWorkingSetSize),
                info.supportsTextureArraySampling);
        }
        else
        {
            fprintf(stderr, "[MelonPrime] metal probe: unavailable (%s)\n",
                info.unavailableReason.c_str());
        }
    });
}

} // namespace MelonPrime::Metal

#endif // MELONPRIME_ENABLE_METAL (Apple-only gate above)
