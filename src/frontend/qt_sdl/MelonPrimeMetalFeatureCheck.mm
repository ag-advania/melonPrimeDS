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

        // Minimal render pipeline smoke test targeting the format the
        // presenter will use (BGRA8Unorm). This proves pipeline state
        // *creation* succeeds on this device/OS combination; it is never
        // used for actual drawing.
        NSError* error = nil;
        NSString* shaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VOut { float4 position [[position]]; };\n"
             "vertex VOut mp_probe_vs(uint vid [[vertex_id]]) {\n"
             "    float2 pos[3] = { float2(-1,-1), float2(3,-1), float2(-1,3) };\n"
             "    VOut out; out.position = float4(pos[vid], 0, 1); return out;\n"
             "}\n"
             "fragment float4 mp_probe_fs(VOut in [[stage_in]]) {\n"
             "    return float4(0, 0, 0, 0);\n"
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

        id<MTLRenderPipelineState> pipeline =
            [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline)
        {
            info.unavailableReason = "render pipeline state creation failed";
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
