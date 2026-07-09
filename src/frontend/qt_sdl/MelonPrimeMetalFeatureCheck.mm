#if defined(__APPLE__) && defined(MELONPRIME_ENABLE_METAL) // scatter-budget-exempt: Metal build-gate, not input dispatch

#include "MelonPrimeMetalFeatureCheck.h"

#import <Metal/Metal.h>

#include <cstdio>
#include <mutex>

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
                "[MelonPrime] metal probe: device='%s' lowPower=%d removable=%d unifiedMemory=%d recommendedMaxWorkingSetSize=%llu\n",
                info.deviceName.c_str(), info.isLowPower, info.isRemovable, info.hasUnifiedMemory,
                static_cast<unsigned long long>(info.recommendedMaxWorkingSetSize));
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
