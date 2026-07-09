// MelonPrimeDS - experimental Metal renderer (real final-output path)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <vector>

#include "GPU3D_Metal.h"
#include "NDS.h"

namespace melonDS
{

namespace
{

using MetalClock = std::chrono::steady_clock;

constexpr int kScreenW = 256;
constexpr int kScreenH = 192;

struct FinalVertex
{
    float pos[2];
    float tex[2];
};

constexpr FinalVertex kFinalQuad[] = {
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{-1.0f,  1.0f}, {0.0f, 0.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
    {{-1.0f, -1.0f}, {0.0f, 1.0f}},
    {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
    {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
};

NSString* const kMetalFinalShaderSource =
    @"#include <metal_stdlib>\n"
     "using namespace metal;\n"
     "struct VertexIn {\n"
     "    float2 position [[attribute(0)]];\n"
     "    float2 texcoord [[attribute(1)]];\n"
     "};\n"
     "struct VOut {\n"
     "    float4 position [[position]];\n"
     "    float2 texcoord;\n"
     "};\n"
     "vertex VOut mp_final_vs(VertexIn in [[stage_in]]) {\n"
     "    VOut out;\n"
     "    out.position = float4(in.position, 0.0, 1.0);\n"
     "    out.texcoord = in.texcoord;\n"
     "    return out;\n"
     "}\n"
     "fragment float4 mp_final_fs(VOut in [[stage_in]],\n"
     "                            texture2d<float> tex [[texture(0)]],\n"
     "                            sampler samp [[sampler(0)]]) {\n"
     "    float4 c = tex.sample(samp, in.texcoord);\n"
     "    return float4(c.rgb, 1.0);\n"
     "}\n";

bool AllowSoftwareFallback()
{
    static const bool allow = []() {
        const char* env = std::getenv("MELONPRIME_METAL_ALLOW_SOFTWARE_FALLBACK");
        return env && env[0] == '1';
    }();
    return allow;
}

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

bool MetalDiagFinalLayersEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_DIAG_FINAL_LAYERS");
        return env && env[0] == '1';
    }();
    return enabled;
}

double ElapsedMs(MetalClock::time_point start, MetalClock::time_point end)
{
    return std::chrono::duration<double, std::milli>(end - start).count();
}

struct FinalPassPerfAccumulator
{
    uint32_t Frames = 0;
    double FinalPassMs = 0.0;
    uint64_t UploadBytes = 0;
    uint32_t LastScale = 1;
    uint32_t LastTargetWidth = 0;
    uint32_t LastTargetHeight = 0;
    uint32_t SoftwareFallbackFrames = 0;
};

void SubmitFinalPassPerf(int scale, NSUInteger width, NSUInteger height, double finalPassMs, uint64_t uploadBytes, bool softwareFallback)
{
    if (!MetalPerfEnabled())
        return;

    static FinalPassPerfAccumulator acc;
    acc.Frames++;
    acc.FinalPassMs += finalPassMs;
    acc.UploadBytes += uploadBytes;
    acc.LastScale = static_cast<uint32_t>(scale);
    acc.LastTargetWidth = static_cast<uint32_t>(width);
    acc.LastTargetHeight = static_cast<uint32_t>(height);
    if (softwareFallback)
        acc.SoftwareFallbackFrames++;

    constexpr uint32_t kReportFrames = 600;
    if (acc.Frames < kReportFrames)
        return;

    const double frames = static_cast<double>(acc.Frames);
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: perf frames=%u native3dMs=reported-by-renderer3D "
        "finalPassMs=%.3f presentMs=reported-by-presenter uploadBytes=%.0f "
        "softwareFallback=%u visibleSource=MetalFinalTexture scale=%u target=%ux%u\n",
        acc.Frames,
        acc.FinalPassMs / frames,
        static_cast<double>(acc.UploadBytes) / frames,
        acc.SoftwareFallbackFrames,
        acc.LastScale,
        acc.LastTargetWidth,
        acc.LastTargetHeight);

    acc = {};
}

struct MetalTextureReadbackSummary
{
    uint64_t NonzeroPixels = 0;
    uint64_t Checksum = 1469598103934665603ull;
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
                summary.NonzeroPixels++;
        }
    }
    summary.Valid = true;
    return summary;
}

uint32_t BGRA8(uint8_t b, uint8_t g, uint8_t r, uint8_t a)
{
    return static_cast<uint32_t>(b) |
           (static_cast<uint32_t>(g) << 8) |
           (static_cast<uint32_t>(r) << 16) |
           (static_cast<uint32_t>(a) << 24);
}

void UploadFinalLayerDiagnosticSource(id<MTLTexture> texture, int layer)
{
    if (!texture)
        return;

    static std::vector<uint32_t> layerPixels[2];
    std::vector<uint32_t>& pixels = layerPixels[layer & 1];
    if (pixels.empty())
    {
        pixels.resize(static_cast<size_t>(kScreenW) * static_cast<size_t>(kScreenH));
        const uint32_t a = (layer == 0) ? BGRA8(0, 0, 255, 255) : BGRA8(255, 0, 0, 255);
        const uint32_t b = (layer == 0) ? BGRA8(0, 255, 0, 255) : BGRA8(0, 255, 255, 255);
        for (int y = 0; y < kScreenH; y++)
        {
            for (int x = 0; x < kScreenW; x++)
            {
                const bool checker = ((x / 16) ^ (y / 16)) & 1;
                pixels[static_cast<size_t>(y) * kScreenW + x] = checker ? b : a;
            }
        }
    }

    [texture replaceRegion:MTLRegionMake2D(0, 0, kScreenW, kScreenH)
               mipmapLevel:0
                 withBytes:pixels.data()
               bytesPerRow:kScreenW * 4];
}

bool EngineAUses3D(const GPU& gpu)
{
    const u32 dispCnt = gpu.GPU2D_A.DispCnt;
    const u32 dispMode = (dispCnt >> 16) & 0x3;
    return gpu.ScreensEnabled &&
           dispMode == 1 &&
           (dispCnt & (1u << 3)) &&
           (dispCnt & (1u << 8));
}

int EngineAOutputLayer(const GPU& gpu)
{
    return gpu.ScreenSwap ? 0 : 1;
}

struct MetalFinalRouting
{
    u32 DispCntA = 0;
    u32 DispCntB = 0;
    u32 DispModeA = 0;
    u32 DispModeB = 0;
    bool ScreensEnabled = false;
    bool ScreenSwap = false;
    bool EngineA3DBitsSet = false;
    bool EngineAUses3D = false;
    bool UnsupportedRoute = false;
    const char* UnsupportedReason = "none";
    int Native3DLayer = 1;
};

MetalFinalRouting AnalyzeFinalRouting(const GPU& gpu)
{
    MetalFinalRouting route;
    route.DispCntA = gpu.GPU2D_A.DispCnt;
    route.DispCntB = gpu.GPU2D_B.DispCnt;
    route.DispModeA = (route.DispCntA >> 16) & 0x3;
    route.DispModeB = (route.DispCntB >> 16) & 0x3;
    route.ScreensEnabled = gpu.ScreensEnabled;
    route.ScreenSwap = gpu.ScreenSwap;
    route.EngineA3DBitsSet = (route.DispCntA & (1u << 3)) && (route.DispCntA & (1u << 8));
    route.EngineAUses3D = EngineAUses3D(gpu);
    route.Native3DLayer = EngineAOutputLayer(gpu);

    if (route.ScreensEnabled && route.EngineA3DBitsSet && !route.EngineAUses3D)
    {
        route.UnsupportedRoute = true;
        route.UnsupportedReason = "engineA_3d_bits_outside_supported_display_mode";
    }

    return route;
}

uint64_t RoutingSignature(const MetalFinalRouting& route)
{
    uint64_t sig = route.DispCntA;
    sig = (sig << 32) ^ route.DispCntB;
    sig ^= static_cast<uint64_t>(route.ScreenSwap ? 1u : 0u) << 1;
    sig ^= static_cast<uint64_t>(route.ScreensEnabled ? 1u : 0u) << 2;
    sig ^= static_cast<uint64_t>(route.EngineAUses3D ? 1u : 0u) << 3;
    sig ^= static_cast<uint64_t>(route.UnsupportedRoute ? 1u : 0u) << 4;
    return sig;
}

} // namespace

struct MetalRenderer::MetalFinalState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLRenderPipelineState> Pipeline = nil;
    id<MTLBuffer> VertexBuffer = nil;
    id<MTLSamplerState> NearestSampler = nil;
    id<MTLSamplerState> LinearSampler = nil;
    id<MTLTexture> FinalOutputTex[2] = { nil, nil };
    id<MTLTexture> CpuScreenTex[2] = { nil, nil };
    int Scale = 0;
    int FrontBuffer = 0;
    uint64_t FrameSerial = 0;
    uint64_t CompletedFrameSerial = 0;
    bool HasCompletedFrame = false;
    bool LoggedFirstFinalFrame = false;
    bool LoggedSoftwareFallbackError = false;
    uint64_t LastRoutingSignature = ~uint64_t { 0 };
    bool LoggedFinalLayerDiagnostic = false;
};

MetalRenderer::MetalRenderer(melonDS::NDS& nds) noexcept
    : SoftRenderer(nds),
      FinalState(std::make_unique<MetalFinalState>())
{
    Rend3D = std::make_unique<MetalRenderer3D>(GPU.GPU3D, *this);
}

MetalRenderer::~MetalRenderer() = default;

bool MetalRenderer::Init()
{
    std::fprintf(stderr,
        "[MelonPrime] metal renderer: initializing native Metal 3D plus final two-screen output path\n");
    return Rend3D->Init() && EnsureFinalOutput();
}

void MetalRenderer::PreSavestate()
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (rend3d && rend3d->IsThreaded())
        rend3d->SetupRenderThread();
}

void MetalRenderer::PostSavestate()
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (rend3d && rend3d->IsThreaded())
        rend3d->EnableRenderThread();
}

void MetalRenderer::SetRenderSettings(RendererSettings& settings)
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (!rend3d)
        return;

    const int scale = std::max(1, settings.ScaleFactor);
    ScaleFactor = scale;

    rend3d->SetThreaded(settings.Threaded);
    rend3d->SetScaleFactor(scale);
    EnsureFinalOutput();
}

bool MetalRenderer::EnsureFinalOutput()
{
    return EnsureFinalOutputForDevice(nullptr);
}

bool MetalRenderer::EnsureFinalOutputForDevice(void* preferredDevice)
{
    if (!FinalState)
        return false;

    MetalFinalState& state = *FinalState;
    id<MTLDevice> preferredMetalDevice = preferredDevice ? (__bridge id<MTLDevice>)preferredDevice : nil;
    if (preferredMetalDevice && state.Device && state.Device != preferredMetalDevice)
    {
        state.Queue = nil;
        state.Library = nil;
        state.Pipeline = nil;
        state.VertexBuffer = nil;
        state.NearestSampler = nil;
        state.LinearSampler = nil;
        state.FinalOutputTex[0] = nil;
        state.FinalOutputTex[1] = nil;
        state.CpuScreenTex[0] = nil;
        state.CpuScreenTex[1] = nil;
        state.Scale = 0;
        state.FrontBuffer = 0;
        state.Device = preferredMetalDevice;
    }

    if (!state.Device)
    {
        state.Device = preferredMetalDevice ? preferredMetalDevice : MTLCreateSystemDefaultDevice();
        if (!state.Device)
        {
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to create final output Metal device\n");
            return false;
        }

        state.Queue = [state.Device newCommandQueue];
        if (!state.Queue)
        {
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to create final output command queue\n");
            return false;
        }

        NSError* error = nil;
        state.Library = [state.Device newLibraryWithSource:kMetalFinalShaderSource options:nil error:&error];
        if (!state.Library)
        {
            const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to compile final output shaders: %s\n", message);
            return false;
        }

        MTLVertexDescriptor* vertexDesc = [[MTLVertexDescriptor alloc] init];
        vertexDesc.attributes[0].format = MTLVertexFormatFloat2;
        vertexDesc.attributes[0].offset = offsetof(FinalVertex, pos);
        vertexDesc.attributes[0].bufferIndex = 0;
        vertexDesc.attributes[1].format = MTLVertexFormatFloat2;
        vertexDesc.attributes[1].offset = offsetof(FinalVertex, tex);
        vertexDesc.attributes[1].bufferIndex = 0;
        vertexDesc.layouts[0].stride = sizeof(FinalVertex);
        vertexDesc.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pipelineDesc = [[MTLRenderPipelineDescriptor alloc] init];
        pipelineDesc.vertexFunction = [state.Library newFunctionWithName:@"mp_final_vs"];
        pipelineDesc.fragmentFunction = [state.Library newFunctionWithName:@"mp_final_fs"];
        pipelineDesc.vertexDescriptor = vertexDesc;
        pipelineDesc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        state.Pipeline = [state.Device newRenderPipelineStateWithDescriptor:pipelineDesc error:&error];
        if (!state.Pipeline)
        {
            const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to create final output pipeline: %s\n", message);
            return false;
        }

        state.VertexBuffer = [state.Device newBufferWithBytes:kFinalQuad
                                                       length:sizeof(kFinalQuad)
                                                      options:MTLResourceStorageModeShared];
        if (!state.VertexBuffer)
        {
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to create final output vertex buffer\n");
            return false;
        }

        MTLSamplerDescriptor* nearestDesc = [[MTLSamplerDescriptor alloc] init];
        nearestDesc.minFilter = MTLSamplerMinMagFilterNearest;
        nearestDesc.magFilter = MTLSamplerMinMagFilterNearest;
        nearestDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        nearestDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        state.NearestSampler = [state.Device newSamplerStateWithDescriptor:nearestDesc];

        MTLSamplerDescriptor* linearDesc = [[MTLSamplerDescriptor alloc] init];
        linearDesc.minFilter = MTLSamplerMinMagFilterLinear;
        linearDesc.magFilter = MTLSamplerMinMagFilterLinear;
        linearDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        linearDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        state.LinearSampler = [state.Device newSamplerStateWithDescriptor:linearDesc];

        if (!state.NearestSampler || !state.LinearSampler)
        {
            std::fprintf(stderr, "[MelonPrime] metal renderer: failed to create final output samplers\n");
            return false;
        }
    }

    const int scale = std::max(1, ScaleFactor);
    if (state.Scale == scale && state.FinalOutputTex[0] && state.FinalOutputTex[1] &&
        state.CpuScreenTex[0] && state.CpuScreenTex[1])
    {
        return true;
    }

    const NSUInteger width = static_cast<NSUInteger>(kScreenW * scale);
    const NSUInteger height = static_cast<NSUInteger>(kScreenH * scale);

    MTLTextureDescriptor* finalDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    finalDesc.textureType = MTLTextureType2DArray;
    finalDesc.arrayLength = 2;
    finalDesc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
    finalDesc.storageMode = MTLStorageModePrivate;

    id<MTLTexture> newFinal[2] = {
        [state.Device newTextureWithDescriptor:finalDesc],
        [state.Device newTextureWithDescriptor:finalDesc],
    };

    MTLTextureDescriptor* cpuDesc =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                           width:kScreenW
                                                          height:kScreenH
                                                       mipmapped:NO];
    cpuDesc.usage = MTLTextureUsageShaderRead;
    cpuDesc.storageMode = MTLStorageModeShared;

    id<MTLTexture> newCpu[2] = {
        [state.Device newTextureWithDescriptor:cpuDesc],
        [state.Device newTextureWithDescriptor:cpuDesc],
    };

    if (!newFinal[0] || !newFinal[1] || !newCpu[0] || !newCpu[1])
    {
        std::fprintf(stderr, "[MelonPrime] metal renderer: failed to allocate final output textures\n");
        return false;
    }

    state.FinalOutputTex[0] = newFinal[0];
    state.FinalOutputTex[1] = newFinal[1];
    state.CpuScreenTex[0] = newCpu[0];
    state.CpuScreenTex[1] = newCpu[1];
    state.Scale = scale;
    state.FrontBuffer = 0;
    state.HasCompletedFrame = false;
    state.CompletedFrameSerial = 0;

    std::fprintf(stderr,
        "[MelonPrime] metal renderer: final output scale=%d size=%zux%zu layers=2\n",
        scale,
        static_cast<size_t>(width),
        static_cast<size_t>(height));

    return true;
}

RendererOutput MetalRenderer::GetSoftwareFallbackOutput()
{
    if (AllowSoftwareFallback())
        return SoftRenderer::GetOutput();

    if (FinalState && !FinalState->LoggedSoftwareFallbackError)
    {
        FinalState->LoggedSoftwareFallbackError = true;
        std::fprintf(stderr,
            "[MelonPrime] metal renderer: ERROR no native Metal final output; refusing silent software fallback\n");
    }
    return {};
}

void MetalRenderer::VBlank()
{
    SoftRenderer::VBlank();
    ComposeFinalOutputForCompletedFrame();
}

bool MetalRenderer::ComposeFinalOutputForCompletedFrame()
{
    auto* rend3d = dynamic_cast<MetalRenderer3D*>(Rend3D.get());
    if (!rend3d)
        return false;

    id<MTLTexture> native3D = (__bridge id<MTLTexture>)rend3d->GetColorTargetTexture();
    void* preferredDevice = native3D ? (__bridge void*)native3D.device : nullptr;
    if (!EnsureFinalOutputForDevice(preferredDevice))
        return false;

    MetalFinalState& state = *FinalState;
    const MetalFinalRouting routing = AnalyzeFinalRouting(GPU);
    const bool native3DMissing = routing.EngineAUses3D && !native3D;
    const bool native3DDeviceMismatch = routing.EngineAUses3D && native3D && native3D.device != state.Device;

    void* topCpu = nullptr;
    void* bottomCpu = nullptr;
    const bool hasCpuScreens = SoftRenderer::GetFramebuffers(&topCpu, &bottomCpu) && topCpu && bottomCpu;

    const int nextBackBuffer = state.FrontBuffer ^ 1;
    id<MTLTexture> finalTex = state.FinalOutputTex[nextBackBuffer];
    if (!finalTex)
        return false;

    const auto start = MetalClock::now();
    uint64_t uploadBytes = 0;

    id<MTLCommandBuffer> commandBuffer = [state.Queue commandBuffer];
    if (!commandBuffer)
        return false;

    const int native3DLayer = routing.Native3DLayer;
    const NSUInteger finalWidth = finalTex.width;
    const NSUInteger finalHeight = finalTex.height;
    const uint64_t frameSerial = ++state.FrameSerial;
    const uint64_t routeSig = RoutingSignature(routing);

    if (frameSerial <= 3 || (frameSerial % 600) == 0)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal frame: compose frame=%llu back=%d front=%d scale=%d target=%zux%zu\n",
            static_cast<unsigned long long>(frameSerial),
            nextBackBuffer,
            state.FrontBuffer,
            state.Scale,
            static_cast<size_t>(finalWidth),
            static_cast<size_t>(finalHeight));
    }
    if (routeSig != state.LastRoutingSignature)
    {
        state.LastRoutingSignature = routeSig;
        std::fprintf(stderr,
            "[MelonPrime] metal final route: dispA=0x%08x modeA=%u dispB=0x%08x modeB=%u "
            "screenSwap=%u screensEnabled=%u engineA3DBits=%u engineAUses3D=%u "
            "native3DLayer=%d supportedSubset=normal2d_plus_engineA_bg0_3d unsupported=%u reason=%s\n",
            routing.DispCntA,
            routing.DispModeA,
            routing.DispCntB,
            routing.DispModeB,
            routing.ScreenSwap ? 1u : 0u,
            routing.ScreensEnabled ? 1u : 0u,
            routing.EngineA3DBitsSet ? 1u : 0u,
            routing.EngineAUses3D ? 1u : 0u,
            routing.Native3DLayer,
            routing.UnsupportedRoute ? 1u : 0u,
            routing.UnsupportedReason);
    }
    const bool finalLayerDiagnostic = MetalDiagFinalLayersEnabled();
    if (finalLayerDiagnostic && !state.LoggedFinalLayerDiagnostic)
    {
        state.LoggedFinalLayerDiagnostic = true;
        std::fprintf(stderr,
            "[MelonPrime] metal final diag: final-layer checker override active "
            "layer0=red/green layer1=blue/yellow\n");
    }

    for (int layer = 0; layer < 2; layer++)
    {
        id<MTLTexture> source = nil;
        id<MTLSamplerState> sampler = state.NearestSampler;
        MTLClearColor clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

        if (finalLayerDiagnostic)
        {
            UploadFinalLayerDiagnosticSource(state.CpuScreenTex[layer], layer);
            uploadBytes += static_cast<uint64_t>(kScreenW) * kScreenH * 4;
            source = state.CpuScreenTex[layer];
            sampler = state.NearestSampler;
        }
        else if (routing.UnsupportedRoute && layer == routing.Native3DLayer)
        {
            // Magenta: final-composer route is known unsupported. Do not
            // hide it with CPU-complete Software output.
            clearColor = MTLClearColorMake(1.0, 0.0, 1.0, 1.0);
        }
        else if (routing.EngineAUses3D && layer == routing.Native3DLayer)
        {
            if (native3DMissing)
            {
                // Red: Engine A needs native 3D, but no native target exists.
                clearColor = MTLClearColorMake(1.0, 0.0, 0.0, 1.0);
            }
            else if (native3DDeviceMismatch)
            {
                // Blue: native target exists but belongs to a different device.
                clearColor = MTLClearColorMake(0.0, 0.0, 1.0, 1.0);
            }
            else
            {
                source = native3D;
                sampler = state.LinearSampler;
            }
        }
        else if (hasCpuScreens)
        {
            void* cpuLayer = (layer == 0) ? topCpu : bottomCpu;
            [state.CpuScreenTex[layer] replaceRegion:MTLRegionMake2D(0, 0, kScreenW, kScreenH)
                                          mipmapLevel:0
                                            withBytes:cpuLayer
                                          bytesPerRow:kScreenW * 4];
            uploadBytes += static_cast<uint64_t>(kScreenW) * kScreenH * 4;
            source = state.CpuScreenTex[layer];
            sampler = state.NearestSampler;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = finalTex;
        pass.colorAttachments[0].slice = static_cast<NSUInteger>(layer);
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = clearColor;

        id<MTLRenderCommandEncoder> encoder = [commandBuffer renderCommandEncoderWithDescriptor:pass];
        if (!encoder)
            continue;

        if (source)
        {
            [encoder setRenderPipelineState:state.Pipeline];
            [encoder setVertexBuffer:state.VertexBuffer offset:0 atIndex:0];
            [encoder setFragmentTexture:source atIndex:0];
            [encoder setFragmentSamplerState:sampler atIndex:0];
            [encoder setViewport:(MTLViewport){0.0, 0.0, static_cast<double>(finalWidth), static_cast<double>(finalHeight), 0.0, 1.0}];
            [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        }
        [encoder endEncoding];
    }

    [commandBuffer commit];
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted)
        return false;

    if (MetalDiagEnabled())
    {
        const MetalTextureReadbackSummary layer0 =
            ReadbackBGRA8Texture(state.Queue, finalTex, 0);
        const MetalTextureReadbackSummary layer1 =
            ReadbackBGRA8Texture(state.Queue, finalTex, 1);
        const Metal3DDiagnostics nativeDiag = rend3d->GetLastDiagnostics();

        static uint64_t diagFrames = 0;
        diagFrames++;
        const bool usedNative3D =
            !finalLayerDiagnostic && routing.EngineAUses3D && native3D && !native3DDeviceMismatch;
        const bool nativeVisibleButFinalBlack =
            usedNative3D && nativeDiag.NonzeroPixels > 0 &&
            ((native3DLayer == 0 && layer0.Valid && layer0.NonzeroPixels == 0) ||
             (native3DLayer == 1 && layer1.Valid && layer1.NonzeroPixels == 0));

        if (diagFrames <= 3 || (diagFrames % 60) == 0 || nativeVisibleButFinalBlack)
        {
            std::fprintf(stderr,
                "[MelonPrime] metal final diag: layer0.nonzero=%llu layer1.nonzero=%llu "
                "layer0.checksum=0x%016llx layer1.checksum=0x%016llx "
                "native3DLayer=%d usedNative3D=%u native3D.nonzero=%llu valid=%u/%u\n",
                static_cast<unsigned long long>(layer0.NonzeroPixels),
                static_cast<unsigned long long>(layer1.NonzeroPixels),
                static_cast<unsigned long long>(layer0.Checksum),
                static_cast<unsigned long long>(layer1.Checksum),
                native3DLayer,
                usedNative3D ? 1u : 0u,
                static_cast<unsigned long long>(nativeDiag.NonzeroPixels),
                layer0.Valid ? 1u : 0u,
                layer1.Valid ? 1u : 0u);
        }
    }

    state.FrontBuffer = nextBackBuffer;
    state.CompletedFrameSerial = frameSerial;
    state.HasCompletedFrame = true;

    if (!state.LoggedFirstFinalFrame)
    {
        state.LoggedFirstFinalFrame = true;
        std::fprintf(stderr,
            "[MelonPrime] metal renderer: final pass visible3DSource=nativeMetal 2DSource=temporaryCpuUpload "
            "completeCpuFrameFallback=false softwareFallback=0 visibleSource=MetalFinalTexture scale=%d "
            "target=%zux%zu native3dMs=reported-by-renderer3D finalPassMs=%.3f "
            "presentMs=reported-by-presenter uploadBytes=%llu\n",
            state.Scale,
            static_cast<size_t>(finalWidth),
            static_cast<size_t>(finalHeight),
            ElapsedMs(start, MetalClock::now()),
            static_cast<unsigned long long>(uploadBytes));
    }

    SubmitFinalPassPerf(state.Scale, finalWidth, finalHeight, ElapsedMs(start, MetalClock::now()), uploadBytes, false);

    return commandBuffer.status == MTLCommandBufferStatusCompleted;
}

RendererOutput MetalRenderer::GetOutput()
{
    if (!FinalState || !FinalState->HasCompletedFrame || !FinalState->FinalOutputTex[FinalState->FrontBuffer])
        return GetSoftwareFallbackOutput();

    return RendererOutput::MetalTexture(
        (__bridge void*)FinalState->FinalOutputTex[FinalState->FrontBuffer],
        FinalState->CompletedFrameSerial);
}

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
