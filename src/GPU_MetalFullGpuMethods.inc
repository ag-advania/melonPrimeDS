// Included after MetalOutputState is complete and before MetalRenderer's destructor.
// MELONPRIME_METAL_GPU_RESIDENT_2D_V1
// MELONPRIME_METAL_MIXED_3D_2D_OPENGL_PARITY_GUARD_V1
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1
// MELONPRIME_METAL_2D_DIRECT_SEGMENTED_CUTOVER_V2
// MELONPRIME_METAL_2D_HOT_PATH_CLEANUP_V1
// MELONPRIME_METAL_2D_SHADOW_PATH_REMOVAL_V1

namespace
{

// MELONPRIME_METAL_MIDFRAME_INVALIDATION_COOLDOWN_V1: minimum frames a
// mid-frame full-GPU invalidation blocks retry for. See MetalFullGpuState's
// MidFrameInvalidationCooldownFrames for why this needs to be a real cooldown
// rather than an immediate retry.
constexpr uint32_t kMidFrameInvalidationCooldownFrames = 60;

bool MetalSegmented2DVisibleRequested()
{
    return true;
}

bool MetalAllowExperimentalMixed3D2DGpuPath()
{
    return true;
}

bool MetalEngineAUsesMixed3DAnd2DOverlay(const GPU& gpu)
{
    const GPU2D& engine = gpu.GPU2D_A;
    const bool uses3D = (engine.DispCnt & (1u << 3)) != 0u;

    constexpr uint32_t kNon3DOverlayMask =
        (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4);
    const bool uses2DOverlay =
        (engine.LayerEnable & kNon3DOverlayMask) != 0u;

    return uses3D && uses2DOverlay;
}

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
    bool BlockedByCaptureFeedback = false;
    bool BlockedByMidFrameInvalidation = false;
    // MELONPRIME_METAL_MIDFRAME_INVALIDATION_COOLDOWN_V1
    // Some game states cause the eligibility signals to look clean at
    // Start3DRendering() (VCount==215, still in the previous frame's VBlank)
    // on every single frame, yet break down again during that same frame's
    // own scanlines every time -- a stable, deterministic oscillation that
    // clearing BlockedByMidFrameInvalidation right away does not break, since
    // the reset check re-samples the exact same unreliable snapshot. A
    // cooldown forces a minimum gap between retry attempts so a persistently
    // oscillating state fails at most once per cooldown window instead of on
    // every single frame (which is what a sustained, multi-second freeze
    // looks like to the user).
    uint32_t MidFrameInvalidationCooldownFrames = 0;
    bool LoggedEnabled = false;
    bool LoggedRejected = false;
    bool LoggedCompose = false;
    bool LoggedMixed3D2DFallback = false;
    bool LoggedMixed3D2DOverride = false;
    bool LoggedSegmentedVisible = false;
    // MELONPRIME_METAL_PER_INSTANCE_DIAGNOSTICS_V1: these two used to be
    // function-statics shared by every MetalRenderer instance in the
    // process, which mixes counts across instances (and is a plain C++
    // data race if two instances' emu threads record concurrently). Moving
    // them here makes each instance's diagnostics its own.
    int LoggedRejectionDetailCount = 0;
    MetalFullGpuFrameStats FrameStats;
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

    // MELONPRIME_METAL_FULL_GPU_DEFAULT_ON_V1 (Phase M3)
    // GPU-resident segmented 2D is the production path for the Metal
    // renderer by default now that the mid-frame invalidation paths are
    // freeze-free and crash-free (see GPU_Metal.mm's BlockedByCaptureFeedback
    // / BlockedByMidFrameInvalidation handling and the CaptureCnt-based
    // eligibility fix). Keep the env var only as an emergency kill switch for
    // comparison/regression isolation, not as the enablement gate.
    const char* env = std::getenv("MELONPRIME_METAL_FULL_GPU");
    FullGpuState->Requested = !env || env[0] != '0';
    if (!FullGpuState->Requested)
        return true;

    std::shared_ptr<MetalOutputState> outputState = LoadOutputState();
    if (!outputState || !outputState->Device)
        return false;
    if (FullGpuState->Pipeline)
        return true;

    NSError* error = nil;
    NSString* source = [[NSString alloc] initWithUTF8String:kMetalFullGpuOutputShaderSource];
    FullGpuState->Library =
        [outputState->Device newLibraryWithSource:source options:nil error:&error];
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
        [outputState->Device newRenderPipelineStateWithDescriptor:desc error:&error];
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
            "[MelonPrime] metal full-gpu: Phase 8B enabled; "
            "GPU-resident 2D and display capture skip normal-frame readback\n");
    }
    return true;
}

bool MetalRenderer::IsMetalFullGpuFrameEligible() const
{
    std::shared_ptr<MetalOutputState> outputState = LoadOutputState();
    if (!FullGpuState || !FullGpuState->Requested ||
        !FullGpuState->Pipeline || !outputState || !outputState->Ready ||
        !Metal2D_A || !Metal2D_B ||
        !Metal2D_A->FullGpuReady() || !Metal2D_B->FullGpuReady() ||
        !MetalCaptureReady())
    {
        return false;
    }
    if (!GPU.ScreensEnabled)
        return false;

    // MELONPRIME_METAL_CAPTURE_PROACTIVE_ELIGIBILITY_V1 (Phase M4 partial fix)
    // Real DS hardware captures scanline-by-scanline as the frame is drawn, so
    // a BG/OBJ that reads its own frame's capture destination bank sees a
    // line-precise read/write interleaving this segmented (whole-frame-batch)
    // 2D renderer cannot reproduce. The existing same-frame hazard check in
    // RenderSegmentedGpuFrame() (MetalSnapshotUsesCaptureTextures) is correct
    // to reject that case -- but it fires *after* DrawScanline/DrawSprites for
    // this entire frame already committed to the full-GPU path (this
    // function's result at Start3DRendering time), meaning SoftRenderer's
    // per-scanline compositing was never run for this frame either. VBlank()
    // then has no valid data source at all, so the frame freezes on the
    // previous texture instead of falling back to a correct CPU-composited
    // frame. Checking capture activity here, before that commitment is made,
    // means a rejection instead correctly runs the mature CPU/Software path
    // for the whole frame -- exactly like any other ineligible frame -- so
    // capture-heavy scenes render every frame (via CPU) instead of freezing
    // on ~90% of them. This does not fix same-frame capture-into-Metal2D; it
    // only stops the current mechanism from losing a frame's content when
    // that limitation is hit.
    //
    // Start3DRendering() (this function's caller) runs at VCount==215, which
    // is still inside the *previous* frame's VBlank -- GPU.CaptureEnable only
    // becomes true later, at VCount==0 of the frame that is about to start.
    // So GPU.CaptureEnable here always reflects the frame that just ended
    // (already cleared), never the upcoming one. The upcoming frame's capture
    // request is already visible now, though: games set CaptureCnt's start
    // bit (31) during the preceding VBlank, and hardware doesn't clear it
    // until this same VCount==192..215 window closes. Check that bit
    // directly instead of the derived, differently-timed CaptureEnable flag.
    if (GPU.CaptureCnt & (1u << 31))
        return false;

    const uint32_t displayModeA =
        (GPU.GPU2D_A.DispCnt >> 16) & 0x3u;
    const uint32_t displayModeB =
        (GPU.GPU2D_B.DispCnt >> 16) & 0x1u;
    if (displayModeA != 1u || displayModeB != 1u)
        return false;

    // OpenGL renders BG/OBJ and the final compositor in scanline segments.
    // The current Metal full-GPU path rebuilds BG/OBJ once at VBlank from the
    // last register/OAM state and applies it to all 192 lines. Mixed 3D+2D
    // gameplay frames therefore cannot yet be considered OpenGL-equivalent.
    //
    // Keep the selected Metal 3D renderer, but route final DS 2D composition
    // through the mature Software compositor until segmented Metal 2D is done.
    if (MetalEngineAUsesMixed3DAnd2DOverlay(GPU))
    {
        if (!MetalAllowExperimentalMixed3D2DGpuPath())
            return false;

        if (MetalSegmented2DVisibleRequested() &&
            (!Metal2D_A->SegmentedSnapshotReady() ||
             !Metal2D_B->SegmentedSnapshotReady()))
        {
            return false;
        }
    }

    return true;
}

void MetalRenderer::Start3DRendering()
{
    if (!FullGpuState)
        FullGpuState = std::make_unique<MetalFullGpuState>();

    if (!GPU.CaptureEnable)
        FullGpuState->BlockedByCaptureFeedback = false;

    // MELONPRIME_METAL_MIDFRAME_INVALIDATION_STICKY_V1
    // Every signal this eligibility check reads (CaptureCnt's start bit,
    // MetalCaptureResourcesCoherent()'s BG/OBJ capture-layer validity, ...) is
    // sampled here at VCount==215, still inside the *previous* frame's
    // VBlank. The game can still write new register state for the upcoming
    // frame after this point, and DrawScanline()'s own re-check (running
    // during this frame's actual VCount 0-191) can discover a worse answer
    // than what was true here. When that happens the frame has already
    // committed to the full-GPU DrawScanline path, so SoftRenderer's
    // per-scanline compositing never ran either -- the frame has no valid
    // 2D data source at all and freezes. Without this flag, a persistent
    // per-frame register pattern (e.g. a BG/OBJ config that keeps
    // reintroducing the same conflict every single frame) turns that into a
    // sustained multi-second freeze, not a single dropped frame, because
    // eligibility looks clean again at every next Start3DRendering call.
    // Sticky-blocking after any mid-frame invalidation caps the damage at one
    // frozen frame per invalidation episode: subsequent frames correctly
    // predict ineligibility in advance (via this flag) and fall back to the
    // mature CPU/Software path instead of repeating the same failed attempt.
    if (FullGpuState->MidFrameInvalidationCooldownFrames > 0)
        FullGpuState->MidFrameInvalidationCooldownFrames--;

    if (FullGpuState->BlockedByMidFrameInvalidation)
    {
        // Only lift the sticky block once the cooldown has fully elapsed AND
        // this frame's own (already-current) snapshot looks eligible. The
        // cooldown is what actually breaks a deterministic every-frame
        // oscillation; without it, a state where this snapshot looks eligible
        // on every single Start3DRendering call (even though it reliably
        // breaks down again during that same frame's own scanlines) would
        // clear the block immediately every time, right back into the same
        // failing attempt.
        if (FullGpuState->MidFrameInvalidationCooldownFrames == 0 &&
            MetalCaptureResourcesCoherent() && IsMetalFullGpuFrameEligible())
        {
            FullGpuState->BlockedByMidFrameInvalidation = false;
        }
    }

    FullGpuState->FrameActive =
        !FullGpuState->BlockedByCaptureFeedback &&
        !FullGpuState->BlockedByMidFrameInvalidation &&
        MetalCaptureResourcesCoherent() &&
        IsMetalFullGpuFrameEligible();
    FullGpuState->FrameValid = FullGpuState->FrameActive;
    if (FullGpuState->FrameActive)
        BeginMetalCaptureFrame();

    Metal3DSetCpuReadbackRequired(
        Rend3D.get(),
        !FullGpuState->FrameActive);
    Rend3D->RenderFrame();
}

void MetalRenderer::DrawScanline(u32 line)
{
    if (FullGpuState && FullGpuState->FrameActive)
    {
        if (!IsMetalFullGpuFrameEligible() ||
            (!GPU.CaptureEnable && !MetalCaptureResourcesCoherent()))
        {
            FullGpuState->FrameValid = false;
            FullGpuState->BlockedByMidFrameInvalidation = true;
            FullGpuState->MidFrameInvalidationCooldownFrames =
                kMidFrameInvalidationCooldownFrames;
        }
        if (line < 192)
        {
            FullGpuState->ScreenSwap[line] = GPU.ScreenSwap ? 1u : 0u;
            FullGpuState->BrightnessA[line] = GPU.MasterBrightnessA;
            FullGpuState->BrightnessB[line] = GPU.MasterBrightnessB;
            Metal2D_A->CaptureScanlineState(static_cast<int>(line));
            Metal2D_B->CaptureScanlineState(static_cast<int>(line));
            CaptureMetalDisplayCaptureLine(line);
            if (!MetalCaptureFrameSupported())
            {
                FullGpuState->FrameValid = false;
                FullGpuState->BlockedByMidFrameInvalidation = true;
                FullGpuState->MidFrameInvalidationCooldownFrames =
                    kMidFrameInvalidationCooldownFrames;
            }
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
    std::shared_ptr<MetalOutputState> state = LoadOutputState();
    if (!FullGpuState || !FullGpuState->Pipeline ||
        !state || !state->Ready ||
        !Metal2D_A || !Metal2D_B)
    {
        return false;
    }

    id<MTLTexture> engineA =
        (__bridge id<MTLTexture>)Metal2D_A->GetOutputTexture();
    id<MTLTexture> engineB =
        (__bridge id<MTLTexture>)Metal2D_B->GetOutputTexture();
    if (!engineA || !engineB ||
        engineA.device != state->Device ||
        engineB.device != state->Device ||
        engineA.width != state->Width ||
        engineA.height != state->Height ||
        engineB.width != state->Width ||
        engineB.height != state->Height)
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

    std::unique_lock<std::mutex> lock(state->Mutex);
    int slotIndex = -1;
    for (int attempt = 0; attempt < MetalOutputState::SlotCount; attempt++)
    {
        const int candidate =
            (state->NextSlot + attempt) % MetalOutputState::SlotCount;
        // Never reuse the currently published slot; see the matching guard in
        // ComposeMetalVisibleOutput for the presenter-lease race this closes.
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
        return false;

    MetalOutputState::Slot& slot = state->Slots[slotIndex];
    state->NextSlot =
        (slotIndex + 1) % MetalOutputState::SlotCount;

    id<MTLCommandBuffer> command = [state->Queue commandBuffer];
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
        config.outputSize[0] = static_cast<uint32_t>(state->Width);
        config.outputSize[1] = static_cast<uint32_t>(state->Height);
        config.scale = static_cast<uint32_t>(state->Scale);
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
            static_cast<double>(state->Width),
            static_cast<double>(state->Height),
            0.0, 1.0 }];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                    vertexStart:0
                    vertexCount:3];
        [encoder endEncoding];
    }

    const uint64_t generation = state->Generation;
    const uint64_t serial = state->NextSerial++;
    slot.InFlight = true;
    slot.Generation = generation;
    slot.Serial = serial;
    state->InFlightCount++;

    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        std::shared_ptr<MetalOutputState> completedState = state;
        std::lock_guard<std::mutex> completionLock(completedState->Mutex);
        MetalOutputState::Slot& completedSlot = completedState->Slots[slotIndex];
        if (completed.status == MTLCommandBufferStatusCompleted &&
            generation == completedState->Generation &&
            completedSlot.Generation == generation &&
            completedSlot.Serial == serial)
        {
            completedState->PublishedSlot = slotIndex;
            completedState->PublishedSerial = serial;
        }
        completedSlot.InFlight = false;
        completedState->InFlightCount--;
        completedState->Completion.notify_all();
    }];
    [command commit];

    if (!FullGpuState->LoggedCompose)
    {
        FullGpuState->LoggedCompose = true;
        std::fprintf(stderr,
            "[MelonPrime] metal full-gpu: first GPU-resident frame published "
            "scale=%d size=%zux%zu readback=0\n",
            state->Scale,
            static_cast<size_t>(state->Width),
            static_cast<size_t>(state->Height));
    }
    return true;
}
