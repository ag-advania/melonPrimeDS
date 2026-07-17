// Included after GPU_MetalFullGpuMethods.inc.
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1

namespace
{
static constexpr const char* kMetalDisplayCaptureShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

// MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1
// Canonical capture authority is DS-native RGB5551 in R16Uint textures.
// Scale-expanded RGBA8 arrays are no longer the storage format.

struct CaptureLineConfig
{
    uint enabled;
    uint dispCntA;
    uint captureCnt;
    int srcAXOffset;
    int srcBCaptureLayer;
    uint srcBCaptureSize;
    uint srcBNativeY;
    uint pad0;
};

struct CaptureDispatchConfig
{
    uint scale;
    uint lineOffset;
    uint2 pad0;
};

static inline float4 mp_unpack_rgba8(uint packed)
{
    return float4(
        float(packed & 0xFFu),
        float((packed >> 8u) & 0xFFu),
        float((packed >> 16u) & 0xFFu),
        float((packed >> 24u) & 0xFFu)) / 255.0;
}

static inline float4 mp_unpack_rgb5551(ushort value)
{
    uint r5 = value & 0x1Fu;
    uint g5 = (value >> 5u) & 0x1Fu;
    uint b5 = (value >> 10u) & 0x1Fu;
    uint a1 = (value >> 15u) & 1u;
    float r = float((r5 << 3u) | (r5 >> 2u)) / 255.0;
    float g = float((g5 << 3u) | (g5 >> 2u)) / 255.0;
    float b = float((b5 << 3u) | (b5 >> 2u)) / 255.0;
    return float4(r, g, b, float(a1));
}

static inline ushort mp_pack_rgb5551_components(uint3 rgb5, uint a1)
{
    return ushort(
        (rgb5.x & 0x1Fu) |
        ((rgb5.y & 0x1Fu) << 5u) |
        ((rgb5.z & 0x1Fu) << 10u) |
        ((a1 & 1u) << 15u));
}

static inline uint3 mp_float_to_rgb5(float4 color)
{
    uint4 a8 = uint4(clamp(color, 0.0, 1.0) * 255.0);
    return a8.rgb >> 3u;
}

static inline uint mp_float_to_a1(float4 color)
{
    return uint4(clamp(color, 0.0, 1.0) * 255.0).a > 0u ? 1u : 0u;
}

static inline float4 mp_capture_source_b(
    constant CaptureLineConfig& lineConfig,
    uint nativeX,
    device const uint* auxVram,
    device const uint* auxFifo,
    texture2d_array<ushort, access::read> snapshot128,
    texture2d_array<ushort, access::read> snapshot256)
{
    const bool fifo = (lineConfig.captureCnt & (1u << 25u)) != 0u;
    if (fifo)
        return mp_unpack_rgba8(auxFifo[(lineConfig.srcBNativeY * 256u) + nativeX]);

    if (lineConfig.srcBCaptureLayer < 0)
        return mp_unpack_rgba8(auxVram[(lineConfig.srcBNativeY * 256u) + nativeX]);

    uint layer = uint(lineConfig.srcBCaptureLayer);
    if (lineConfig.srcBCaptureSize == 0u)
    {
        uint startBlock = layer & 3u;
        uint blockBaseY = startBlock * 64u;
        uint relativeY = (lineConfig.srcBNativeY + 256u - blockBaseY) & 0xFFu;
        uint linearIndex = relativeY * 256u + nativeX;
        uint capX = linearIndex & 0x7Fu;
        uint capY = linearIndex >> 7u;
        if (capY >= 128u)
            return float4(0.0);

        uint2 coord = min(uint2(capX, capY), uint2(
            max(snapshot128.get_width(), 1u) - 1u,
            max(snapshot128.get_height(), 1u) - 1u));
        return mp_unpack_rgb5551(snapshot128.read(coord, layer).r);
    }

    uint2 coord = min(uint2(nativeX, lineConfig.srcBNativeY), uint2(
        max(snapshot256.get_width(), 1u) - 1u,
        max(snapshot256.get_height(), 1u) - 1u));
    return mp_unpack_rgb5551(snapshot256.read(coord, layer >> 2u).r);
}

kernel void mp_metal_display_capture(
    constant CaptureLineConfig* lines [[buffer(0)]],
    constant CaptureDispatchConfig& dispatchConfig [[buffer(1)]],
    device const uint* auxVram [[buffer(2)]],
    device const uint* auxFifo [[buffer(3)]],
    texture2d<float, access::read> engineA2D [[texture(0)]],
    texture2d<float, access::read> engineA3D [[texture(1)]],
    texture2d_array<ushort, access::read> snapshot128 [[texture(2)]],
    texture2d_array<ushort, access::read> snapshot256 [[texture(3)]],
    texture2d_array<ushort, access::write> capture128 [[texture(4)]],
    texture2d_array<ushort, access::write> capture256 [[texture(5)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint scale = max(dispatchConfig.scale, 1u);
    uint nativeX = gid.x;
    uint nativeLine = gid.y + dispatchConfig.lineOffset;
    if (nativeX >= 256u || nativeLine >= 192u)
        return;

    constant CaptureLineConfig& config = lines[nativeLine];
    if (config.enabled == 0u)
        return;

    uint captureSize = (config.captureCnt >> 20u) & 3u;
    uint dstWidth = captureSize == 0u ? 128u : 256u;
    uint dstHeight = captureSize == 0u ? 128u : 64u * captureSize;
    if (nativeX >= dstWidth || nativeLine >= dstHeight)
        return;

    uint srcA = (config.captureCnt >> 24u) & 1u;
    float4 colorA = float4(0.0);
    if (srcA == 0u)
    {
        uint2 coord = uint2(
            nativeX * scale + scale / 2u,
            nativeLine * scale + scale / 2u);
        coord = min(coord, uint2(
            max(engineA2D.get_width(), 1u) - 1u,
            max(engineA2D.get_height(), 1u) - 1u));
        colorA = engineA2D.read(coord);
    }
    else
    {
        int sourceX = int(nativeX) + config.srcAXOffset;
        if (sourceX >= 0 && sourceX < 256)
        {
            uint2 coord = uint2(
                uint(sourceX) * scale + scale / 2u,
                nativeLine * scale + scale / 2u);
            coord = min(coord, uint2(
                max(engineA3D.get_width(), 1u) - 1u,
                max(engineA3D.get_height(), 1u) - 1u));
            colorA = engineA3D.read(coord);
        }
    }

    float4 colorB = mp_capture_source_b(
        config, nativeX, auxVram, auxFifo, snapshot128, snapshot256);

    uint dstMode = (config.captureCnt >> 29u) & 3u;
    ushort output;
    if (dstMode == 0u)
    {
        output = mp_pack_rgb5551_components(
            mp_float_to_rgb5(colorA), mp_float_to_a1(colorA));
    }
    else if (dstMode == 1u)
    {
        output = mp_pack_rgb5551_components(
            mp_float_to_rgb5(colorB), mp_float_to_a1(colorB));
    }
    else
    {
        uint eva = min(config.captureCnt & 0x1Fu, 16u);
        uint evb = min((config.captureCnt >> 8u) & 0x1Fu, 16u);
        uint3 a5 = mp_float_to_rgb5(colorA);
        uint3 b5 = mp_float_to_rgb5(colorB);
        uint alphaA = mp_float_to_a1(colorA);
        uint alphaB = mp_float_to_a1(colorB);
        uint3 out5 = min(
            ((a5 * alphaA * eva) + (b5 * alphaB * evb) + 8u) >> 4u,
            uint3(31u));
        uint outAlpha =
            ((eva > 0u) ? alphaA : 0u) |
            ((evb > 0u) ? alphaB : 0u);
        output = mp_pack_rgb5551_components(out5, outAlpha);
    }

    ushort4 packed = ushort4(output, 0u, 0u, 0u);
    uint dstBank = (config.captureCnt >> 16u) & 3u;
    uint dstOffset = (config.captureCnt >> 18u) & 3u;
    if (captureSize == 0u)
    {
        uint layer = (dstBank << 2u) | dstOffset;
        capture128.write(packed, uint2(nativeX, nativeLine), layer);
    }
    else
    {
        uint dstNativeY = (dstOffset * 64u + nativeLine) & 0xFFu;
        capture256.write(packed, uint2(nativeX, dstNativeY), dstBank);
    }
}
)MSL";

struct MetalCaptureLineConfigCpu
{
    uint32_t Enabled = 0;
    uint32_t DispCntA = 0;
    uint32_t CaptureCnt = 0;
    int32_t SrcAXOffset = 0;
    int32_t SrcBCaptureLayer = -1;
    uint32_t SrcBCaptureSize = 0;
    uint32_t SrcBNativeY = 0;
    uint32_t Pad0 = 0;
};
static_assert(sizeof(MetalCaptureLineConfigCpu) == 32);

struct MetalCaptureDispatchConfigCpu
{
    uint32_t Scale = 1;
    uint32_t LineOffset = 0;
    uint32_t Pad[2] = {};
};
static_assert(sizeof(MetalCaptureDispatchConfigCpu) == 16);

static uint32_t MetalPackRgb5551AsRgba8(uint16_t color)
{
    const uint32_t r5 = color & 0x1Fu;
    const uint32_t g5 = (color >> 5u) & 0x1Fu;
    const uint32_t b5 = (color >> 10u) & 0x1Fu;
    const uint32_t r8 = (r5 << 3u) | (r5 >> 2u);
    const uint32_t g8 = (g5 << 3u) | (g5 >> 2u);
    const uint32_t b8 = (b5 << 3u) | (b5 >> 2u);
    const uint32_t a8 = (color & 0x8000u) ? 0xFFu : 0u;
    return r8 | (g8 << 8u) | (b8 << 16u) | (a8 << 24u);
}
}

struct MetalRenderer::MetalCaptureState
{
    // MELONPRIME_METAL_CAPTURE_METADATA_STATE_MACHINE_V1: Meta is read and
    // written from two different threads -- the emulation/render thread
    // (every function below except the completion handlers) and Metal's own
    // completion-queue dispatch thread (UploadCaptureTexture's and
    // EncodeMetalDisplayCapture's completion handlers) -- so every access
    // must go through MetaMutex. Dirty/CpuUploadInFlight/GpuWritesInFlight/
    // DirtySerial/CpuSubmittedSerial/GpuSubmittedSerial/LatestGpuSubmittedToken
    // replace the old single PendingCpuUpload+PendingUploadSerial pair (and
    // the later shared UploadInFlight+SubmittedSerial, then the bool
    // GpuCaptureInFlight) so that (a) a write already in flight is never
    // resubmitted every frame just because it has not completed yet within
    // one frame (the old design's livelock under GPU backpressure),
    // (b) a completion can tell a merely-superseded write (fine, some other
    // write will finalize this layer) apart from data that changed again
    // after this particular write was dispatched (must NOT be marked valid),
    // (c) CPU-fallback upload and GPU Display Capture can be in flight for
    // different DirtySerial generations without one writer's completion
    // clearing the other writer's in-flight state, and (d) overlapping GPU
    // capture command buffers are counted (GpuWritesInFlight) with a
    // monotonic token so an older completion cannot clear the bit while a
    // newer command is still running.
    struct CaptureMeta
    {
        int Size = -1;
        bool Valid = false;
        // This layer's destination data (VRAM contents or capture
        // configuration) has changed since the last write that completed
        // successfully, so it needs a fresh write (GPU capture or CPU
        // fallback) before Valid can be trusted again.
        bool Dirty = false;
        // A CPU-fallback upload for this layer has been dispatched and its
        // completion has not yet run. While true, a new CPU upload for the
        // same DirtySerial must not be dispatched again.
        bool CpuUploadInFlight = false;
        // Number of GPU Display Capture command buffers that have been
        // submitted for this layer and have not yet completed. A plain bool
        // cannot represent overlapping GPU captures (H-01): an older
        // completion must not clear the in-flight bit while a newer command
        // is still running. CPU upload gating uses GpuWritesInFlight == 0.
        uint32_t GpuWritesInFlight = 0;
        // Bumped whenever the underlying data actually changes (AllocCapture,
        // a capture-destination reconfigure). A write dispatch snapshots the
        // current value into the writer-specific SubmittedSerial; a
        // completion only trusts its own result if DirtySerial still equals
        // the SubmittedSerial it snapshotted -- otherwise something changed
        // the data again while the write was in flight and the result must
        // not be applied.
        uint64_t DirtySerial = 0;
        uint64_t CpuSubmittedSerial = 0;
        uint64_t GpuSubmittedSerial = 0;
        // Monotonic token of the most recently submitted GPU capture write
        // for this layer. Stale completions (token != LatestGpuSubmittedToken)
        // only decrement GpuWritesInFlight and must not touch Valid/Dirty.
        uint64_t LatestGpuSubmittedToken = 0;
        // MELONPRIME_METAL_CAPTURE_WRITE_TICKET_V1: published generation for
        // this layer. Bumped on AllocCapture and when a write is accepted.
        // Old operation completions must not finalize a newer Generation.
        uint64_t Generation = 0;
        // Expected Generation of the latest in-flight GPU write (snapshotted
        // into CaptureWriteTicket at dispatch).
        uint64_t LatestGpuSubmittedGeneration = 0;
    };

    // MELONPRIME_METAL_CAPTURE_WRITE_TICKET_V1 (§10.11): per-operation token
    // so an old completion cannot finalize a newer generation.
    struct CaptureWriteTicket
    {
        uint64_t FrameSerial = 0;
        uint32_t SegmentIndex = 0;
        uint32_t Layer = 0;
        uint64_t DirtySerial = 0;
        uint64_t Generation = 0;
        uint64_t Token = 0;
        int Size = -1;
    };

    // Guards every field of every Meta[] entry. Never held across GPU command
    // encoding/submission -- only around the metadata reads/writes
    // themselves, both on the emulation thread and inside completion
    // handlers.
    std::mutex MetaMutex;

    // Global monotonic token source for GPU Display Capture dispatches
    // (shared across layers; each pending write carries its own token).
    uint64_t NextGpuWriteToken = 1;

    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLComputePipelineState> CapturePipeline = nil;
    // MELONPRIME_METAL_CAPTURE_PINGPONG_V1 (§10.6 A): read/write faces per
    // size class. PublishedIndex is what 2D/3D/Source-B sample; encode writes
    // the opposite face then publishes it after commit.
    id<MTLTexture> Capture128[2] = {nil, nil};
    id<MTLTexture> Capture256[2] = {nil, nil};
    uint8_t PublishedIndex = 0;
    uint64_t FrameSerial = 0;
    uint32_t NextSegmentIndex = 0;
    id<MTLBuffer> LineConfigBuffer = nil;
    id<MTLBuffer> AuxVramBuffer = nil;
    id<MTLBuffer> AuxFifoBuffer = nil;
    id<MTLBuffer> ReadbackBuffer = nil;
    std::array<CaptureMeta, 16> Meta {};
    int Scale = 0;
    bool Ready = false;
    bool FrameHadCapture = false;
    bool FrameSupported = true;
    bool LoggedReady = false;
    bool LoggedFirstCapture = false;
    bool LoggedOnDemandReadback = false;
    bool LoggedCpuFallbackUpload = false;
    bool LoggedIncompleteCaptureSync = false;
    // MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1 /
    // MELONPRIME_METAL_CAPTURE_CPU_UPLOAD_SCRATCH_V1: native RGB5551 upload
    // scratch (max 256x256x2 = 128 KiB). Scale-expanded RGBA8 staging is gone.
    std::vector<uint16_t> CpuUploadScratch;

    // MELONPRIME_METAL_CAPTURE_STAGING_RING_V1: reusable shared MTLBuffers for
    // native RGB5551 uploads. Cap is 128 KiB (one full 256x256 layer).
    struct StagingBuffer
    {
        id<MTLBuffer> Buffer = nil;
        NSUInteger Capacity = 0;
        std::shared_ptr<std::atomic<bool>> Busy =
            std::make_shared<std::atomic<bool>>(false);
    };
    static constexpr int kStagingRingSize = 4;
    static constexpr NSUInteger kMaxCaptureStagingBytes = 256u * 256u * sizeof(uint16_t);
    std::array<StagingBuffer, kStagingRingSize> StagingRing;
    int StagingRingCursor = 0;
    bool LoggedStagingRingExhausted = false;
};

// Returns an idle ring slot with enough capacity, growing it in place if
// needed, or nullptr if every slot is currently busy (caller falls back to
// a one-off allocation). Never resizes the ring itself, so indices and the
// Busy shared_ptr identity stay stable across calls. A member function (not
// a free function in an anonymous namespace) because MetalCaptureState is
// private; the header only sees this through an opaque void*.
void* MetalRenderer::AcquireStagingSlot(MetalCaptureState& state, unsigned long requiredBytesArg)
{
    const NSUInteger requiredBytes = static_cast<NSUInteger>(requiredBytesArg);
    // Cap persistent ring growth to one native 256x256 RGB5551 layer.
    if (requiredBytes > MetalCaptureState::kMaxCaptureStagingBytes)
        return nullptr;
    for (int i = 0; i < MetalCaptureState::kStagingRingSize; i++)
    {
        const int idx = (state.StagingRingCursor + i) % MetalCaptureState::kStagingRingSize;
        auto& slot = state.StagingRing[idx];
        if (slot.Busy->load(std::memory_order_acquire))
            continue;
        if (slot.Capacity < requiredBytes)
        {
            id<MTLBuffer> grown =
                [state.Device newBufferWithLength:requiredBytes
                                           options:MTLResourceStorageModeShared];
            if (!grown)
                continue;
            slot.Buffer = grown;
            slot.Capacity = requiredBytes;
        }
        state.StagingRingCursor = (idx + 1) % MetalCaptureState::kStagingRingSize;
        return &slot;
    }
    return nullptr;
}

bool MetalRenderer::ConfigureMetalCaptureState(void* preferredDevice)
{
    if (!FullGpuState || !FullGpuState->Requested)
        return true;
    std::shared_ptr<MetalOutputState> outputState = LoadOutputState();
    if (!outputState || !outputState->Device || !outputState->Queue)
        return false;

    id<MTLDevice> device = (__bridge id<MTLDevice>)preferredDevice;
    if (!device)
        device = outputState->Device;
    id<MTLCommandQueue> queue = outputState->Queue;
    if (!device || !queue || queue.device != device)
        return false;

    const int scale = std::max(1, ScaleFactor);
    // MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1: canonical capture textures
    // are native-resolution and scale-independent. Scale only affects how
    // hi-res Engine A / 3D sources are sampled when encoding capture.
    if (CaptureState && CaptureState->Ready &&
        CaptureState->Device == device &&
        CaptureState->Queue == queue)
    {
        CaptureState->Scale = scale;
        return true;
    }

    std::array<MetalCaptureState::CaptureMeta, 16> recoveredMeta {};
    if (CaptureState && CaptureState->Ready)
    {
        {
            // Locked snapshot: an in-flight completion on this (about to be
            // replaced) CaptureState can still be writing Meta concurrently.
            std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
            recoveredMeta = CaptureState->Meta;
        }
        for (int layer = 0; layer < 16; layer++)
        {
            const auto& meta = recoveredMeta[layer];
            if (!meta.Valid || meta.Size < 0)
                continue;
            const int bank = layer >> 2;
            const int start = layer & 3;
            if (GPU.GetCaptureBlock_LCDC(
                    (static_cast<u32>(bank) << 17) |
                    (static_cast<u32>(start) << 15)) == layer)
            {
                SyncVRAMCapture(
                    static_cast<u32>(bank),
                    static_cast<u32>(start),
                    static_cast<u32>(meta.Size),
                    true);
            }
        }
    }

    auto next = std::make_shared<MetalCaptureState>();
    next->Meta = recoveredMeta;
    for (auto& meta : next->Meta)
    {
        if (meta.Size >= 0)
        {
            meta.Valid = false;
            meta.Dirty = true;
            meta.DirtySerial++;
            meta.CpuUploadInFlight = false;
            meta.GpuWritesInFlight = 0;
            meta.LatestGpuSubmittedToken = 0;
        }
    }
    next->Device = device;
    next->Queue = queue;
    next->Scale = scale;

    NSError* error = nil;
    NSString* source =
        [[NSString alloc] initWithUTF8String:kMetalDisplayCaptureShaderSource];
    next->Library = [device newLibraryWithSource:source options:nil error:&error];
    if (!next->Library)
    {
        const char* message =
            error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: shader compile failed: %s\n",
            message);
        return false;
    }

    id<MTLFunction> captureFunction =
        [next->Library newFunctionWithName:@"mp_metal_display_capture"];
    if (!captureFunction)
        return false;

    next->CapturePipeline =
        [device newComputePipelineStateWithFunction:captureFunction error:&error];
    if (!next->CapturePipeline)
    {
        const char* message =
            error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: capture pipeline failed: %s\n",
            message);
        return false;
    }

    auto makeArrayTexture = [&](NSUInteger width,
                                NSUInteger height,
                                NSUInteger layers,
                                MTLTextureUsage usage)
        -> id<MTLTexture> {
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor
                texture2DDescriptorWithPixelFormat:MTLPixelFormatR16Uint
                                             width:width
                                            height:height
                                         mipmapped:NO];
        desc.textureType = MTLTextureType2DArray;
        desc.arrayLength = layers;
        desc.usage = usage;
        desc.storageMode = MTLStorageModePrivate;
        return [device newTextureWithDescriptor:desc];
    };

    // MELONPRIME_METAL_CAPTURE_PINGPONG_V1: two R16Uint faces per size.
    // Memory ~2 MiB total (replaces Capture+Snapshot pair).
    next->Capture128[0] = makeArrayTexture(
        128u, 128u, 16u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->Capture128[1] = makeArrayTexture(
        128u, 128u, 16u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->Capture256[0] = makeArrayTexture(
        256u, 256u, 4u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->Capture256[1] = makeArrayTexture(
        256u, 256u, 4u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->PublishedIndex = 0;

    next->LineConfigBuffer =
        [device newBufferWithLength:
                    sizeof(MetalCaptureLineConfigCpu) * 192u
                         options:MTLResourceStorageModeShared];
    next->AuxVramBuffer =
        [device newBufferWithLength:
                    sizeof(uint32_t) * 256u * 192u
                         options:MTLResourceStorageModeShared];
    next->AuxFifoBuffer =
        [device newBufferWithLength:
                    sizeof(uint32_t) * 256u * 192u
                         options:MTLResourceStorageModeShared];
    next->ReadbackBuffer =
        [device newBufferWithLength:
                    sizeof(uint16_t) * 256u * 256u
                         options:MTLResourceStorageModeShared];

    if (!next->Capture128[0] || !next->Capture128[1] ||
        !next->Capture256[0] || !next->Capture256[1] ||
        !next->LineConfigBuffer || !next->AuxVramBuffer ||
        !next->AuxFifoBuffer || !next->ReadbackBuffer)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: allocation failed scale=%d\n",
            scale);
        return false;
    }

    std::memset([next->LineConfigBuffer contents], 0,
        sizeof(MetalCaptureLineConfigCpu) * 192u);
    std::memset([next->AuxVramBuffer contents], 0,
        sizeof(uint32_t) * 256u * 192u);
    std::memset([next->AuxFifoBuffer contents], 0,
        sizeof(uint32_t) * 256u * 192u);

    next->Ready = true;
    next->LoggedReady = true;
    std::fprintf(stderr,
        "[MelonPrime] metal display capture: configured native R16Uint "
        "ping-pong scaleSample=%d capture128=%zux%zux16 capture256=%zux%zux4 "
        "(scale-independent canonical storage)\n",
        scale,
        static_cast<size_t>(next->Capture128[0].width),
        static_cast<size_t>(next->Capture128[0].height),
        static_cast<size_t>(next->Capture256[0].width),
        static_cast<size_t>(next->Capture256[0].height));

    CaptureState = std::move(next);

    // MELONPRIME_METAL_COMPUTE_CAPTURE_TEXTURE_WIRE_V1: hand the (re)created
    // native R16Uint capture texture arrays to the compute 3D renderer.
    // Recreate only on device/queue change -- scale changes keep these
    // textures (canonical storage is scale-independent).
    Metal3DSetCaptureTextures(
        Rend3D.get(),
        GetMetalCapture128Texture(),
        GetMetalCapture256Texture());

    if (!UploadCpuCompletedCaptures())
        return false;
    return true;
}

void MetalRenderer::BeginMetalCaptureFrame()
{
    if (!CaptureState || !CaptureState->Ready)
        return;
    CaptureState->FrameHadCapture = false;
    CaptureState->FrameSupported = true;
    CaptureState->FrameSerial++;
    CaptureState->NextSegmentIndex = 0;
    auto* lines = static_cast<MetalCaptureLineConfigCpu*>(
        [CaptureState->LineConfigBuffer contents]);
    for (int line = 0; line < 192; line++)
        lines[line] = {};
}

void MetalRenderer::CaptureMetalDisplayCaptureLine(u32 line)
{
    if (!CaptureState || !CaptureState->Ready || line >= 192)
        return;

    auto* lines = static_cast<MetalCaptureLineConfigCpu*>(
        [CaptureState->LineConfigBuffer contents]);
    MetalCaptureLineConfigCpu& config = lines[line];
    config = {};

    const u32 captureCnt = GPU.CaptureCnt;
    const u32 dstBank = (captureCnt >> 16) & 0x3;
    const bool enabled =
        GPU.CaptureEnable &&
        ((GPU.VRAMMap_LCDC & (1u << dstBank)) != 0u);
    if (!enabled)
        return;

    CaptureState->FrameHadCapture = true;
    config.Enabled = 1;
    config.DispCntA = GPU.GPU2D_A.DispCnt;
    config.CaptureCnt = captureCnt;
    config.SrcBCaptureLayer = -1;

    if ((captureCnt & (1u << 24)) != 0u)
    {
        int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
        xpos -= ((xpos & 0x100) << 1);
        config.SrcAXOffset = xpos;
    }

    const u32 dstMode = (captureCnt >> 29) & 0x3;
    const u32 evb = std::min((captureCnt >> 8) & 0x1Fu, 16u);
    const bool useSourceB =
        dstMode == 1u || ((dstMode == 2u || dstMode == 3u) && evb > 0u);
    if (!useSourceB)
        return;

    const bool fifo = (captureCnt & (1u << 25)) != 0u;
    config.SrcBNativeY = line;
    uint32_t* target = nullptr;
    if (fifo)
    {
        target = static_cast<uint32_t*>(
            [CaptureState->AuxFifoBuffer contents]) + line * 256u;
        for (u32 x = 0; x < 256; x++)
            target[x] = MetalPackRgb5551AsRgba8(GPU.DispFIFOBuffer[x]);
        return;
    }

    const u32 dispCntA = GPU.GPU2D_A.DispCnt;
    const u32 displayMode = (dispCntA >> 16) & 0x3;
    const u32 srcBank = (dispCntA >> 18) & 0x3;
    const u32 srcOffset =
        displayMode == 2u ? 0u : ((captureCnt >> 26) & 0x3);
    const u32 srcY = (line + srcOffset * 64u) & 0xFFu;
    const u32 srcBlock = srcY >> 6u;
    config.SrcBNativeY = srcY;

    const int captureLayer = GPU.GetCaptureBlock_LCDC(
        (srcBank << 17u) | (srcBlock << 15u));
    if (captureLayer >= 0)
    {
        bool valid;
        int size;
        {
            std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
            valid = CaptureState->Meta[captureLayer].Valid;
            size = CaptureState->Meta[captureLayer].Size;
        }
        if (!valid || size < 0)
        {
            CaptureState->FrameSupported = false;
            return;
        }
        config.SrcBCaptureLayer = captureLayer;
        config.SrcBCaptureSize = static_cast<uint32_t>(size);
        return;
    }

    target = static_cast<uint32_t*>(
        [CaptureState->AuxVramBuffer contents]) + line * 256u;
    if ((GPU.VRAMMap_LCDC & (1u << srcBank)) == 0u)
    {
        std::fill(target, target + 256, 0u);
        return;
    }

    const uint16_t* source =
        reinterpret_cast<const uint16_t*>(GPU.VRAM[srcBank]) +
        srcY * 256u;
    for (u32 x = 0; x < 256; x++)
        target[x] = MetalPackRgb5551AsRgba8(source[x]);
}

bool MetalRenderer::EncodeMetalDisplayCapture(
    void* engineA2DTexture,
    void* high3DTexture,
    int startLine,
    int endLine,
    uint32_t segmentIndex)
{
    if (startLine < 0) startLine = 0;
    if (endLine > 192) endLine = 192;
    if (startLine >= endLine)
        return true;
    if (!CaptureState || !CaptureState->Ready)
        return false;
    if (!CaptureState->FrameSupported)
        return false;
    if (!CaptureState->FrameHadCapture)
        return true;

    id<MTLTexture> engineA2D =
        (__bridge id<MTLTexture>)engineA2DTexture;
    id<MTLTexture> high3D =
        (__bridge id<MTLTexture>)high3DTexture;
    if (!engineA2D || !high3D ||
        engineA2D.device != CaptureState->Device ||
        high3D.device != CaptureState->Device)
    {
        return false;
    }

    auto* lines = static_cast<MetalCaptureLineConfigCpu*>(
        [CaptureState->LineConfigBuffer contents]);

    const uint8_t readFace = CaptureState->PublishedIndex;
    const uint8_t writeFace = static_cast<uint8_t>(1u - readFace);
    id<MTLTexture> read128 = CaptureState->Capture128[readFace];
    id<MTLTexture> read256 = CaptureState->Capture256[readFace];
    id<MTLTexture> write128 = CaptureState->Capture128[writeFace];
    id<MTLTexture> write256 = CaptureState->Capture256[writeFace];
    if (!read128 || !read256 || !write128 || !write256)
        return false;

    id<MTLCommandBuffer> command = [CaptureState->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal GPU Display Capture";

    // MELONPRIME_METAL_CAPTURE_PINGPONG_V1: keep write face complete by
    // copying published read face, then overwrite written layers via compute.
    // Source B samples the read face (same-frame prior writes).
    {
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit)
            return false;
        [blit copyFromTexture:read128
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(read128.width, read128.height, 1)
                    toTexture:write128
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        // Full array: copy remaining slices.
        for (NSUInteger slice = 1; slice < read128.arrayLength; slice++)
        {
            [blit copyFromTexture:read128
                      sourceSlice:slice
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(read128.width, read128.height, 1)
                        toTexture:write128
                 destinationSlice:slice
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        for (NSUInteger slice = 0; slice < read256.arrayLength; slice++)
        {
            [blit copyFromTexture:read256
                      sourceSlice:slice
                      sourceLevel:0
                     sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(read256.width, read256.height, 1)
                        toTexture:write256
                 destinationSlice:slice
                 destinationLevel:0
                destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        [blit endEncoding];
    }

    id<MTLComputeCommandEncoder> encoder =
        [command computeCommandEncoder];
    if (!encoder)
        return false;

    MetalCaptureDispatchConfigCpu dispatchConfig = {};
    dispatchConfig.Scale =
        static_cast<uint32_t>(std::max(1, CaptureState->Scale));
    dispatchConfig.LineOffset = static_cast<uint32_t>(startLine);

    [encoder setComputePipelineState:CaptureState->CapturePipeline];
    [encoder setBuffer:CaptureState->LineConfigBuffer offset:0 atIndex:0];
    [encoder setBytes:&dispatchConfig
               length:sizeof(dispatchConfig)
              atIndex:1];
    [encoder setBuffer:CaptureState->AuxVramBuffer offset:0 atIndex:2];
    [encoder setBuffer:CaptureState->AuxFifoBuffer offset:0 atIndex:3];
    [encoder setTexture:engineA2D atIndex:0];
    [encoder setTexture:high3D atIndex:1];
    [encoder setTexture:read128 atIndex:2];
    [encoder setTexture:read256 atIndex:3];
    [encoder setTexture:write128 atIndex:4];
    [encoder setTexture:write256 atIndex:5];

    const NSUInteger threadWidth =
        std::max<NSUInteger>(1, CaptureState->CapturePipeline.threadExecutionWidth);
    const NSUInteger threadHeight = std::max<NSUInteger>(
        1,
        CaptureState->CapturePipeline.maxTotalThreadsPerThreadgroup /
            threadWidth);
    [encoder dispatchThreads:MTLSizeMake(
                256,
                static_cast<NSUInteger>(endLine - startLine),
                1)
           threadsPerThreadgroup:MTLSizeMake(threadWidth, threadHeight, 1)];
    [encoder endEncoding];

    // MELONPRIME_METAL_CAPTURE_WRITE_TICKET_V1
    std::vector<MetalCaptureState::CaptureWriteTicket> pendingWrites;
    const uint64_t frameSerial = CaptureState->FrameSerial;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        std::array<int, 16> pendingIndex;
        pendingIndex.fill(-1);
        for (int line = startLine; line < endLine; line++)
        {
            const auto& config = lines[line];
            if (!config.Enabled)
                continue;
            const int bank = static_cast<int>((config.CaptureCnt >> 16) & 3u);
            const int start = static_cast<int>((config.CaptureCnt >> 18) & 3u);
            const int size = static_cast<int>((config.CaptureCnt >> 20) & 3u);
            const int layer = (bank << 2) | start;
            auto& meta = CaptureState->Meta[layer];
            const int existing = pendingIndex[layer];
            if (existing >= 0)
            {
                pendingWrites[static_cast<size_t>(existing)].Size = size;
                pendingWrites[static_cast<size_t>(existing)].DirtySerial =
                    meta.DirtySerial;
                meta.GpuSubmittedSerial = meta.DirtySerial;
            }
            else
            {
                const uint64_t token = CaptureState->NextGpuWriteToken++;
                const uint64_t generation = meta.Generation + 1;
                meta.LatestGpuSubmittedToken = token;
                meta.LatestGpuSubmittedGeneration = generation;
                meta.GpuWritesInFlight++;
                meta.GpuSubmittedSerial = meta.DirtySerial;
                pendingIndex[layer] = static_cast<int>(pendingWrites.size());
                MetalCaptureState::CaptureWriteTicket ticket;
                ticket.FrameSerial = frameSerial;
                ticket.SegmentIndex = segmentIndex;
                ticket.Layer = static_cast<uint32_t>(layer);
                ticket.DirtySerial = meta.DirtySerial;
                ticket.Generation = generation;
                ticket.Token = token;
                ticket.Size = size;
                pendingWrites.push_back(ticket);
            }
        }
    }

    std::shared_ptr<MetalCaptureState> capturedState = CaptureState;
    const uint8_t publishFace = writeFace;
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        const bool ok = completed.status == MTLCommandBufferStatusCompleted;
        if (!ok)
        {
            const char* message = completed.error
                ? [[completed.error localizedDescription] UTF8String]
                : "unknown Metal error";
            std::fprintf(stderr,
                "[MelonPrime] metal display capture: GPU capture command "
                "failed: %s\n", message);
        }
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (const auto& ticket : pendingWrites)
        {
            auto& meta = capturedState->Meta[ticket.Layer];
            if (meta.GpuWritesInFlight > 0)
                meta.GpuWritesInFlight--;
            if (ticket.Token != meta.LatestGpuSubmittedToken)
                continue;
            if (ticket.Generation != meta.LatestGpuSubmittedGeneration)
                continue;
            if (!ok)
            {
                meta.Valid = false;
                meta.Dirty = true;
                continue;
            }
            if (meta.CpuUploadInFlight)
            {
                meta.Valid = false;
                meta.Dirty = true;
                continue;
            }
            if (meta.DirtySerial == ticket.DirtySerial)
            {
                meta.Size = ticket.Size;
                meta.Valid = true;
                meta.Dirty = false;
                meta.Generation = ticket.Generation;
            }
            else
            {
                meta.Valid = false;
                meta.Dirty = true;
            }
        }
    }];
    [command commit];

    // Publish write face for subsequent segments on the same queue.
    CaptureState->PublishedIndex = publishFace;
    CaptureState->NextSegmentIndex = segmentIndex + 1;
    Metal3DSetCaptureTextures(
        Rend3D.get(),
        GetMetalCapture128Texture(),
        GetMetalCapture256Texture());

    if (!CaptureState->LoggedFirstCapture)
    {
        CaptureState->LoggedFirstCapture = true;
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: first GPU capture submitted "
            "scale=%d readback=0 ping-pong=1\n",
            CaptureState->Scale);
    }
    return true;
}

void MetalRenderer::AllocCapture(u32 bank, u32 start, u32 len)
{
    if (!CaptureState || !CaptureState->Ready ||
        bank >= 4 || start >= 4)
    {
        return;
    }

    const int layer = static_cast<int>((bank << 2) | start);
    std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
    auto& meta = CaptureState->Meta[layer];
    if (meta.Size != static_cast<int>(len))
        meta.Valid = false;
    meta.Size = static_cast<int>(len);
    meta.Dirty = true;
    meta.DirtySerial++;
    meta.Generation++;
}

bool MetalRenderer::UploadCaptureTexture(
    std::shared_ptr<MetalCaptureState> capturedState,
    void* textureRaw,
    unsigned long slice,
    const uint16_t* source,
    unsigned long nativeWidth,
    unsigned long nativeHeight,
    std::vector<int> affectedLayers)
{
    // capturedState and affectedLayers are genuine by-value function
    // parameters (not accessed through a C++ lambda's capture-by-reference),
    // and layerSerials below is a genuine local of this function -- so the
    // Objective-C completion-handler block nested in this function body
    // captures them the same well-supported way every other completion
    // handler in this codebase does. An earlier version of this routine
    // built the block inside a capturing C++ lambda (itself a local of
    // UploadCpuCompletedCaptures()); that nesting produced a reproducible
    // EXC_BAD_ACCESS in the async completion callback under real gameplay.
    id<MTLTexture> texture = (__bridge id<MTLTexture>)textureRaw;
    // MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1: upload native RGB5551
    // directly into R16Uint canonical textures (no scale² RGBA8 expand).
    const NSUInteger width = static_cast<NSUInteger>(nativeWidth);
    const NSUInteger height = static_cast<NSUInteger>(nativeHeight);
    const NSUInteger requiredBytes = width * height * sizeof(uint16_t);
    std::vector<uint16_t>& pixels = capturedState->CpuUploadScratch;
    if (pixels.size() < width * height)
        pixels.resize(width * height);
    std::memcpy(pixels.data(), source, requiredBytes);

    id<MTLBuffer> upload = nil;
    std::shared_ptr<std::atomic<bool>> stagingSlotBusy;
    if (auto* slot = static_cast<MetalCaptureState::StagingBuffer*>(
            AcquireStagingSlot(*capturedState, static_cast<unsigned long>(requiredBytes))))
    {
        std::memcpy([slot->Buffer contents], pixels.data(), requiredBytes);
        upload = slot->Buffer;
        stagingSlotBusy = slot->Busy;
        stagingSlotBusy->store(true, std::memory_order_release);
    }
    else
    {
        upload = [capturedState->Device
            newBufferWithBytes:pixels.data()
                        length:requiredBytes
                       options:MTLResourceStorageModeShared];
        if (!capturedState->LoggedStagingRingExhausted)
        {
            capturedState->LoggedStagingRingExhausted = true;
            std::fprintf(stderr,
                "[MelonPrime] metal display capture: staging buffer ring "
                "unavailable (busy or above kMaxCaptureStagingBytes=%zu), "
                "falling back to a one-off buffer allocation "
                "(further occurrences not logged)\n",
                static_cast<size_t>(MetalCaptureState::kMaxCaptureStagingBytes));
        }
    }
    id<MTLCommandBuffer> command =
        [capturedState->Queue commandBuffer];
    id<MTLBlitCommandEncoder> blit =
        command ? [command blitCommandEncoder] : nil;
    if (!upload || !command || !blit)
    {
        if (stagingSlotBusy)
            stagingSlotBusy->store(false, std::memory_order_release);
        // The caller already set CpuUploadInFlight=true for these layers before
        // calling this function (so a concurrent dispatch could not double-
        // submit them); since no command was committed, no completion
        // handler will ever run to clear it. Without this, a command/blit
        // allocation failure here would permanently strand these layers with
        // CpuUploadInFlight stuck true, and UploadCpuCompletedCaptures() would
        // never retry them again.
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (int layer : affectedLayers)
            capturedState->Meta[layer].CpuUploadInFlight = false;
        return false;
    }

    const NSUInteger bytesPerRow = width * sizeof(uint16_t);
    [blit copyFromBuffer:upload
            sourceOffset:0
       sourceBytesPerRow:bytesPerRow
     sourceBytesPerImage:bytesPerRow * height
              sourceSize:MTLSizeMake(width, height, 1)
               toTexture:texture
        destinationSlice:static_cast<NSUInteger>(slice)
        destinationLevel:0
       destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];

    // This layer's CpuSubmittedSerial was snapshotted by the caller (under
    // MetaMutex, before CpuUploadInFlight was set) before dispatch, so the
    // handler can tell a merely-superseded write (some other write will
    // finalize this layer) apart from data that changed again while this
    // particular write was in flight (must not be marked valid).
    std::vector<std::pair<int, uint64_t>> layerSerials;
    layerSerials.reserve(affectedLayers.size());
    {
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (int layer : affectedLayers)
            layerSerials.emplace_back(layer, capturedState->Meta[layer].CpuSubmittedSerial);
    }

    // Same-queue command ordering (capturedState->Queue is the
    // renderer-side queue shared by 3D, 2D, capture and final
    // composition -- NOT the presenter's queue, which is separate and
    // synchronized only through RendererOutputLease) guarantees any
    // later command buffer on this queue that reads this texture sees
    // this write's effects, without a CPU-side wait for the common
    // (success) case. This runs on every CPU-fallback frame with a
    // pending capture write, so a synchronous GPU stall here was a real,
    // measured hot-path cost. What we do need to wait for is knowing
    // whether the write actually landed, so metadata reflects reality.
    [command addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        // The blit above only reads `upload` while the GPU executes this
        // command buffer, so completion (success or failure) is exactly
        // when it's safe to hand the staging slot back to the ring.
        if (stagingSlotBusy)
            stagingSlotBusy->store(false, std::memory_order_release);
        const bool ok = completed.status == MTLCommandBufferStatusCompleted;
        if (!ok)
        {
            const char* message = completed.error
                ? [[completed.error localizedDescription] UTF8String]
                : "unknown Metal error";
            std::fprintf(stderr,
                "[MelonPrime] metal display capture: CPU fallback upload "
                "command failed: %s\n", message);
        }
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (const auto& entry : layerSerials)
        {
            auto& meta = capturedState->Meta[entry.first];
            meta.CpuUploadInFlight = false;
            if (!ok)
            {
                meta.Valid = false;
                meta.Dirty = true;
                continue;
            }
            // A GPU capture write is still in flight for this layer -- that
            // write owns the texture now.
            if (meta.GpuWritesInFlight > 0)
            {
                meta.Valid = false;
                meta.Dirty = true;
                continue;
            }
            if (meta.DirtySerial == entry.second)
            {
                meta.Valid = true;
                meta.Dirty = false;
            }
            else
            {
                // Data changed again (AllocCapture) after this upload was
                // dispatched -- stale result, leave Dirty for the next write.
                meta.Valid = false;
                meta.Dirty = true;
            }
        }
    }];
    [command commit];
    return true;
}

bool MetalRenderer::UploadCpuCompletedCaptures()
{
    if (!CaptureState || !CaptureState->Ready)
        return true;

    // MELONPRIME_METAL_CAPTURE_UPLOAD_INFLIGHT_GATING_V1: only a layer that
    // is Dirty *and not already CpuUploadInFlight or GpuWritesInFlight>0* is
    // eligible for a new CPU dispatch this frame. Without the in-flight
    // check, a GPU that takes longer than one frame to complete a write
    // caused a new upload to be (re)dispatched every single frame for the
    // same still-pending data, permanently superseding the previous
    // in-flight write's completion before it could ever land -- Dirty/Valid
    // then never settled, and the renderer could never consider capture
    // resources coherent again. Skipping while GpuWritesInFlight>0 also
    // prevents a CPU blit from racing a live GPU Display Capture write to
    // the same texture slice.
    uint32_t upload128Mask = 0;
    uint32_t upload256Mask = 0;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        for (int layer = 0; layer < 16; layer++)
        {
            const auto& meta = CaptureState->Meta[layer];
            if (!meta.Dirty || meta.CpuUploadInFlight ||
                meta.GpuWritesInFlight > 0 || meta.Size < 0)
                continue;
            if (meta.Size == 0)
                upload128Mask |= 1u << layer;
            else
                upload256Mask |= 1u << (layer >> 2);
        }
    }

    if (!upload128Mask && !upload256Mask)
        return true;

    // Kept alive by the completion handlers below (captured by value), so a
    // ConfigureMetalCaptureState() reconfigure racing with an in-flight
    // upload can never leave a handler writing through a dangling
    // MetalCaptureState. See MetalCaptureState::CaptureMeta::CpuSubmittedSerial
    // for how a handler tells whether it is still the authoritative result
    // for its layer once it does run.
    std::shared_ptr<MetalCaptureState> capturedState = CaptureState;

    for (uint32_t layer = 0; layer < 16; layer++)
    {
        if ((upload128Mask & (1u << layer)) == 0u)
            continue;
        const uint32_t bank = layer >> 2u;
        const uint32_t start = layer & 3u;
        const uint16_t* source =
            reinterpret_cast<const uint16_t*>(GPU.VRAM[bank]) +
            start * 64u * 256u;
        {
            std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
            auto& meta = CaptureState->Meta[layer];
            meta.CpuUploadInFlight = true;
            meta.CpuSubmittedSerial = meta.DirtySerial;
        }
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture128[0],
                layer,
                source,
                128u,
                128u,
                std::vector<int>{static_cast<int>(layer)}))
        {
            return false;
        }
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture128[1],
                layer,
                source,
                128u,
                128u,
                std::vector<int>{}))
        {
            return false;
        }
    }

    for (uint32_t bank = 0; bank < 4; bank++)
    {
        if ((upload256Mask & (1u << bank)) == 0u)
            continue;
        const uint16_t* source =
            reinterpret_cast<const uint16_t*>(GPU.VRAM[bank]);
        std::vector<int> affectedLayers;
        {
            std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
            for (uint32_t start = 0; start < 4; start++)
            {
                const int layer = static_cast<int>((bank << 2u) | start);
                auto& meta = CaptureState->Meta[layer];
                if (meta.Dirty && !meta.CpuUploadInFlight &&
                    meta.GpuWritesInFlight == 0 && meta.Size > 0)
                {
                    meta.CpuUploadInFlight = true;
                    meta.CpuSubmittedSerial = meta.DirtySerial;
                    affectedLayers.push_back(layer);
                }
            }
        }
        if (affectedLayers.empty())
            continue;
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture256[0],
                bank,
                source,
                256u,
                256u,
                std::move(affectedLayers)))
        {
            return false;
        }
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture256[1],
                bank,
                source,
                256u,
                256u,
                std::vector<int>{}))
        {
            return false;
        }
    }

    if (!CaptureState->LoggedCpuFallbackUpload)
    {
        CaptureState->LoggedCpuFallbackUpload = true;
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: CPU fallback capture upload "
            "submitted to GPU texture arrays (further occurrences not "
            "logged)\n");
    }
    return true;
}

void MetalRenderer::SyncVRAMCapture(
    u32 bank,
    u32 start,
    u32 len,
    bool complete)
{
    if (!CaptureState || !CaptureState->Ready ||
        bank >= 4 || start >= 4)
    {
        return;
    }

    const int metadataLayer = static_cast<int>((bank << 2) | start);
    MetalCaptureState::CaptureMeta meta;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        meta = CaptureState->Meta[metadataLayer];
    }
    if (!meta.Valid || meta.Size != static_cast<int>(len))
        return;

    if (!complete && !CaptureState->LoggedIncompleteCaptureSync)
    {
        // SyncVRAMCapture() runs on every CPU VRAM access that touches a
        // capture-tracked bank -- this can fire thousands of times per frame
        // in games that read/write capture-backed VRAM in a loop, so it must
        // stay rate-limited (see LoggedOnDemandReadback just below for the
        // established pattern this follows).
        CaptureState->LoggedIncompleteCaptureSync = true;
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: CPU requested incomplete "
            "capture bank=%u start=%u size=%u (further occurrences not logged)\n",
            bank, start, len);
    }

    // MELONPRIME_METAL_CAPTURE_NATIVE_R16UINT_V1: direct blit from canonical
    // R16Uint texture into a native RGB5551 readback buffer (no decode kernel).
    const bool use128 = len == 0;
    const NSUInteger width = use128 ? 128u : 256u;
    const NSUInteger height = use128 ? 128u : 256u;
    const NSUInteger slice = use128
        ? static_cast<NSUInteger>(metadataLayer)
        : static_cast<NSUInteger>(bank);
    id<MTLTexture> source = use128
        ? CaptureState->Capture128[CaptureState->PublishedIndex]
        : CaptureState->Capture256[CaptureState->PublishedIndex];

    id<MTLCommandBuffer> command = [CaptureState->Queue commandBuffer];
    id<MTLBlitCommandEncoder> blit =
        command ? [command blitCommandEncoder] : nil;
    if (!command || !blit || !source)
        return;

    const NSUInteger bytesPerRow = width * sizeof(uint16_t);
    [blit copyFromTexture:source
              sourceSlice:slice
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(width, height, 1)
                 toBuffer:CaptureState->ReadbackBuffer
        destinationOffset:0
      destinationBytesPerRow:bytesPerRow
    destinationBytesPerImage:bytesPerRow * height];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted)
        return;

    const uint16_t* pixels = static_cast<const uint16_t*>(
        [CaptureState->ReadbackBuffer contents]);
    uint16_t* vram = reinterpret_cast<uint16_t*>(GPU.VRAM[bank]);
    if (use128)
    {
        std::memcpy(
            vram + start * 64u * 256u,
            pixels,
            128u * 128u * sizeof(uint16_t));
        for (u32 row = start * 64u; row < (start + 1u) * 64u; row++)
            GPU.VRAMDirty[bank][row] = true;
    }
    else
    {
        u32 block = start;
        for (u32 copied = 0; copied < len; copied++)
        {
            std::memcpy(
                vram + block * 64u * 256u,
                pixels + block * 64u * 256u,
                64u * 256u * sizeof(uint16_t));
            for (u32 row = block * 64u; row < (block + 1u) * 64u; row++)
                GPU.VRAMDirty[bank][row] = true;
            block = (block + 1u) & 3u;
        }
    }

    if (!CaptureState->LoggedOnDemandReadback)
    {
        CaptureState->LoggedOnDemandReadback = true;
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: on-demand CPU readback "
            "bank=%u start=%u size=%u\n",
            bank, start, len);
    }

    // MELONPRIME_METAL_READBACK_EXPLICIT_V1 (PR-6): ARM CPU VRAM reads are
    // an allowed explicit reason; count bytes separately from normal-path.
    const uint64_t bytes = (len == 0)
        ? (128ull * 128ull * sizeof(uint16_t))
        : (static_cast<uint64_t>(len) * 64ull * 256ull * sizeof(uint16_t));
    MetalRecordExplicitReadbackBytes(MetalReadbackReason::CpuVramRead, bytes);
}

bool MetalRenderer::MetalCaptureReady() const
{
    return CaptureState && CaptureState->Ready;
}

bool MetalRenderer::MetalCaptureFrameHadCapture() const
{
    return CaptureState && CaptureState->FrameHadCapture;
}

bool MetalRenderer::MetalCaptureFrameSupported() const
{
    return !CaptureState || CaptureState->FrameSupported;
}

bool MetalRenderer::MetalCaptureResourcesCoherent() const
{
    if (!CaptureState || !CaptureState->Ready)
        return false;

    // Single locked snapshot rather than a lock per validLayer() call below:
    // this function calls validLayer() dozens of times per invocation, and a
    // per-layer lock/unlock would let a completion handler flip Valid on one
    // layer mid-scan while another layer in the same scan is still being
    // read, mixing two different points in time into one coherency verdict.
    std::array<MetalCaptureState::CaptureMeta, 16> metaSnapshot;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        metaSnapshot = CaptureState->Meta;
    }

    auto validLayer = [&](int layer) {
        return layer < 0 ||
            (layer < 16 && metaSnapshot[layer].Valid);
    };

    for (u32 bank = 0; bank < 4; bank++)
    {
        for (u32 block = 0; block < 4; block++)
        {
            if (!validLayer(GPU.GetCaptureBlock_LCDC(
                    (bank << 17u) | (block << 15u))))
                return false;
        }
    }

    int info32[32] = {};
    GPU.GPU2D_A.GetCaptureInfo_BG(info32);
    for (int layer : info32)
        if (!validLayer(layer))
            return false;

    int info16[16] = {};
    GPU.GPU2D_A.GetCaptureInfo_OBJ(info16);
    for (int layer : info16)
        if (!validLayer(layer))
            return false;

    int infoB[16] = {};
    GPU.GPU2D_B.GetCaptureInfo_BG(infoB);
    for (int i = 0; i < 8; i++)
        if (!validLayer(infoB[i]))
            return false;
    for (int& entry : infoB)
        entry = -1;
    GPU.GPU2D_B.GetCaptureInfo_OBJ(infoB);
    for (int i = 0; i < 8; i++)
        if (!validLayer(infoB[i]))
            return false;

    return true;
}

void* MetalRenderer::GetMetalCapture128Texture() const
{
    return CaptureState && CaptureState->Capture128[CaptureState->PublishedIndex]
        ? (__bridge void*)CaptureState->Capture128[CaptureState->PublishedIndex]
        : nullptr;
}

void* MetalRenderer::GetMetalCapture256Texture() const
{
    return CaptureState && CaptureState->Capture256[CaptureState->PublishedIndex]
        ? (__bridge void*)CaptureState->Capture256[CaptureState->PublishedIndex]
        : nullptr;
}
