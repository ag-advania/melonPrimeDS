// MelonPrimeDS - experimental Metal renderer (real final-output path)

#if defined(MELONPRIME_ENABLE_METAL)

#import <Metal/Metal.h>

#include "GPU_Metal.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>

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
    const bool engineAUses3D = EngineAUses3D(GPU);
    if (engineAUses3D && (!native3D || native3D.device != state.Device))
        return false;

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

    const int native3DLayer = EngineAOutputLayer(GPU);
    const NSUInteger finalWidth = finalTex.width;
    const NSUInteger finalHeight = finalTex.height;
    const uint64_t frameSerial = ++state.FrameSerial;

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

    for (int layer = 0; layer < 2; layer++)
    {
        id<MTLTexture> source = nil;
        id<MTLSamplerState> sampler = state.NearestSampler;

        if (engineAUses3D && layer == native3DLayer)
        {
            source = native3D;
            sampler = state.LinearSampler;
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
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);

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
