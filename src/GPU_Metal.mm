// MelonPrimeDS - Metal renderer with native 3D GetLine integration

#if defined(MELONPRIME_ENABLE_METAL)

// MELONPRIME_METAL_HIRES_VISIBLE_OUTPUT_V1

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "GPU2D_Metal.h"
#include "GPU3D_Metal.h"
#include "GPU3D_MetalCompute.h"
#include "NDS.h"

namespace melonDS
{

namespace
{

bool MetalComputeFoundationEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("MELONPRIME_METAL_COMPUTE_FOUNDATION");
        return value && std::strcmp(value, "1") == 0;
    }();
    return enabled;
}

void* Metal3DColorTarget(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetColorTargetTexture();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetColorTargetTexture();
    return nullptr;
}

void* Metal3DNativeResolveTarget(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetNativeResolveTexture();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetNativeResolveTexture();
    return nullptr;
}

void* Metal3DCommandQueue(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetCommandQueue();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetCommandQueue();
    return nullptr;
}

void* Metal3DPreferredDevice(Renderer3D* renderer) noexcept
{
    id<MTLTexture> texture = (__bridge id<MTLTexture>)Metal3DColorTarget(renderer);
    return texture ? (__bridge void*)texture.device : nullptr;
}

bool Metal3DIsThreaded(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->IsThreaded();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->IsThreaded();
    return false;
}

void Metal3DSetupRenderThread(Renderer3D* renderer)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        compute->SetupRenderThread();
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        raster->SetupRenderThread();
}

void Metal3DEnableRenderThread(Renderer3D* renderer)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        compute->EnableRenderThread();
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        raster->EnableRenderThread();
}

void ConfigureMetal3DRenderer(
    Renderer3D* renderer,
    bool threaded,
    int scale,
    bool betterPolygons)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
    {
        compute->SetThreaded(threaded);
        compute->SetScaleFactor(scale);
        compute->SetBetterPolygons(betterPolygons);
    }
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
    {
        raster->SetThreaded(threaded);
        raster->SetScaleFactor(scale);
        raster->SetBetterPolygons(betterPolygons);
    }
}



static constexpr const char* kMetalVisibleOutputShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VisibleOutputVertex
{
    float4 position [[position]];
};

vertex VisibleOutputVertex mp_visible_output_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    VisibleOutputVertex out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

struct VisibleOutputConfig
{
    uint2 outputSize;
    uint scale;
    uint outputLayer;
    uint engineALayer;
    uint useHighResolution3D;
    uint2 _pad;
};

fragment float4 mp_visible_output_fs(
    VisibleOutputVertex in [[stage_in]],
    constant VisibleOutputConfig& config [[buffer(0)]],
    texture2d_array<float, access::read> cpuComposite [[texture(0)]],
    texture2d<float, access::read> highResolution3D [[texture(1)]],
    texture2d<float, access::read> nativeResolution3D [[texture(2)]])
{
    uint2 outputCoord = min(uint2(in.position.xy), config.outputSize - uint2(1u, 1u));
    uint divisor = max(config.scale, 1u);
    uint2 nativeCoord = min(outputCoord / divisor, uint2(255u, 191u));
    float4 base = cpuComposite.read(nativeCoord, config.outputLayer);

    if (config.useHighResolution3D == 0u || config.scale <= 1u ||
        config.outputLayer != config.engineALayer)
    {
        return float4(base.rgb, 1.0);
    }

    uint2 highCoord = min(outputCoord,
        uint2(highResolution3D.get_width() - 1u, highResolution3D.get_height() - 1u));
    float4 high3D = highResolution3D.read(highCoord);
    float4 low3D = nativeResolution3D.read(nativeCoord);

    // CPU 2D already contains the native 3D layer. Recover the background
    // underneath that native sample, then composite the full-resolution 3D
    // sample over it. This keeps DS BG/OBJ/window/HUD output intact while the
    // geometry and texture edges come from the scaled Metal render target.
    float lowAlpha = clamp(low3D.a, 0.0, 1.0);
    float highAlpha = clamp(high3D.a, 0.0, 1.0);
    float3 background = base.rgb;
    if (lowAlpha < (254.0 / 255.0))
    {
        float denominator = max(1.0 - lowAlpha, 1.0 / 255.0);
        background = clamp((base.rgb - low3D.rgb * lowAlpha) / denominator, 0.0, 1.0);
    }

    float3 result = mix(background, high3D.rgb, highAlpha);
    return float4(clamp(result, 0.0, 1.0), 1.0);
}
)";

} // namespace


struct MetalRenderer::MetalOutputState
{
    static constexpr int SlotCount = 3;

    struct Slot
    {
        id<MTLTexture> CpuComposite = nil;
        id<MTLTexture> FinalTexture = nil;
        bool InFlight = false;
        uint64_t Serial = 0;
        uint64_t Generation = 0;
    };

    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLRenderPipelineState> Pipeline = nil;
    Slot Slots[SlotCount];

    std::mutex Mutex;
    std::condition_variable Completion;
    int InFlightCount = 0;
    int NextSlot = 0;
    int PublishedSlot = -1;
    uint64_t PublishedSerial = 0;
    uint64_t NextSerial = 1;
    uint64_t Generation = 1;
    int Scale = 0;
    NSUInteger Width = 0;
    NSUInteger Height = 0;
    bool Ready = false;
    bool LoggedVisibleOutput = false;
    bool LoggedNoFreeSlot = false;

    ~MetalOutputState()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        Completion.wait(lock, [this]() { return InFlightCount == 0; });
    }
};

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds)
{
    Metal2D_A = std::make_unique<MetalRenderer2D>(GPU.GPU2D_A);
    Metal2D_B = std::make_unique<MetalRenderer2D>(GPU.GPU2D_B);

    if (MetalComputeFoundationEnabled())
    {
        std::fprintf(stderr,
            "[MelonPrime] metal compute foundation: selected developer foundation mode\n");
        Rend3D = std::make_unique<MetalComputeRenderer3D>(GPU.GPU3D, *this);
    }
    else
    {
        Rend3D = std::make_unique<MetalRenderer3D>(GPU.GPU3D, *this);
    }
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: initializing native Metal 3D GetLine integration path\n");
    if (!Rend3D->Init())
        return false;

    void* preferredDevice = Metal3DPreferredDevice(Rend3D.get());
    ConfigureMetal2DMirror(preferredDevice);
    if (!ConfigureMetalVisibleOutput(preferredDevice))
    {
        std::fprintf(stderr,
            "[MelonPrime] metal visible output: initialization failed; CPU output remains available\n");
    }
    return true;
}

void MetalRenderer::PreSavestate()
{
    if (Metal3DIsThreaded(Rend3D.get()))
        Metal3DSetupRenderThread(Rend3D.get());
}

void MetalRenderer::PostSavestate()
{
    if (Metal3DIsThreaded(Rend3D.get()))
        Metal3DEnableRenderThread(Rend3D.get());
}

void MetalRenderer::SetRenderSettings(RendererSettings& settings)
{
    const int scale = std::max(1, settings.ScaleFactor);
    ScaleFactor = scale;

    ConfigureMetal3DRenderer(
        Rend3D.get(), settings.Threaded, scale, settings.BetterPolygons);
    void* preferredDevice = Metal3DPreferredDevice(Rend3D.get());
    ConfigureMetal2DMirror(preferredDevice);
    ConfigureMetalVisibleOutput(preferredDevice);
}

void MetalRenderer::ConfigureMetal2DMirror(void* preferredDevice)
{
    bool okA = !Metal2D_A || Metal2D_A->Configure(preferredDevice, ScaleFactor);
    bool okB = !Metal2D_B || Metal2D_B->Configure(preferredDevice, ScaleFactor);
    if (!okA || !okB)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal 2d: scaffold allocation failed; keeping Phase 2/3 CPU-composited output visible\n");
    }
}


bool MetalRenderer::ConfigureMetalVisibleOutput(void* preferredDevice)
{
    @autoreleasepool
    {
        id<MTLDevice> device = (__bridge id<MTLDevice>)preferredDevice;
        id<MTLCommandQueue> rendererQueue =
            (__bridge id<MTLCommandQueue>)Metal3DCommandQueue(Rend3D.get());
        if (!device || !rendererQueue || rendererQueue.device != device)
            return false;

        if (!OutputState || OutputState->Device != device ||
            OutputState->Queue != rendererQueue)
        {
            OutputState = std::make_unique<MetalOutputState>();
            OutputState->Device = device;
            // Use the renderer's queue. Output composition then sits between
            // completed frame N and 3D writes for frame N+1 without a CPU wait
            // or a cross-queue resource race.
            OutputState->Queue = rendererQueue;

            NSError* error = nil;
            NSString* source = [[NSString alloc] initWithUTF8String:kMetalVisibleOutputShaderSource];
            OutputState->Library = [device newLibraryWithSource:source options:nil error:&error];
            if (!OutputState->Library)
            {
                const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: shader compile failed: %s\n", message);
                return false;
            }

            id<MTLFunction> vertex =
                [OutputState->Library newFunctionWithName:@"mp_visible_output_vs"];
            id<MTLFunction> fragment =
                [OutputState->Library newFunctionWithName:@"mp_visible_output_fs"];
            if (!vertex || !fragment)
                return false;

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.label = @"MelonPrime Metal Visible HiRes Output Pipeline";
            desc.vertexFunction = vertex;
            desc.fragmentFunction = fragment;
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            OutputState->Pipeline =
                [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!OutputState->Pipeline)
            {
                const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: pipeline creation failed: %s\n", message);
                return false;
            }
        }

        id<MTLTexture> high3D =
            (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
        if (!high3D || high3D.device != OutputState->Device)
            return false;

        const NSUInteger width = high3D.width;
        const NSUInteger height = high3D.height;
        const int scale = std::max(1, static_cast<int>(width / 256u));

        std::lock_guard<std::mutex> lock(OutputState->Mutex);
        if (OutputState->Ready && OutputState->Scale == scale &&
            OutputState->Width == width && OutputState->Height == height)
        {
            return true;
        }

        OutputState->Generation++;
        OutputState->PublishedSlot = -1;
        OutputState->PublishedSerial = 0;
        OutputState->Scale = scale;
        OutputState->Width = width;
        OutputState->Height = height;
        OutputState->Ready = false;

        for (int i = 0; i < MetalOutputState::SlotCount; i++)
        {
            MTLTextureDescriptor* cpuDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                   width:256
                                                                  height:192
                                                               mipmapped:NO];
            cpuDesc.textureType = MTLTextureType2DArray;
            cpuDesc.arrayLength = 2;
            cpuDesc.usage = MTLTextureUsageShaderRead;
            cpuDesc.storageMode = MTLStorageModeShared;

            MTLTextureDescriptor* finalDesc =
                [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                   width:width
                                                                  height:height
                                                               mipmapped:NO];
            finalDesc.textureType = MTLTextureType2DArray;
            finalDesc.arrayLength = 2;
            finalDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            finalDesc.storageMode = MTLStorageModePrivate;

            OutputState->Slots[i].CpuComposite =
                [OutputState->Device newTextureWithDescriptor:cpuDesc];
            OutputState->Slots[i].FinalTexture =
                [OutputState->Device newTextureWithDescriptor:finalDesc];
            if (!OutputState->Slots[i].CpuComposite || !OutputState->Slots[i].FinalTexture)
            {
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: texture allocation failed scale=%d size=%zux%zu\n",
                    scale, static_cast<size_t>(width), static_cast<size_t>(height));
                return false;
            }
        }

        OutputState->Ready = true;
        OutputState->LoggedVisibleOutput = false;
        OutputState->LoggedNoFreeSlot = false;
        std::fprintf(stderr,
            "[MelonPrime] metal visible output: configured scale=%d textureArray=%zux%zux2\n",
            scale, static_cast<size_t>(width), static_cast<size_t>(height));
        return true;
    }
}

void MetalRenderer::ComposeMetalVisibleOutput()
{
    @autoreleasepool
    {
        if (!OutputState || !OutputState->Ready)
            return;

        void* top = nullptr;
        void* bottom = nullptr;
        if (!SoftRenderer::GetFramebuffers(&top, &bottom) || !top || !bottom)
            return;

        id<MTLTexture> high3D =
            (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
        id<MTLTexture> low3D =
            (__bridge id<MTLTexture>)Metal3DNativeResolveTarget(Rend3D.get());
        if (!high3D || !low3D || high3D.device != OutputState->Device ||
            low3D.device != OutputState->Device)
        {
            return;
        }

        struct VisibleOutputConfigCpu
        {
            uint32_t outputSize[2];
            uint32_t scale;
            uint32_t outputLayer;
            uint32_t engineALayer;
            uint32_t useHighResolution3D;
            uint32_t pad[2];
        };
        static_assert(sizeof(VisibleOutputConfigCpu) == 32,
            "VisibleOutputConfigCpu must match MSL layout");

        std::unique_lock<std::mutex> lock(OutputState->Mutex);
        int slotIndex = -1;
        for (int attempt = 0; attempt < MetalOutputState::SlotCount; attempt++)
        {
            const int candidate =
                (OutputState->NextSlot + attempt) % MetalOutputState::SlotCount;
            if (!OutputState->Slots[candidate].InFlight)
            {
                slotIndex = candidate;
                break;
            }
        }
        if (slotIndex < 0)
        {
            if (!OutputState->LoggedNoFreeSlot)
            {
                OutputState->LoggedNoFreeSlot = true;
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: all output slots busy; retaining previous frame\n");
            }
            return;
        }

        MetalOutputState::Slot& slot = OutputState->Slots[slotIndex];
        OutputState->NextSlot = (slotIndex + 1) % MetalOutputState::SlotCount;
        OutputState->LoggedNoFreeSlot = false;

        const MTLRegion nativeRegion = MTLRegionMake2D(0, 0, 256, 192);
        [slot.CpuComposite replaceRegion:nativeRegion
                             mipmapLevel:0
                                   slice:0
                               withBytes:top
                             bytesPerRow:256 * 4
                           bytesPerImage:256 * 192 * 4];
        [slot.CpuComposite replaceRegion:nativeRegion
                             mipmapLevel:0
                                   slice:1
                               withBytes:bottom
                             bytesPerRow:256 * 4
                           bytesPerImage:256 * 192 * 4];

        id<MTLCommandBuffer> commandBuffer = [OutputState->Queue commandBuffer];
        if (!commandBuffer)
            return;
        commandBuffer.label = @"MelonPrime Metal Visible HiRes Output";

        const uint32_t engineALayer = GPU.ScreenSwap ? 0u : 1u;
        const uint32_t displayModeA = (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
        const uint32_t useHighResolution3D =
            (GPU.ScreensEnabled && displayModeA == 1u &&
             (GPU.GPU2D_A.DispCnt & (1u << 3)) != 0u) ? 1u : 0u;

        for (uint32_t layer = 0; layer < 2; layer++)
        {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = slot.FinalTexture;
            pass.colorAttachments[0].slice = layer;
            pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLRenderCommandEncoder> encoder =
                [commandBuffer renderCommandEncoderWithDescriptor:pass];
            if (!encoder)
                return;

            VisibleOutputConfigCpu config = {};
            config.outputSize[0] = static_cast<uint32_t>(OutputState->Width);
            config.outputSize[1] = static_cast<uint32_t>(OutputState->Height);
            config.scale = static_cast<uint32_t>(OutputState->Scale);
            config.outputLayer = layer;
            config.engineALayer = engineALayer;
            config.useHighResolution3D = useHighResolution3D;

            [encoder setRenderPipelineState:OutputState->Pipeline];
            [encoder setFragmentBytes:&config length:sizeof(config) atIndex:0];
            [encoder setFragmentTexture:slot.CpuComposite atIndex:0];
            [encoder setFragmentTexture:high3D atIndex:1];
            [encoder setFragmentTexture:low3D atIndex:2];
            [encoder setViewport:(MTLViewport){
                0.0, 0.0,
                static_cast<double>(OutputState->Width),
                static_cast<double>(OutputState->Height),
                0.0, 1.0 }];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];
        }

        const uint64_t generation = OutputState->Generation;
        const uint64_t serial = OutputState->NextSerial++;
        slot.InFlight = true;
        slot.Generation = generation;
        slot.Serial = serial;
        OutputState->InFlightCount++;
        MetalOutputState* state = OutputState.get();

        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            std::lock_guard<std::mutex> completionLock(state->Mutex);
            MetalOutputState::Slot& completedSlot = state->Slots[slotIndex];
            if (completed.status == MTLCommandBufferStatusCompleted &&
                generation == state->Generation &&
                completedSlot.Generation == generation && completedSlot.Serial == serial)
            {
                state->PublishedSlot = slotIndex;
                state->PublishedSerial = serial;
            }
            else if (completed.status == MTLCommandBufferStatusError)
            {
                const char* message = completed.error
                    ? [[completed.error localizedDescription] UTF8String]
                    : "unknown Metal output error";
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: command failed: %s\n", message);
            }
            completedSlot.InFlight = false;
            state->InFlightCount--;
            state->Completion.notify_all();
        }];

        [commandBuffer commit];

        if (!OutputState->LoggedVisibleOutput)
        {
            OutputState->LoggedVisibleOutput = true;
            std::fprintf(stderr,
                "[MelonPrime] metal visible output: first compose scale=%d size=%zux%zu engineALayer=%u high3D=%u\n",
                OutputState->Scale,
                static_cast<size_t>(OutputState->Width),
                static_cast<size_t>(OutputState->Height),
                engineALayer, useHighResolution3D);
        }
    }
}

void MetalRenderer::VBlank()
{
    SoftRenderer::VBlank();
}

void MetalRenderer::SwapBuffers()
{
    Renderer::SwapBuffers();
    ComposeMetalVisibleOutput();
}

RendererOutput MetalRenderer::GetOutput()
{
    if (OutputState)
    {
        std::lock_guard<std::mutex> lock(OutputState->Mutex);
        if (OutputState->Ready && OutputState->PublishedSlot >= 0)
        {
            const int slotIndex = OutputState->PublishedSlot;
            id<MTLTexture> texture = OutputState->Slots[slotIndex].FinalTexture;
            if (texture)
                return RendererOutput::MetalTexture(
                    (__bridge void*)texture, OutputState->PublishedSerial);
        }
    }
    return SoftRenderer::GetOutput();
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
