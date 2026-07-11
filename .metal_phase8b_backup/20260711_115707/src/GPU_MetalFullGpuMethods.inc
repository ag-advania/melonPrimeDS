// Included after MetalOutputState is complete and before MetalRenderer's destructor.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1

namespace
{
static constexpr const char* kMetalFullGpuOutputShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

struct FullGpuVertexOut
{
    float4 position [[position]];
};

vertex FullGpuVertexOut mp_full_gpu_output_vs(uint vertexID [[vertex_id]])
{
    constexpr float2 positions[3] = {
        float2(-1.0, -1.0),
        float2( 3.0, -1.0),
        float2(-1.0,  3.0),
    };
    FullGpuVertexOut out;
    out.position = float4(positions[vertexID], 0.0, 1.0);
    return out;
}

struct FullGpuOutputConfig
{
    uint2 outputSize;
    uint scale;
    uint outputLayer;
    uint screenSwap[192];
    uint brightnessA[192];
    uint brightnessB[192];
};

static inline float3 mp_full_apply_brightness(float3 color, uint reg)
{
    uint mode = reg >> 14u;
    float factor = float(min(reg & 0x1Fu, 16u)) / 16.0;
    if (mode == 1u)
        return mix(color, float3(1.0), factor);
    if (mode == 2u)
        return color * (1.0 - factor);
    return color;
}

fragment float4 mp_full_gpu_output_fs(
    FullGpuVertexOut in [[stage_in]],
    constant FullGpuOutputConfig& config [[buffer(0)]],
    texture2d<float, access::read> engineA [[texture(0)]],
    texture2d<float, access::read> engineB [[texture(1)]])
{
    uint2 coord = min(
        uint2(in.position.xy),
        config.outputSize - uint2(1u, 1u));
    uint line = min(coord.y / max(config.scale, 1u), 191u);
    bool swap = config.screenSwap[line] != 0u;
    bool selectA = config.outputLayer == 0u ? swap : !swap;
    float4 color = selectA ? engineA.read(coord) : engineB.read(coord);
    uint brightness = selectA
        ? config.brightnessA[line]
        : config.brightnessB[line];
    color.rgb = mp_full_apply_brightness(color.rgb, brightness);
    return float4(clamp(color.rgb, 0.0, 1.0), 1.0);
}
)MSL";
}

struct MetalRenderer::MetalFullGpuState
{
    enum CompletedKind
    {
        RetainPrevious = 0,
        CpuComposite = 1,
        FullGpu = 2,
    };

    id<MTLLibrary> Library = nil;
    id<MTLRenderPipelineState> Pipeline = nil;
    bool Requested = false;
    bool FrameActive = false;
    bool FrameValid = false;
    CompletedKind Completed = RetainPrevious;
    bool DisabledAfterReject = false;
    bool LoggedEnabled = false;
    bool LoggedRejected = false;
    bool LoggedCompose = false;
    std::array<uint32_t, 192> ScreenSwap {};
    std::array<uint32_t, 192> BrightnessA {};
    std::array<uint32_t, 192> BrightnessB {};
    std::array<uint32_t, 192> CompletedScreenSwap {};
    std::array<uint32_t, 192> CompletedBrightnessA {};
    std::array<uint32_t, 192> CompletedBrightnessB {};
};

bool MetalRenderer::InitializeMetalFullGpuOutput()
{
    if (!FullGpuState)
        FullGpuState = std::make_unique<MetalFullGpuState>();

    const char* env = std::getenv("MELONPRIME_METAL_FULL_GPU");
    FullGpuState->Requested = env && env[0] == '1';
    if (!FullGpuState->Requested)
        return true;

    if (!OutputState || !OutputState->Device)
        return false;
    if (FullGpuState->Pipeline)
        return true;

    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetalFullGpuOutputShaderSource];
    FullGpuState->Library =
        [OutputState->Device newLibraryWithSource:source options:nil error:&error];
    if (!FullGpuState->Library)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu output: shader compile failed: %s\n", message);
        return false;
    }

    id<MTLFunction> vertex =
        [FullGpuState->Library newFunctionWithName:@"mp_full_gpu_output_vs"];
    id<MTLFunction> fragment =
        [FullGpuState->Library newFunctionWithName:@"mp_full_gpu_output_fs"];
    if (!vertex || !fragment)
        return false;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.label = @"MelonPrime Metal Full GPU Final Output";
    desc.vertexFunction = vertex;
    desc.fragmentFunction = fragment;
    desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    FullGpuState->Pipeline =
        [OutputState->Device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!FullGpuState->Pipeline)
    {
        const char* message = error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu output: pipeline failed: %s\n", message);
        return false;
    }

    if (!FullGpuState->LoggedEnabled)
    {
        FullGpuState->LoggedEnabled = true;
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu: Phase 8A enabled; "
            "normal display-mode frames use GPU-resident 2D and skip 3D readback\n");
    }
    return true;
}

bool MetalRenderer::IsMetalFullGpuFrameEligible() const
{
    if (!FullGpuState || !FullGpuState->Requested ||
        FullGpuState->DisabledAfterReject ||
        !FullGpuState->Pipeline || !OutputState || !OutputState->Ready ||
        !Metal2D_A || !Metal2D_B ||
        !Metal2D_A->FullGpuReady() || !Metal2D_B->FullGpuReady())
    {
        return false;
    }
    if (!GPU.ScreensEnabled || GPU.CaptureEnable)
        return false;

    const uint32_t displayModeA = (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
    const uint32_t displayModeB = (GPU.GPU2D_B.DispCnt >> 16) & 0x1u;
    return displayModeA == 1u && displayModeB == 1u;
}

void MetalRenderer::Start3DRendering()
{
    if (!FullGpuState)
        FullGpuState = std::make_unique<MetalFullGpuState>();

    FullGpuState->FrameActive = IsMetalFullGpuFrameEligible();
    FullGpuState->FrameValid = FullGpuState->FrameActive;
    Metal3DSetCpuReadbackRequired(
        Rend3D.get(),
        !FullGpuState->FrameActive);
    Rend3D->RenderFrame();
}

void MetalRenderer::DrawScanline(u32 line)
{
    if (FullGpuState && FullGpuState->FrameActive)
    {
        if (!IsMetalFullGpuFrameEligible())
            FullGpuState->FrameValid = false;
        if (line < 192)
        {
            FullGpuState->ScreenSwap[line] = GPU.ScreenSwap ? 1u : 0u;
            FullGpuState->BrightnessA[line] = GPU.MasterBrightnessA;
            FullGpuState->BrightnessB[line] = GPU.MasterBrightnessB;
            Metal2D_A->CaptureScanlineState(static_cast<int>(line));
            Metal2D_B->CaptureScanlineState(static_cast<int>(line));
        }
        return;
    }
    SoftRenderer::DrawScanline(line);
}

void MetalRenderer::DrawSprites(u32 line)
{
    if (FullGpuState && FullGpuState->FrameActive)
    {
        if (line < 192)
        {
            Metal2D_A->CaptureSpriteScanlineState(static_cast<int>(line));
            Metal2D_B->CaptureSpriteScanlineState(static_cast<int>(line));
        }
        return;
    }
    SoftRenderer::DrawSprites(line);
}

void MetalRenderer::VBlankEnd()
{
    if (!(FullGpuState && FullGpuState->FrameActive))
        SoftRenderer::VBlankEnd();
}

bool MetalRenderer::ComposeMetalFullGpuOutput()
{
    if (!FullGpuState || !FullGpuState->Pipeline ||
        !OutputState || !OutputState->Ready ||
        !Metal2D_A || !Metal2D_B)
    {
        return false;
    }

    id<MTLTexture> engineA =
        (__bridge id<MTLTexture>)Metal2D_A->GetOutputTexture();
    id<MTLTexture> engineB =
        (__bridge id<MTLTexture>)Metal2D_B->GetOutputTexture();
    if (!engineA || !engineB ||
        engineA.device != OutputState->Device ||
        engineB.device != OutputState->Device ||
        engineA.width != OutputState->Width ||
        engineA.height != OutputState->Height ||
        engineB.width != OutputState->Width ||
        engineB.height != OutputState->Height)
    {
        return false;
    }

    struct FullGpuOutputConfigCpu
    {
        uint32_t outputSize[2];
        uint32_t scale;
        uint32_t outputLayer;
        uint32_t screenSwap[192];
        uint32_t brightnessA[192];
        uint32_t brightnessB[192];
    };
    static_assert(sizeof(FullGpuOutputConfigCpu) == 2320,
        "FullGpuOutputConfigCpu must match MSL");

    std::unique_lock<std::mutex> lock(OutputState->Mutex);
    int slotIndex = -1;
    for (int attempt = 0; attempt < MetalOutputState::SlotCount; attempt++)
    {
        const int candidate =
            (OutputState->NextSlot + attempt) % MetalOutputState::SlotCount;
        if (!OutputState->Slots[candidate].InFlight &&
            OutputState->Slots[candidate].PresenterRefs == 0)
        {
            slotIndex = candidate;
            break;
        }
    }
    if (slotIndex < 0)
        return false;

    MetalOutputState::Slot& slot = OutputState->Slots[slotIndex];
    OutputState->NextSlot =
        (slotIndex + 1) % MetalOutputState::SlotCount;

    id<MTLCommandBuffer> command = [OutputState->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal Full GPU Final Output";

    for (uint32_t layer = 0; layer < 2; layer++)
    {
        MTLRenderPassDescriptor* pass =
            [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = slot.FinalTexture;
        pass.colorAttachments[0].slice = layer;
        pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;

        id<MTLRenderCommandEncoder> encoder =
            [command renderCommandEncoderWithDescriptor:pass];
        if (!encoder)
            return false;

        FullGpuOutputConfigCpu config = {};
        config.outputSize[0] = static_cast<uint32_t>(OutputState->Width);
        config.outputSize[1] = static_cast<uint32_t>(OutputState->Height);
        config.scale = static_cast<uint32_t>(OutputState->Scale);
        config.outputLayer = layer;
        std::memcpy(config.screenSwap,
                    FullGpuState->CompletedScreenSwap.data(),
                    sizeof(config.screenSwap));
        std::memcpy(config.brightnessA,
                    FullGpuState->CompletedBrightnessA.data(),
                    sizeof(config.brightnessA));
        std::memcpy(config.brightnessB,
                    FullGpuState->CompletedBrightnessB.data(),
                    sizeof(config.brightnessB));

        [encoder setRenderPipelineState:FullGpuState->Pipeline];
        [encoder setFragmentBytes:&config length:sizeof(config) atIndex:0];
        [encoder setFragmentTexture:engineA atIndex:0];
        [encoder setFragmentTexture:engineB atIndex:1];
        [encoder setViewport:(MTLViewport){
            0.0, 0.0,
            static_cast<double>(OutputState->Width),
            static_cast<double>(OutputState->Height),
            0.0, 1.0 }];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:3];
        [encoder endEncoding];
    }

    const uint64_t generation = OutputState->Generation;
    const uint64_t serial = OutputState->NextSerial++;
    slot.InFlight = true;
    slot.Generation = generation;
    slot.Serial = serial;
    OutputState->InFlightCount++;
    MetalOutputState* state = OutputState.get();

    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        std::lock_guard<std::mutex> completionLock(state->Mutex);
        MetalOutputState::Slot& completedSlot = state->Slots[slotIndex];
        if (completed.status == MTLCommandBufferStatusCompleted &&
            generation == state->Generation &&
            completedSlot.Generation == generation &&
            completedSlot.Serial == serial)
        {
            state->PublishedSlot = slotIndex;
            state->PublishedSerial = serial;
        }
        completedSlot.InFlight = false;
        state->InFlightCount--;
        state->Completion.notify_all();
    }];
    [command commit];

    if (!FullGpuState->LoggedCompose)
    {
        FullGpuState->LoggedCompose = true;
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu: first GPU-resident frame published "
            "scale=%d size=%zux%zu readback=0\n",
            OutputState->Scale,
            static_cast<size_t>(OutputState->Width),
            static_cast<size_t>(OutputState->Height));
    }
    return true;
}
