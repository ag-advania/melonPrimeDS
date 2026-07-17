// MelonPrimeDS - Metal renderer with native 3D GetLine integration

#if defined(MELONPRIME_ENABLE_METAL)

// MELONPRIME_METAL_HIRES_VISIBLE_OUTPUT_V1
// MELONPRIME_METAL_HIRES_SCALE_AUTHORITY_V2
// MELONPRIME_METAL_COMPUTE_HIRES_LATCH_V1
// MELONPRIME_METAL_OUTPUT_LEASE_V1
// MELONPRIME_METAL_MASTER_BRIGHTNESS_V1
// MELONPRIME_METAL_FRAME_SNAPSHOT_V1
// MELONPRIME_METAL_VISIBLE_3D_OWNERSHIP_GATE_V1
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_DIRECT_SEGMENTED_CUTOVER_V2
// MELONPRIME_METAL_2D_HOT_PATH_CLEANUP_V1
// MELONPRIME_METAL_2D_SHADOW_PATH_REMOVAL_V1
// MELONPRIME_METAL_COMPUTE_TEXTURED_RASTER_V1

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

#include "GPU2D_Metal.h"
#include "GPU3D_Metal.h"
#include "GPU3D_MetalCompute.h"
#include "GPU_MetalStrictDiagnostics.h"
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

bool MetalFullGpuStatsEnabled()
{
    static const bool enabled = []() {
        const char* value = std::getenv("MELONPRIME_METAL_PERF");
        return value && value[0] == '1';
    }();
    return enabled;
}

// Phase M0 diagnostics: how each VBlank's visible output was actually
// produced this session, reported every 600 frames. Mirrors the
// MetalPerfAccumulator pattern already used by GPU3D_Metal.mm/
// GPU3D_MetalCompute.mm so the three subsystems can be compared side by side
// in the same log stream.
struct MetalFullGpuFrameStats
{
    uint32_t Frames = 0;
    uint32_t FullGpuFrames = 0;
    uint32_t CpuCompositeFrames = 0;
    uint32_t RetainPreviousFrames = 0;
    uint32_t BlockedByCaptureFeedbackFrames = 0;
};

void MetalFullGpuFrameStatsRecord(
    MetalFullGpuFrameStats& stats,
    bool fullGpu, bool cpuComposite, bool retainPrevious, bool blockedByCapture)
{
    if (!MetalFullGpuStatsEnabled())
        return;

    stats.Frames++;
    if (fullGpu) stats.FullGpuFrames++;
    if (cpuComposite) stats.CpuCompositeFrames++;
    if (retainPrevious) stats.RetainPreviousFrames++;
    if (blockedByCapture) stats.BlockedByCaptureFeedbackFrames++;

    constexpr uint32_t kReportFrames = 600;
    if (stats.Frames < kReportFrames)
        return;

    std::fprintf(stderr,
        "[MelonPrime] metal renderer: visible-source mix frames=%u "
        "fullGpu=%u/%u cpuComposite=%u/%u retainPrevious=%u/%u "
        "blockedByCaptureFeedback=%u/%u\n",
        stats.Frames,
        stats.FullGpuFrames, stats.Frames,
        stats.CpuCompositeFrames, stats.Frames,
        stats.RetainPreviousFrames, stats.Frames,
        stats.BlockedByCaptureFeedbackFrames, stats.Frames);

    stats = {};
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

bool Metal3DLastFrameUsesHighResolution3D(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->LastFrameUsesHighResolution3D();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->LastFrameUsesHighResolution3D();
    return false;
}

uint32_t Metal3DLastFrameEngineALayer(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetLastFrameEngineALayer();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetLastFrameEngineALayer();
    return 1u;
}

int Metal3DLastFrameRenderedScale(Renderer3D* renderer) noexcept
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        return compute->GetLastFrameRenderedScale();
    if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        return raster->GetLastFrameRenderedScale();
    return 1;
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

void Metal3DSetCpuReadbackRequired(Renderer3D* renderer, bool required)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
        compute->SetCpuReadbackRequired(required);
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
        raster->SetCpuReadbackRequired(required);
}

void Metal3DSetCaptureTextures(
    Renderer3D* renderer,
    void* capture128Texture,
    void* capture256Texture)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
    {
        compute->SetCaptureTextures(
            capture128Texture,
            capture256Texture);
    }
}

void ConfigureMetal3DRenderer(
    Renderer3D* renderer,
    bool threaded,
    int scale,
    bool highResolutionCoordinates,
    bool betterPolygons)
{
    if (auto* compute = dynamic_cast<MetalComputeRenderer3D*>(renderer))
    {
        compute->SetThreaded(threaded);
        compute->SetScaleFactor(scale);
        compute->SetHighResolutionCoordinates(highResolutionCoordinates);
        compute->SetBetterPolygons(betterPolygons);
    }
    else if (auto* raster = dynamic_cast<MetalRenderer3D*>(renderer))
    {
        raster->SetThreaded(threaded);
        raster->SetScaleFactor(scale);
        raster->SetHighResolutionCoordinates(highResolutionCoordinates);
        raster->SetBetterPolygons(betterPolygons);
        // MELONPRIME_METAL_RENDER_OPTIONS_V1
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
    uint masterBrightnessA;
    uint masterBrightnessB;
};

static inline float3 mp_apply_master_brightness(float3 color, uint reg)
{
    uint mode = reg >> 14;
    float factor = float(min(reg & 0x1Fu, 16u)) / 16.0;
    if (mode == 1u)
        return mix(color, float3(1.0), factor);
    if (mode == 2u)
        return color * (1.0 - factor);
    return color;
}

// Reproduce the exact Metal readback -> DS 6-bit -> CPU master-brightness ->
// BGRA8 expansion path. The final CPU composite can then be compared against
// the native 3D sample in the same quantized color space.
static inline float3 mp_cpu_native_3d_reference(
    float3 color,
    uint reg)
{
    uint3 color8 = uint3(
        clamp(color, float3(0.0), float3(1.0)) * 255.0 + 0.5);
    uint3 color6 =
        (color8 * uint3(63u) + uint3(127u)) / uint3(255u);

    uint mode = reg >> 14u;
    uint factor = min(reg & 0x1Fu, 16u);
    if (mode == 1u)
    {
        color6 +=
            ((uint3(63u) - color6) * factor) >> 4u;
    }
    else if (mode == 2u)
    {
        color6 -=
            ((color6 * factor + uint3(15u)) >> 4u);
    }

    uint3 expanded = (color6 << 2u) | (color6 >> 4u);
    return float3(expanded) / 255.0;
}

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
        uint2(highResolution3D.get_width() - 1u,
              highResolution3D.get_height() - 1u));
    float4 high3D = highResolution3D.read(highCoord);
    float4 low3D = nativeResolution3D.read(nativeCoord);

    uint brightness = (config.outputLayer == config.engineALayer)
        ? config.masterBrightnessA
        : config.masterBrightnessB;
    float3 brightHigh3D =
        mp_apply_master_brightness(high3D.rgb, brightness);
    float lowAlpha = clamp(low3D.a, 0.0, 1.0);
    float highAlpha = clamp(high3D.a, 0.0, 1.0);

    // Mode 1 is the normal ownership-gated path. Only replace a subpixel when:
    //   1. native and high-resolution 3D are both fully opaque, and
    //   2. the completed CPU screen pixel matches the exact native 3D color.
    // A BG/OBJ HUD, reticle, window, blend or brightness effect changes the CPU
    // result, so that pixel remains untouched. This prevents opaque 3D clear
    // pixels from overwriting the already-correct Software 2D composite.
    if (config.useHighResolution3D == 1u)
    {
        constexpr float opaqueThreshold = 30.5 / 31.0;
        if (lowAlpha < opaqueThreshold || highAlpha < opaqueThreshold)
            return float4(base.rgb, 1.0);

        float3 expectedNative3D =
            mp_cpu_native_3d_reference(low3D.rgb, brightness);
        constexpr float ownershipTolerance = 2.0 / 255.0;
        if (any(abs(base.rgb - expectedNative3D) >
                float3(ownershipTolerance)))
        {
            return float4(base.rgb, 1.0);
        }

        return float4(clamp(brightHigh3D, 0.0, 1.0), 1.0);
    }

    // Mode 2 is retained only for A/B diagnostics. It reproduces the previous
    // unconditional replacement behavior and may hide native 2D overlays.
    low3D.rgb = mp_apply_master_brightness(low3D.rgb, brightness);
    float3 background = base.rgb;
    if (lowAlpha < (254.0 / 255.0))
    {
        float denominator = max(1.0 - lowAlpha, 1.0 / 255.0);
        background = clamp(
            (base.rgb - low3D.rgb * lowAlpha) / denominator,
            0.0,
            1.0);
    }

    float3 result = mix(background, brightHigh3D, highAlpha);
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
        int PresenterRefs = 0;
        uint64_t Serial = 0;
        uint64_t Generation = 0;
    };

    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLRenderPipelineState> Pipeline = nil;
    id<MTLTexture> CapturedHigh3D = nil;
    id<MTLTexture> CapturedLow3D = nil;
    bool CapturedFrameReady = false;
    int CapturedScale = 1;
    uint32_t CapturedEngineALayer = 1;
    uint32_t CapturedUseHighResolution3D = 0;
    uint64_t CapturedSerial = 0;
    Slot Slots[SlotCount];

    std::mutex Mutex;
    std::condition_variable Completion;
    int InFlightCount = 0;
    int PresenterRefCount = 0;
    int NextSlot = 0;
    int PublishedSlot = -1;
    uint64_t PublishedSerial = 0;
    uint64_t NextSerial = 1;
    uint64_t Generation = 1;
    // Assigned once at construction; never reused across OutputState
    // instances. See MELONPRIME_METAL_OUTPUT_PRODUCER_ID_V1 on RendererOutput.
    uint64_t ProducerId = 0;
    int Scale = 0;
    NSUInteger Width = 0;
    NSUInteger Height = 0;
    bool Ready = false;
    bool LoggedVisibleOutput = false;
    bool LoggedNoFreeSlot = false;

    MetalOutputState()
    {
        static std::atomic<uint64_t> nextProducerId { 1 };
        ProducerId = nextProducerId.fetch_add(1, std::memory_order_relaxed);
    }

    ~MetalOutputState()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        Completion.wait(lock, [this]() {
            return InFlightCount == 0 && PresenterRefCount == 0;
        });
    }
};

#include "GPU_MetalFullGpuMethods.inc"
#include "GPU_MetalCaptureMethods.inc"

MetalRenderer::MetalRenderer(melonDS::NDS& nds, bool useComputeRenderer) noexcept
    : SoftRenderer(nds)
{
    ComputeRendererSelected = useComputeRenderer;
    Metal2D_A = std::make_unique<MetalRenderer2D>(GPU.GPU2D_A);
    Metal2D_B = std::make_unique<MetalRenderer2D>(GPU.GPU2D_B);

    if (useComputeRenderer || MetalComputeFoundationEnabled())
    {
        std::fprintf(stderr,
            useComputeRenderer
                ? "[MelonPrime] metal compute: selected from Video Settings\n"
                : "[MelonPrime] metal compute foundation: selected developer foundation mode\n");
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
    if (!InitializeMetalFullGpuOutput())
    {
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu: initialization failed; stable CPU compositor path remains active\n");
    }
    if (!ConfigureMetalCaptureState(preferredDevice))
    {
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: initialization failed; "
            "capture frames remain on the CPU path\n");
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
        Rend3D.get(), settings.Threaded, scale,
        settings.HiresCoordinates, settings.BetterPolygons);

    // The compute wrapper keeps the validated Metal raster renderer as the
    // visible source. Treat the requested setting as authoritative and verify
    // the actual raster target before creating the final two-screen texture.
    id<MTLTexture> high3D =
        (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
    const NSUInteger expectedWidth = static_cast<NSUInteger>(256 * scale);
    const NSUInteger expectedHeight = static_cast<NSUInteger>(192 * scale);
    if (high3D && (high3D.width != expectedWidth || high3D.height != expectedHeight))
    {
        // One retry closes ordering gaps between renderer initialization and
        // the first settings update without recreating the whole renderer.
        ConfigureMetal3DRenderer(
            Rend3D.get(), settings.Threaded, scale,
            settings.HiresCoordinates, settings.BetterPolygons);
        high3D = (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
    }

    if (!high3D || high3D.width != expectedWidth || high3D.height != expectedHeight)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal hires: target mismatch renderer=%s requested=%dx actual=%zux%zu expected=%zux%zu\n",
            ComputeRendererSelected ? "MetalCompute" : "Metal",
            scale,
            high3D ? static_cast<size_t>(high3D.width) : 0u,
            high3D ? static_cast<size_t>(high3D.height) : 0u,
            static_cast<size_t>(expectedWidth),
            static_cast<size_t>(expectedHeight));
    }

    void* preferredDevice = Metal3DPreferredDevice(Rend3D.get());
    ConfigureMetal2DMirror(preferredDevice);
    ConfigureMetalVisibleOutput(preferredDevice);
    ConfigureMetalCaptureState(preferredDevice);
}

void MetalRenderer::ConfigureMetal2DMirror(void* preferredDevice)
{
    void* preferredQueue = Metal3DCommandQueue(Rend3D.get());
    bool okA = !Metal2D_A ||
        Metal2D_A->Configure(preferredDevice, preferredQueue, ScaleFactor);
    bool okB = !Metal2D_B ||
        Metal2D_B->Configure(preferredDevice, preferredQueue, ScaleFactor);
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

        id<MTLTexture> high3D =
            (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
        if (!high3D || high3D.device != device)
            return false;

        // Do not derive the requested scale from a stale 1x target. The UI/config
        // setting is authoritative; the target must match it before the output
        // state is allowed to publish a Metal texture.
        const int scale = std::max(1, ScaleFactor);
        const NSUInteger width = static_cast<NSUInteger>(256 * scale);
        const NSUInteger height = static_cast<NSUInteger>(192 * scale);
        if (high3D.width != width || high3D.height != height)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal visible output: refusing stale target renderer=%s requestedScale=%d actual=%zux%zu expected=%zux%zu\n",
                ComputeRendererSelected ? "MetalCompute" : "Metal",
                scale,
                static_cast<size_t>(high3D.width),
                static_cast<size_t>(high3D.height),
                static_cast<size_t>(width),
                static_cast<size_t>(height));
            return false;
        }

        // Fast path: already configured for this device/queue/scale.
        if (OutputState && OutputState->Ready &&
            OutputState->Device == device &&
            OutputState->Queue == rendererQueue &&
            OutputState->Scale == scale &&
            OutputState->Width == width &&
            OutputState->Height == height)
        {
            return true;
        }

        // MELONPRIME_METAL_OUTPUT_STATE_SWAP_RECONFIGURE_V1: never rebuild an
        // existing MetalOutputState in place while waiting for PresenterRefCount
        // to reach 0. The presenter may retain lastGoodMetalLease across the
        // CpuBgra window that follows Ready=false, which made the old wait a
        // permanent deadlock on every internal-resolution change. Build a fresh
        // state (new ProducerId), swap the shared_ptr, and let leases /
        // completion handlers drain the previous state naturally.
        auto next = std::make_shared<MetalOutputState>();
        next->Device = device;
        // Use the renderer's queue. Output composition then sits between
        // completed frame N and 3D writes for frame N+1 without a CPU wait
        // or a cross-queue resource race.
        next->Queue = rendererQueue;

        if (OutputState && OutputState->Device == device &&
            OutputState->Library && OutputState->Pipeline)
        {
            next->Library = OutputState->Library;
            next->Pipeline = OutputState->Pipeline;
        }
        else
        {
            NSError* error = nil;
            NSString* source = [[NSString alloc] initWithUTF8String:kMetalVisibleOutputShaderSource];
            next->Library = [device newLibraryWithSource:source options:nil error:&error];
            if (!next->Library)
            {
                const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: shader compile failed: %s\n", message);
                return false;
            }

            id<MTLFunction> vertex =
                [next->Library newFunctionWithName:@"mp_visible_output_vs"];
            id<MTLFunction> fragment =
                [next->Library newFunctionWithName:@"mp_visible_output_fs"];
            if (!vertex || !fragment)
                return false;

            MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
            desc.label = @"MelonPrime Metal Visible HiRes Output Pipeline";
            desc.vertexFunction = vertex;
            desc.fragmentFunction = fragment;
            desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
            next->Pipeline =
                [device newRenderPipelineStateWithDescriptor:desc error:&error];
            if (!next->Pipeline)
            {
                const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: pipeline creation failed: %s\n", message);
                return false;
            }
        }

        next->Scale = scale;
        next->Width = width;
        next->Height = height;
        next->CapturedFrameReady = false;

        MTLTextureDescriptor* capturedHighDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:width
                                                              height:height
                                                           mipmapped:NO];
        capturedHighDesc.usage = MTLTextureUsageShaderRead;
        capturedHighDesc.storageMode = MTLStorageModePrivate;

        MTLTextureDescriptor* capturedLowDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:256
                                                              height:192
                                                           mipmapped:NO];
        capturedLowDesc.usage = MTLTextureUsageShaderRead;
        capturedLowDesc.storageMode = MTLStorageModePrivate;

        next->CapturedHigh3D =
            [next->Device newTextureWithDescriptor:capturedHighDesc];
        next->CapturedLow3D =
            [next->Device newTextureWithDescriptor:capturedLowDesc];
        if (!next->CapturedHigh3D || !next->CapturedLow3D)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal visible output: frame snapshot allocation failed scale=%d\n",
                scale);
            return false;
        }

        for (int i = 0; i < MetalOutputState::SlotCount; i++)
        {
            MetalOutputState::Slot& outputSlot = next->Slots[i];
            outputSlot.InFlight = false;
            outputSlot.PresenterRefs = 0;
            outputSlot.Serial = 0;
            outputSlot.Generation = next->Generation;

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

            outputSlot.CpuComposite =
                [next->Device newTextureWithDescriptor:cpuDesc];
            outputSlot.FinalTexture =
                [next->Device newTextureWithDescriptor:finalDesc];
            if (!outputSlot.CpuComposite || !outputSlot.FinalTexture)
            {
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: texture allocation failed scale=%d size=%zux%zu\n",
                    scale, static_cast<size_t>(width), static_cast<size_t>(height));
                return false;
            }
        }

        next->Ready = true;
        next->LoggedVisibleOutput = false;
        next->LoggedNoFreeSlot = false;

        const uint64_t previousProducerId =
            OutputState ? OutputState->ProducerId : 0;
        std::shared_ptr<MetalOutputState> previous =
            std::exchange(OutputState, std::move(next));
        // Drop the local previous reference without blocking. In-flight compose
        // completions and presenter leases keep the old state alive via their
        // own shared_ptr until PresenterRefCount/InFlightCount drain.
        (void)previous;

        std::fprintf(stderr,
            "[MelonPrime] metal visible output: configured scale=%d textureArray=%zux%zux2 "
            "producerId=%llu previousProducerId=%llu (swap-reconfigure, no presenter wait)\n",
            scale, static_cast<size_t>(width), static_cast<size_t>(height),
            static_cast<unsigned long long>(OutputState->ProducerId),
            static_cast<unsigned long long>(previousProducerId));
        return true;
    }
}

void MetalRenderer::Finish3DRendering()
{
    Renderer::Finish3DRendering();
    if (!(FullGpuState && FullGpuState->FrameActive))
        CaptureMetalVisible3DFrame();
}

void MetalRenderer::CaptureMetalVisible3DFrame()
{
    @autoreleasepool
    {
        std::shared_ptr<MetalOutputState> state = OutputState;
        if (!state || !state->Ready)
            return;

        id<MTLTexture> liveHigh3D =
            (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
        id<MTLTexture> liveLow3D =
            (__bridge id<MTLTexture>)Metal3DNativeResolveTarget(Rend3D.get());
        if (!liveHigh3D || !liveLow3D ||
            liveHigh3D.device != state->Device ||
            liveLow3D.device != state->Device)
        {
            return;
        }

        const int renderedScale = Metal3DLastFrameRenderedScale(Rend3D.get());
        const uint32_t displayModeA = (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
        const bool engineA3DEnabled = (GPU.GPU2D_A.DispCnt & (1u << 3)) != 0u;
        const uint32_t useHighResolution3D =
            (state->Scale > 1 &&
             renderedScale == state->Scale &&
             GPU.ScreensEnabled &&
             displayModeA == 1u &&
             engineA3DEnabled &&
             GPU.GPU3D.RenderNumPolygons > 0 &&
             !GPU.GPU3D.AbortFrame) ? 1u : 0u;

        std::lock_guard<std::mutex> lock(state->Mutex);
        state->CapturedFrameReady = false;
        state->CapturedScale = renderedScale;
        state->CapturedEngineALayer = GPU.ScreenSwap ? 0u : 1u;
        state->CapturedUseHighResolution3D = useHighResolution3D;
        state->CapturedSerial++;

        FrameMasterBrightnessA = GPU.MasterBrightnessA;
        FrameMasterBrightnessB = GPU.MasterBrightnessB;

        if (!useHighResolution3D)
        {
            state->CapturedFrameReady = true;
            return;
        }

        if (!state->CapturedHigh3D || !state->CapturedLow3D ||
            liveHigh3D.width != state->CapturedHigh3D.width ||
            liveHigh3D.height != state->CapturedHigh3D.height ||
            liveLow3D.width != state->CapturedLow3D.width ||
            liveLow3D.height != state->CapturedLow3D.height)
        {
            return;
        }

        id<MTLCommandBuffer> commandBuffer = [state->Queue commandBuffer];
        if (!commandBuffer)
            return;
        commandBuffer.label = @"MelonPrime Metal Current-Frame 3D Snapshot";

        id<MTLBlitCommandEncoder> blit = [commandBuffer blitCommandEncoder];
        if (!blit)
            return;

        [blit copyFromTexture:liveHigh3D
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(liveHigh3D.width, liveHigh3D.height, 1)
                   toTexture:state->CapturedHigh3D
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blit copyFromTexture:liveLow3D
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(liveLow3D.width, liveLow3D.height, 1)
                   toTexture:state->CapturedLow3D
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];

        [blit endEncoding];
        [commandBuffer commit];

        // The copy is submitted on the renderer queue before VCount 215 starts
        // rendering frame N+1 into the live targets. Queue ordering therefore
        // preserves frame N without a CPU wait.
        state->CapturedFrameReady = true;
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

        id<MTLTexture> liveHigh3D =
            (__bridge id<MTLTexture>)Metal3DColorTarget(Rend3D.get());
        if (!liveHigh3D || liveHigh3D.device != OutputState->Device)
            return;

        const NSUInteger expectedWidth =
            static_cast<NSUInteger>(256 * std::max(1, ScaleFactor));
        const NSUInteger expectedHeight =
            static_cast<NSUInteger>(192 * std::max(1, ScaleFactor));
        if (liveHigh3D.width != expectedWidth ||
            liveHigh3D.height != expectedHeight ||
            OutputState->Width != expectedWidth ||
            OutputState->Height != expectedHeight ||
            OutputState->Scale != std::max(1, ScaleFactor))
        {
            // Self-heal if the target was resized after the previous output
            // configuration. This path is reached before taking OutputState's
            // composition lock.
            void* preferredDevice = Metal3DPreferredDevice(Rend3D.get());
            if (!ConfigureMetalVisibleOutput(preferredDevice))
                return;
        }

        struct VisibleOutputConfigCpu
        {
            uint32_t outputSize[2];
            uint32_t scale;
            uint32_t outputLayer;
            uint32_t engineALayer;
            uint32_t useHighResolution3D;
            uint32_t masterBrightnessA;
            uint32_t masterBrightnessB;
        };
        static_assert(sizeof(VisibleOutputConfigCpu) == 32,
            "VisibleOutputConfigCpu must match MSL layout");


        // Pin the active state for the rest of this compose. Scale reconfigure
        // may swap OutputState concurrently; completions and slot mutation must
        // stay on the state we started composing into.
        std::shared_ptr<MetalOutputState> state = OutputState;
        if (!state || !state->Ready)
            return;

        std::unique_lock<std::mutex> lock(state->Mutex);
        id<MTLTexture> high3D = state->CapturedHigh3D;
        id<MTLTexture> low3D = state->CapturedLow3D;
        const bool capturedFrameReady = state->CapturedFrameReady;
        const uint32_t engineALayer = state->CapturedEngineALayer;
        const int renderedScale = state->CapturedScale;
        const uint32_t useHighResolution3D =
            (capturedFrameReady &&
             state->Scale > 1 &&
             renderedScale == state->Scale &&
             state->CapturedUseHighResolution3D) ? 1u : 0u;
        if (!high3D || !low3D)
            return;

        int slotIndex = -1;
        for (int attempt = 0; attempt < MetalOutputState::SlotCount; attempt++)
        {
            const int candidate =
                (state->NextSlot + attempt) % MetalOutputState::SlotCount;
            // Never reuse the currently published slot. It is the only frame
            // AcquireOutputLease() can hand to the presenter; overwriting it
            // (InFlight=true + a new serial) between publish and lease-acquire
            // makes the lease fail and the presenter fall back to a one-frame
            // CPU BGRA flash. PresenterRefs==0 alone does not protect that
            // window because the presenter has not taken its ref yet.
            if (candidate == state->PublishedSlot)
                continue;
            if (!state->Slots[candidate].InFlight &&
                state->Slots[candidate].PresenterRefs == 0)
            {
                slotIndex = candidate;
                break;
            }
        }
        if (slotIndex < 0)
        {
            if (!state->LoggedNoFreeSlot)
            {
                state->LoggedNoFreeSlot = true;
                std::fprintf(stderr,
                    "[MelonPrime] metal visible output: all output slots busy; retaining previous frame\n");
            }
            return;
        }

        MetalOutputState::Slot& slot = state->Slots[slotIndex];
        state->NextSlot = (slotIndex + 1) % MetalOutputState::SlotCount;
        state->LoggedNoFreeSlot = false;

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

        id<MTLCommandBuffer> commandBuffer = [state->Queue commandBuffer];
        if (!commandBuffer)
            return;
        commandBuffer.label = @"MelonPrime Metal Visible HiRes Output";


        // high3D/low3D and all associated frame state were captured at
        // VCount 192, before VCount 215 begins rendering frame N+1.
        uint32_t replacementMode = useHighResolution3D;
        if (useHighResolution3D)
        {
            const char* replacementEnv =
                std::getenv("MELONPRIME_METAL_HIRES_REPLACEMENT");
            if (replacementEnv &&
                std::strcmp(replacementEnv, "off") == 0)
            {
                replacementMode = 0u;
            }
            else if (replacementEnv &&
                     std::strcmp(replacementEnv, "force") == 0)
            {
                replacementMode = 2u;
            }
        }

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
            config.outputSize[0] = static_cast<uint32_t>(state->Width);
            config.outputSize[1] = static_cast<uint32_t>(state->Height);
            config.scale = static_cast<uint32_t>(state->Scale);
            config.outputLayer = layer;
            config.engineALayer = engineALayer;
            config.useHighResolution3D = replacementMode;
            config.masterBrightnessA = FrameMasterBrightnessA;
            config.masterBrightnessB = FrameMasterBrightnessB;

            [encoder setRenderPipelineState:state->Pipeline];
            [encoder setFragmentBytes:&config length:sizeof(config) atIndex:0];
            [encoder setFragmentTexture:slot.CpuComposite atIndex:0];
            [encoder setFragmentTexture:high3D atIndex:1];
            [encoder setFragmentTexture:low3D atIndex:2];
            [encoder setViewport:(MTLViewport){
                0.0, 0.0,
                static_cast<double>(state->Width),
                static_cast<double>(state->Height),
                0.0, 1.0 }];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            [encoder endEncoding];
        }


        const uint64_t generation = state->Generation;
        const uint64_t serial = state->NextSerial++;
        slot.InFlight = true;
        slot.Generation = generation;
        slot.Serial = serial;
        state->InFlightCount++;

        [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            // Capture state by value so a swap-reconfigure cannot destroy the
            // MetalOutputState out from under this completion (and so the
            // destructor never waits on a completion still holding only a raw
            // pointer into a dying object).
            std::shared_ptr<MetalOutputState> completedState = state;
            std::lock_guard<std::mutex> completionLock(completedState->Mutex);
            MetalOutputState::Slot& completedSlot = completedState->Slots[slotIndex];
            if (completed.status == MTLCommandBufferStatusCompleted &&
                generation == completedState->Generation &&
                completedSlot.Generation == generation && completedSlot.Serial == serial)
            {
                completedState->PublishedSlot = slotIndex;
                completedState->PublishedSerial = serial;
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
            completedState->InFlightCount--;
            completedState->Completion.notify_all();
        }];

        [commandBuffer commit];

        if (!state->LoggedVisibleOutput)
        {
            state->LoggedVisibleOutput = true;
            std::fprintf(stderr,
                "[MelonPrime] metal visible output: first compose renderer=%s scale=%d renderedScale=%d size=%zux%zu engineALayer=%u replaceMode=%u (0=off 1=ownership 2=force)\n",
                ComputeRendererSelected ? "MetalCompute" : "Metal",
                state->Scale,
                renderedScale,
                static_cast<size_t>(state->Width),
                static_cast<size_t>(state->Height),
                engineALayer, replacementMode);
        }
    }
}

void MetalRenderer::VBlank()
{
    if (FullGpuState && FullGpuState->FrameActive)
    {
        bool rendered = false;
        // Same evaluation order and short-circuiting as the original single
        // boolean expression -- broken out only so a rejection can name the
        // stage that failed instead of leaving "unexpected rejection" with
        // nothing to act on.
        const char* rejectionStage = "none";
        if (FullGpuState->FrameValid &&
            MetalCaptureFrameSupported())
        {
            void* high3D = Metal3DColorTarget(Rend3D.get());
            void* capture128 = GetMetalCapture128Texture();
            void* capture256 = GetMetalCapture256Texture();
            const bool allowCaptureTextures =
                !MetalCaptureFrameHadCapture();

            if (!Metal2D_A || !Metal2D_B)
                rejectionStage = "2d-renderers-missing";
            else if (!capture128 || !capture256)
                rejectionStage = "capture-textures-missing";
            else if (!Metal2D_A->SegmentedFrameComplete())
                rejectionStage = "segment-snapshot-A-incomplete";
            else if (!Metal2D_B->SegmentedFrameComplete())
                rejectionStage = "segment-snapshot-B-incomplete";
            else if (!Metal2D_A->RenderSegmentedGpuFrame(
                         high3D,
                         capture128,
                         capture256,
                         allowCaptureTextures))
                rejectionStage = "render-segmented-A";
            else if (!Metal2D_B->RenderSegmentedGpuFrame(
                         high3D,
                         capture128,
                         capture256,
                         allowCaptureTextures))
                rejectionStage = "render-segmented-B";
            else if (!EncodeMetalDisplayCapture(
                         Metal2D_A->GetOutputTexture(),
                         high3D))
                rejectionStage = "encode-display-capture";
            else
                rendered = true;
        }
        else
        {
            rejectionStage = FullGpuState->FrameValid
                ? "capture-frame-unsupported"
                : "frame-invalidated-mid-render";
        }

        if (rendered)
        {
            FullGpuState->Completed = MetalFullGpuState::FullGpu;
            FullGpuState->CompletedScreenSwap = FullGpuState->ScreenSwap;
            FullGpuState->CompletedBrightnessA = FullGpuState->BrightnessA;
            FullGpuState->CompletedBrightnessB = FullGpuState->BrightnessB;
        }
        else
        {
            FullGpuState->Completed = MetalFullGpuState::RetainPrevious;
            // MetalCaptureFrameHadCapture() is narrower than "capture is
            // active this frame" -- it additionally requires the capture
            // destination bank to be VRAMMap_LCDC-mapped at the moment each
            // scanline's capture would have recorded, so it can be false even
            // when GPU.CaptureEnable (and the CaptureCnt start bit our
            // eligibility check reads) is genuinely true. Use the same signal
            // eligibility used, not the narrower one, or every capture this
            // narrower check misses becomes a false "unexpected" rejection.
            // MetalCaptureResourcesCoherent() is a second, independent known
            // gap: it can go false mid-frame when a BG/OBJ layer starts
            // referencing capture-backed VRAM that has never actually been
            // captured into (Meta[layer].Valid false), which has nothing to
            // do with whether a capture is active this exact frame.
            FullGpuState->BlockedByCaptureFeedback =
                MetalCaptureFrameHadCapture() ||
                GPU.CaptureEnable ||
                !MetalCaptureResourcesCoherent();
            if (!FullGpuState->LoggedRejected)
            {
                FullGpuState->LoggedRejected = true;
                std::fprintf(stderr,
                    "[MelonPrime] metal full-gpu: frame rejected; "
                    "retaining previous frame and using CPU fallback while "
                    "same-frame capture feedback remains active\n");
            }

            // Same-frame display-capture feedback is a known, catalogued gap
            // (Phase M4 of the full-Metal-ification plan is not done yet), so
            // it does not trip the strict assert. Likewise a mid-frame
            // eligibility loss that DrawScanline already detected and
            // sticky-blocked (BlockedByMidFrameInvalidation): that event is
            // handled by design -- one RetainPrevious frame, then the CPU
            // path under cooldown -- so it is a bounded scene-transition
            // artifact, not a GPU-only contract violation. Only a rejection
            // with NEITHER known cause is a genuine within-frame regression.
            if (!FullGpuState->BlockedByCaptureFeedback &&
                !FullGpuState->BlockedByMidFrameInvalidation)
            {
                if (FullGpuState->LoggedRejectionDetailCount < 10)
                {
                    FullGpuState->LoggedRejectionDetailCount++;
                    std::fprintf(stderr,
                        "[MelonPrime] metal full-gpu: unexpected rejection detail "
                        "stage=%s frameValid=%d captureFrameSupported=%d "
                        "captureEnable=%d captureCntBit31=%d screensEnabled=%d\n",
                        rejectionStage,
                        FullGpuState->FrameValid ? 1 : 0,
                        MetalCaptureFrameSupported() ? 1 : 0,
                        GPU.CaptureEnable ? 1 : 0,
                        (GPU.CaptureCnt & (1u << 31)) ? 1 : 0,
                        GPU.ScreensEnabled ? 1 : 0);
                }
                MetalStrictGpuOnlyViolation(
                    "MetalRenderer::VBlank",
                    "full-gpu frame rejected mid-render for a reason other than "
                    "known display-capture feedback, despite eligibility having "
                    "passed in Start3DRendering");
                // MELONPRIME_METAL_VBLANK_REJECTION_COOLDOWN_V1: engage the
                // same sticky cooldown DrawScanline-detected invalidations
                // use. Without it, a state that passes every Start3DRendering
                // eligibility check yet fails again at this VBlank stage on
                // every single frame retries (and RetainPrevious-freezes the
                // visible picture) indefinitely -- observed in practice as a
                // sustained retainPrevious=600/600 freeze with thousands of
                // the strict-violation lines above, frozen on the last
                // composed frame. With the cooldown, the damage is bounded to
                // one retained frame per episode; the next frames correctly
                // predict ineligibility and take the mature CPU path, which
                // keeps the picture updating.
                FullGpuState->BlockedByMidFrameInvalidation = true;
                FullGpuState->MidFrameInvalidationCooldownFrames =
                    kMidFrameInvalidationCooldownFrames;
            }
        }
        MetalFullGpuFrameStatsRecord(
            FullGpuState->FrameStats,
            rendered, false, !rendered, FullGpuState->BlockedByCaptureFeedback);
        return;
    }

    SoftRenderer::VBlank();
    UploadCpuCompletedCaptures();



    if (FullGpuState)
    {
        FullGpuState->CompletedScreenSwap = FullGpuState->ScreenSwap;
        FullGpuState->CompletedBrightnessA = FullGpuState->BrightnessA;
        FullGpuState->CompletedBrightnessB = FullGpuState->BrightnessB;
        FullGpuState->Completed = MetalFullGpuState::CpuComposite;
        MetalFullGpuFrameStatsRecord(FullGpuState->FrameStats, false, true, false, false);
    }
}

void MetalRenderer::SwapBuffers()
{
    Renderer::SwapBuffers();
    if (FullGpuState && FullGpuState->Completed == MetalFullGpuState::FullGpu)
        ComposeMetalFullGpuOutput();
    else if (!FullGpuState || FullGpuState->Completed == MetalFullGpuState::CpuComposite)
        ComposeMetalVisibleOutput();
    if (FullGpuState)
        FullGpuState->Completed = MetalFullGpuState::RetainPrevious;
}

RendererOutputLease MetalRenderer::AcquireOutputLease()
{
    if (OutputState)
    {
        std::shared_ptr<MetalOutputState> state = OutputState;
        std::lock_guard<std::mutex> lock(state->Mutex);
        if (state->Ready && state->PublishedSlot >= 0)
        {
            const int slotIndex = state->PublishedSlot;
            MetalOutputState::Slot& slot = state->Slots[slotIndex];
            id<MTLTexture> texture = slot.FinalTexture;
            if (texture && !slot.InFlight &&
                slot.Generation == state->Generation &&
                slot.Serial == state->PublishedSerial)
            {
                slot.PresenterRefs++;
                state->PresenterRefCount++;

                struct LeaseContext
                {
                    std::shared_ptr<MetalOutputState> State;
                    int SlotIndex;
                    uint64_t Generation;
                    uint64_t Serial;
                };

                LeaseContext* context = new LeaseContext{
                    state, slotIndex, slot.Generation, slot.Serial };

                auto release = +[](void* opaque) {
                    std::unique_ptr<LeaseContext> lease(
                        static_cast<LeaseContext*>(opaque));
                    std::lock_guard<std::mutex> releaseLock(lease->State->Mutex);
                    MetalOutputState::Slot& leasedSlot =
                        lease->State->Slots[lease->SlotIndex];
                    if (leasedSlot.PresenterRefs > 0 &&
                        leasedSlot.Generation == lease->Generation &&
                        leasedSlot.Serial == lease->Serial)
                    {
                        leasedSlot.PresenterRefs--;
                        lease->State->PresenterRefCount--;
                    }
                    else
                    {
                        std::fprintf(stderr,
                            "[MelonPrime] metal visible output: stale presenter lease serial=%llu\n",
                            static_cast<unsigned long long>(lease->Serial));
                    }
                    lease->State->Completion.notify_all();
                };

                return RendererOutputLease(
                    RendererOutput::MetalTexture(
                        (__bridge void*)texture, slot.Serial,
                        static_cast<u32>(texture.width),
                        static_cast<u32>(texture.height),
                        static_cast<u32>(texture.arrayLength),
                        static_cast<u32>(state->Scale),
                        slot.Generation,
                        state->ProducerId),
                    context,
                    release);
            }
        }
    }

    return RendererOutputLease(SoftRenderer::GetOutput(), nullptr, nullptr);
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
                    (__bridge void*)texture, OutputState->PublishedSerial,
                    static_cast<u32>(texture.width),
                    static_cast<u32>(texture.height),
                    static_cast<u32>(texture.arrayLength),
                    static_cast<u32>(OutputState->Scale),
                    OutputState->Slots[slotIndex].Generation,
                    OutputState->ProducerId);
        }
    }
    return SoftRenderer::GetOutput();
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
