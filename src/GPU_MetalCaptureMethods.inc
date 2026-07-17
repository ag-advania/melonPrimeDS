// Included after GPU_MetalFullGpuMethods.inc.
// MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1

namespace
{
static constexpr const char* kMetalDisplayCaptureShaderSource = R"MSL(
#include <metal_stdlib>
using namespace metal;

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
    uint3 pad0;
};

struct CaptureReadbackConfig
{
    uint scale;
    uint width;
    uint height;
    uint layer;
    uint use128;
    uint3 pad0;
};

static inline float4 mp_unpack_rgba8(uint packed)
{
    return float4(
        float(packed & 0xFFu),
        float((packed >> 8u) & 0xFFu),
        float((packed >> 16u) & 0xFFu),
        float((packed >> 24u) & 0xFFu)) / 255.0;
}

static inline float4 mp_capture_source_b(
    constant CaptureLineConfig& lineConfig,
    uint nativeX,
    uint subX,
    uint subY,
    uint scale,
    device const uint* auxVram,
    device const uint* auxFifo,
    texture2d_array<float, access::read> snapshot128,
    texture2d_array<float, access::read> snapshot256)
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

        uint2 coord = uint2(
            capX * scale + subX,
            capY * scale + subY);
        coord = min(coord, uint2(
            max(snapshot128.get_width(), 1u) - 1u,
            max(snapshot128.get_height(), 1u) - 1u));
        return snapshot128.read(coord, layer);
    }

    uint2 coord = uint2(
        nativeX * scale + subX,
        lineConfig.srcBNativeY * scale + subY);
    coord = min(coord, uint2(
        max(snapshot256.get_width(), 1u) - 1u,
        max(snapshot256.get_height(), 1u) - 1u));
    return snapshot256.read(coord, layer >> 2u);
}

kernel void mp_metal_display_capture(
    constant CaptureLineConfig* lines [[buffer(0)]],
    constant CaptureDispatchConfig& dispatchConfig [[buffer(1)]],
    device const uint* auxVram [[buffer(2)]],
    device const uint* auxFifo [[buffer(3)]],
    texture2d<float, access::read> engineA2D [[texture(0)]],
    texture2d<float, access::read> engineA3D [[texture(1)]],
    texture2d_array<float, access::read> snapshot128 [[texture(2)]],
    texture2d_array<float, access::read> snapshot256 [[texture(3)]],
    texture2d_array<float, access::write> capture128 [[texture(4)]],
    texture2d_array<float, access::write> capture256 [[texture(5)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint scale = max(dispatchConfig.scale, 1u);
    uint nativeX = gid.x / scale;
    uint nativeLine = gid.y / scale;
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

    uint subX = gid.x - nativeX * scale;
    uint subY = gid.y - nativeLine * scale;

    uint srcA = (config.captureCnt >> 24u) & 1u;
    float4 colorA = float4(0.0);
    if (srcA == 0u)
    {
        uint2 coord = min(gid, uint2(
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
                uint(sourceX) * scale + subX,
                nativeLine * scale + subY);
            coord = min(coord, uint2(
                max(engineA3D.get_width(), 1u) - 1u,
                max(engineA3D.get_height(), 1u) - 1u));
            colorA = engineA3D.read(coord);
        }
    }

    float4 colorB = mp_capture_source_b(
        config, nativeX, subX, subY, scale,
        auxVram, auxFifo, snapshot128, snapshot256);

    uint dstMode = (config.captureCnt >> 29u) & 3u;
    float4 output;
    if (dstMode == 0u)
    {
        output = colorA;
    }
    else if (dstMode == 1u)
    {
        output = colorB;
    }
    else
    {
        uint eva = min(config.captureCnt & 0x1Fu, 16u);
        uint evb = min((config.captureCnt >> 8u) & 0x1Fu, 16u);
        uint4 a8 = uint4(clamp(colorA, 0.0, 1.0) * 255.0);
        uint4 b8 = uint4(clamp(colorB, 0.0, 1.0) * 255.0);
        uint3 a5 = a8.rgb >> 3u;
        uint3 b5 = b8.rgb >> 3u;
        uint alphaA = a8.a > 0u ? 1u : 0u;
        uint alphaB = b8.a > 0u ? 1u : 0u;
        uint3 out5 = min(
            ((a5 * alphaA * eva) + (b5 * alphaB * evb) + 8u) >> 4u,
            uint3(31u));
        uint outAlpha =
            ((eva > 0u) ? alphaA : 0u) |
            ((evb > 0u) ? alphaB : 0u);
        output = float4(float3(out5 << 3u) / 255.0, float(outAlpha));
    }

    uint dstBank = (config.captureCnt >> 16u) & 3u;
    uint dstOffset = (config.captureCnt >> 18u) & 3u;
    if (captureSize == 0u)
    {
        uint layer = (dstBank << 2u) | dstOffset;
        capture128.write(output, gid, layer);
    }
    else
    {
        uint dstNativeY = (dstOffset * 64u + nativeLine) & 0xFFu;
        uint2 dstCoord = uint2(
            nativeX * scale + subX,
            dstNativeY * scale + subY);
        capture256.write(output, dstCoord, dstBank);
    }
}

kernel void mp_metal_capture_readback(
    constant CaptureReadbackConfig& config [[buffer(0)]],
    device ushort* output [[buffer(1)]],
    texture2d_array<float, access::read> capture128 [[texture(0)]],
    texture2d_array<float, access::read> capture256 [[texture(1)]],
    uint2 gid [[thread_position_in_grid]])
{
    if (gid.x >= config.width || gid.y >= config.height)
        return;

    uint scale = max(config.scale, 1u);
    uint2 sampleCoord = uint2(
        gid.x * scale + scale / 2u,
        gid.y * scale + scale / 2u);

    float4 color;
    if (config.use128 != 0u)
    {
        sampleCoord = min(sampleCoord, uint2(
            max(capture128.get_width(), 1u) - 1u,
            max(capture128.get_height(), 1u) - 1u));
        color = capture128.read(sampleCoord, config.layer);
    }
    else
    {
        sampleCoord = min(sampleCoord, uint2(
            max(capture256.get_width(), 1u) - 1u,
            max(capture256.get_height(), 1u) - 1u));
        color = capture256.read(sampleCoord, config.layer);
    }

    uint4 rgba = uint4(clamp(color, 0.0, 1.0) * 255.0);
    uint r = min((rgba.r * 31u + 127u) / 255u, 31u);
    uint g = min((rgba.g * 31u + 127u) / 255u, 31u);
    uint b = min((rgba.b * 31u + 127u) / 255u, 31u);
    uint a = rgba.a > 0u ? 0x8000u : 0u;
    output[gid.y * config.width + gid.x] =
        ushort(r | (g << 5u) | (b << 10u) | a);
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
    uint32_t Pad[3] = {};
};
static_assert(sizeof(MetalCaptureDispatchConfigCpu) == 16);

struct MetalCaptureReadbackConfigCpu
{
    uint32_t Scale = 1;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t Layer = 0;
    uint32_t Use128 = 0;
    uint32_t Pad[3] = {};
};
static_assert(sizeof(MetalCaptureReadbackConfigCpu) == 32);

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
    // must go through MetaMutex. Dirty/UploadInFlight/DirtySerial/
    // SubmittedSerial replace the old single PendingCpuUpload+
    // PendingUploadSerial pair so that (a) a write already in flight is never
    // resubmitted every frame just because it has not completed yet within
    // one frame (the old design's livelock under GPU backpressure), and
    // (b) a completion can tell a merely-superseded write (fine, some other
    // write will finalize this layer) apart from data that changed again
    // after this particular write was dispatched (must NOT be marked valid).
    struct CaptureMeta
    {
        int Size = -1;
        bool Valid = false;
        // This layer's destination data (VRAM contents or capture
        // configuration) has changed since the last write that completed
        // successfully, so it needs a fresh write (GPU capture or CPU
        // fallback) before Valid can be trusted again.
        bool Dirty = false;
        // A write for this layer has been dispatched and its completion
        // has not yet run. While true, a new write for the *same* DirtySerial
        // must not be dispatched again -- that is what produced the
        // resubmit-every-frame livelock under sustained GPU backpressure.
        bool UploadInFlight = false;
        // Bumped whenever the underlying data actually changes (AllocCapture,
        // a capture-destination reconfigure). A write dispatch snapshots the
        // current value into SubmittedSerial; a completion only trusts its
        // own result if DirtySerial still equals the SubmittedSerial it
        // snapshotted -- otherwise something changed the data again while
        // the write was in flight and the result must not be applied.
        uint64_t DirtySerial = 0;
        uint64_t SubmittedSerial = 0;
    };

    // Guards every field of every Meta[] entry. Never held across GPU command
    // encoding/submission -- only around the metadata reads/writes
    // themselves, both on the emulation thread and inside completion
    // handlers.
    std::mutex MetaMutex;

    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLComputePipelineState> CapturePipeline = nil;
    id<MTLComputePipelineState> ReadbackPipeline = nil;
    id<MTLTexture> Capture128 = nil;
    id<MTLTexture> Capture256 = nil;
    id<MTLTexture> Snapshot128 = nil;
    id<MTLTexture> Snapshot256 = nil;
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
    // MELONPRIME_METAL_CAPTURE_CPU_UPLOAD_SCRATCH_V1: reused across
    // UploadCpuCompletedCaptures() calls instead of a fresh heap allocation
    // per layer per frame (this hot path fires on every CPU-fallback frame
    // that has a pending capture write).
    std::vector<uint32_t> CpuUploadScratch;

    // MELONPRIME_METAL_CAPTURE_STAGING_RING_V1: a small ring of reusable
    // MTLBuffers so the common case needs no per-upload
    // newBufferWithBytes: call. AcquireStagingSlot() falls back to a one-off
    // allocation if every slot is genuinely busy, so correctness never
    // depends on this size being exactly right. Busy is a
    // shared_ptr<atomic<bool>> (not a plain bool on the slot) so a
    // completion handler can flip it after this MetalCaptureState's ring
    // itself may have been reconfigured -- see UploadCaptureTexture().
    //
    // Each slot holds a scale-expanded RGBA8 buffer (up to 256*scale square,
    // 4 bytes/pixel), so ring size trades memory against one-off-allocation
    // frequency at high internal-resolution scale factors: at 16x that is 64
    // MiB/slot, and this ring plus the (also scale-expanded) Capture/Snapshot
    // texture arrays can otherwise reach roughly 1.5 GiB combined on an 8/16x
    // build -- a real risk on 8 GB integrated-GPU Macs. Kept small (a
    // realistic frame has only a handful of layers needing CPU-fallback
    // upload at once, not all 16+4) rather than sized for the theoretical
    // worst case of every layer needing upload the same frame.
    struct StagingBuffer
    {
        id<MTLBuffer> Buffer = nil;
        NSUInteger Capacity = 0;
        std::shared_ptr<std::atomic<bool>> Busy =
            std::make_shared<std::atomic<bool>>(false);
    };
    static constexpr int kStagingRingSize = 4;
    // Persistent ring slots refuse to grow past this. At 16x a single 256x256
    // capture is 64 MiB; keeping that out of the reusable ring bounds the
    // worst-case resident footprint of the ring itself to 4 * 16 MiB = 64 MiB
    // (vs 4 * 64 MiB = 256 MiB, or the earlier 8-slot * 64 MiB = 512 MiB).
    // Oversized uploads still succeed via a one-off buffer for that frame.
    static constexpr NSUInteger kMaxCaptureStagingBytes = 16u * 1024u * 1024u;
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
    // Cap persistent ring growth so an 8x/16x session cannot pin hundreds of
    // MiB in reusable slots. Oversized requests return nullptr and the caller
    // falls back to a one-off allocation for that upload only -- the ring
    // itself stays bounded to common scales (256@8x = 8 MiB fits; 256@16x =
    // 64 MiB does not and never permanently occupies a ring slot).
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
    if (!OutputState || !OutputState->Device || !OutputState->Queue)
        return false;

    id<MTLDevice> device = (__bridge id<MTLDevice>)preferredDevice;
    if (!device)
        device = OutputState->Device;
    id<MTLCommandQueue> queue = OutputState->Queue;
    if (!device || !queue || queue.device != device)
        return false;

    const int scale = std::max(1, ScaleFactor);
    if (CaptureState && CaptureState->Ready &&
        CaptureState->Device == device &&
        CaptureState->Queue == queue &&
        CaptureState->Scale == scale)
    {
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
            meta.UploadInFlight = false;
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
    id<MTLFunction> readbackFunction =
        [next->Library newFunctionWithName:@"mp_metal_capture_readback"];
    if (!captureFunction || !readbackFunction)
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

    next->ReadbackPipeline =
        [device newComputePipelineStateWithFunction:readbackFunction error:&error];
    if (!next->ReadbackPipeline)
    {
        const char* message =
            error ? [[error localizedDescription] UTF8String] : "unknown error";
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: readback pipeline failed: %s\n",
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
                texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                             width:width
                                            height:height
                                         mipmapped:NO];
        desc.textureType = MTLTextureType2DArray;
        desc.arrayLength = layers;
        desc.usage = usage;
        desc.storageMode = MTLStorageModePrivate;
        return [device newTextureWithDescriptor:desc];
    };

    const NSUInteger scaleU = static_cast<NSUInteger>(scale);
    next->Capture128 = makeArrayTexture(
        128u * scaleU, 128u * scaleU, 16u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->Capture256 = makeArrayTexture(
        256u * scaleU, 256u * scaleU, 4u,
        MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite);
    next->Snapshot128 = makeArrayTexture(
        128u * scaleU, 128u * scaleU, 16u,
        MTLTextureUsageShaderRead);
    next->Snapshot256 = makeArrayTexture(
        256u * scaleU, 256u * scaleU, 4u,
        MTLTextureUsageShaderRead);

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

    if (!next->Capture128 || !next->Capture256 ||
        !next->Snapshot128 || !next->Snapshot256 ||
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
        "[MelonPrime] metal display capture: configured scale=%d "
        "capture128=%zux%zux16 capture256=%zux%zux4\n",
        scale,
        static_cast<size_t>(next->Capture128.width),
        static_cast<size_t>(next->Capture128.height),
        static_cast<size_t>(next->Capture256.width),
        static_cast<size_t>(next->Capture256.height));

    CaptureState = std::move(next);

    // MELONPRIME_METAL_COMPUTE_CAPTURE_TEXTURE_WIRE_V1: hand the (re)created
    // capture texture arrays to the compute 3D renderer. Its capture-backed
    // texture sampler (GPU3D_MetalComputeTexturedMethods.inc) otherwise stays
    // on the 16x16/4x4 dummy textures it was initialized with, so any 3D
    // polygon textured from display-capture VRAM would sample the dummy. The
    // setter itself validates device/type and is a no-op for the raster
    // renderer. Must re-run after every reconfigure -- the textures are
    // recreated on scale changes.
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
    void* high3DTexture)
{
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
    uint32_t snapshot128Mask = 0;
    uint32_t snapshot256Mask = 0;
    for (int line = 0; line < 192; line++)
    {
        const auto& config = lines[line];
        if (!config.Enabled || config.SrcBCaptureLayer < 0)
            continue;
        if (config.SrcBCaptureSize == 0)
            snapshot128Mask |= 1u << config.SrcBCaptureLayer;
        else
            snapshot256Mask |= 1u << (config.SrcBCaptureLayer >> 2);
    }

    id<MTLCommandBuffer> command = [CaptureState->Queue commandBuffer];
    if (!command)
        return false;
    command.label = @"MelonPrime Metal GPU Display Capture";

    if (snapshot128Mask || snapshot256Mask)
    {
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit)
            return false;

        for (uint32_t layer = 0; layer < 16; layer++)
        {
            if ((snapshot128Mask & (1u << layer)) == 0u)
                continue;
            [blit copyFromTexture:CaptureState->Capture128
                     sourceSlice:layer
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(
                          CaptureState->Capture128.width,
                          CaptureState->Capture128.height,
                          1)
                       toTexture:CaptureState->Snapshot128
                destinationSlice:layer
                destinationLevel:0
               destinationOrigin:MTLOriginMake(0, 0, 0)];
        }
        for (uint32_t layer = 0; layer < 4; layer++)
        {
            if ((snapshot256Mask & (1u << layer)) == 0u)
                continue;
            [blit copyFromTexture:CaptureState->Capture256
                     sourceSlice:layer
                     sourceLevel:0
                    sourceOrigin:MTLOriginMake(0, 0, 0)
                      sourceSize:MTLSizeMake(
                          CaptureState->Capture256.width,
                          CaptureState->Capture256.height,
                          1)
                       toTexture:CaptureState->Snapshot256
                destinationSlice:layer
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

    [encoder setComputePipelineState:CaptureState->CapturePipeline];
    [encoder setBuffer:CaptureState->LineConfigBuffer offset:0 atIndex:0];
    [encoder setBytes:&dispatchConfig
               length:sizeof(dispatchConfig)
              atIndex:1];
    [encoder setBuffer:CaptureState->AuxVramBuffer offset:0 atIndex:2];
    [encoder setBuffer:CaptureState->AuxFifoBuffer offset:0 atIndex:3];
    [encoder setTexture:engineA2D atIndex:0];
    [encoder setTexture:high3D atIndex:1];
    [encoder setTexture:CaptureState->Snapshot128 atIndex:2];
    [encoder setTexture:CaptureState->Snapshot256 atIndex:3];
    [encoder setTexture:CaptureState->Capture128 atIndex:4];
    [encoder setTexture:CaptureState->Capture256 atIndex:5];

    const NSUInteger threadWidth =
        std::max<NSUInteger>(1, CaptureState->CapturePipeline.threadExecutionWidth);
    const NSUInteger threadHeight = std::max<NSUInteger>(
        1,
        CaptureState->CapturePipeline.maxTotalThreadsPerThreadgroup /
            threadWidth);
    [encoder dispatchThreads:MTLSizeMake(
                256u * static_cast<NSUInteger>(CaptureState->Scale),
                192u * static_cast<NSUInteger>(CaptureState->Scale),
                1)
           threadsPerThreadgroup:MTLSizeMake(threadWidth, threadHeight, 1)];
    [encoder endEncoding];

    // MELONPRIME_METAL_GPU_CAPTURE_COMPLETION_V1: collect (layer, size) pairs
    // written this dispatch and snapshot each one's DirtySerial as
    // SubmittedSerial *before* commit, under the same mutex the completion
    // handler below and the CPU-fallback path both use. Finalizing Valid=true
    // here (queue-ordering guarantees success, but not GPU-error-free
    // success) let a failed command buffer leave stale/garbage texture
    // contents marked Valid -- any subsequent read (BG/OBJ sampling this
    // frame, or an on-demand CPU readback) would then treat that garbage as
    // authoritative.
    struct PendingCaptureWrite { int Layer; int Size; uint64_t SubmittedSerial; };
    std::vector<PendingCaptureWrite> pendingWrites;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        for (int line = 0; line < 192; line++)
        {
            const auto& config = lines[line];
            if (!config.Enabled)
                continue;
            const int bank = static_cast<int>((config.CaptureCnt >> 16) & 3u);
            const int start = static_cast<int>((config.CaptureCnt >> 18) & 3u);
            const int size = static_cast<int>((config.CaptureCnt >> 20) & 3u);
            const int layer = (bank << 2) | start;
            auto& meta = CaptureState->Meta[layer];
            meta.UploadInFlight = true;
            meta.SubmittedSerial = meta.DirtySerial;
            pendingWrites.push_back({layer, size, meta.DirtySerial});
        }
    }

    std::shared_ptr<MetalCaptureState> capturedState = CaptureState;
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
        for (const auto& write : pendingWrites)
        {
            auto& meta = capturedState->Meta[write.Layer];
            meta.UploadInFlight = false;
            if (!ok)
            {
                meta.Valid = false;
                meta.Dirty = true;
                continue;
            }
            if (meta.DirtySerial == write.SubmittedSerial)
            {
                meta.Size = write.Size;
                meta.Valid = true;
                meta.Dirty = false;
            }
            else
            {
                // The destination changed again (AllocCapture) while this
                // write was in flight -- this result is stale, leave Dirty
                // so a later write (GPU capture or CPU fallback) finalizes it.
                meta.Valid = false;
                meta.Dirty = true;
            }
        }
    }];
    [command commit];

    if (!CaptureState->LoggedFirstCapture)
    {
        CaptureState->LoggedFirstCapture = true;
        std::fprintf(stderr,
            "[MelonPrime] metal display capture: first GPU capture submitted "
            "scale=%d readback=0\n",
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
    const NSUInteger scale =
        static_cast<NSUInteger>(std::max(1, capturedState->Scale));
    const NSUInteger width = static_cast<NSUInteger>(nativeWidth) * scale;
    const NSUInteger height = static_cast<NSUInteger>(nativeHeight) * scale;
    std::vector<uint32_t>& pixels = capturedState->CpuUploadScratch;
    if (pixels.size() < width * height)
        pixels.resize(width * height);

    for (NSUInteger y = 0; y < nativeHeight; y++)
    {
        for (NSUInteger x = 0; x < nativeWidth; x++)
        {
            const uint32_t packed =
                MetalPackRgb5551AsRgba8(source[y * nativeWidth + x]);
            for (NSUInteger sy = 0; sy < scale; sy++)
            {
                uint32_t* row = pixels.data() +
                    (y * scale + sy) * width + x * scale;
                for (NSUInteger sx = 0; sx < scale; sx++)
                    row[sx] = packed;
            }
        }
    }

    const NSUInteger requiredBytes = width * height * sizeof(uint32_t);
    // MELONPRIME_METAL_CAPTURE_STAGING_RING_V1: reuse a persistent MTLBuffer
    // instead of newBufferWithBytes: on every fallback-upload frame -- this
    // hot path fires on every CPU-fallback frame with a pending capture
    // write, and per-frame Metal buffer allocation was a measured hot-path
    // cost the same way the CpuUploadScratch vector allocation was. Falls
    // back to a one-off allocation (rare: every ring slot genuinely busy, or
    // the growth allocation itself failed) so correctness never depends on
    // the ring being sized exactly right.
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
        // The caller already set UploadInFlight=true for these layers before
        // calling this function (so a concurrent dispatch could not double-
        // submit them); since no command was committed, no completion
        // handler will ever run to clear it. Without this, a command/blit
        // allocation failure here would permanently strand these layers with
        // UploadInFlight stuck true, and UploadCpuCompletedCaptures() would
        // never retry them again.
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (int layer : affectedLayers)
            capturedState->Meta[layer].UploadInFlight = false;
        return false;
    }

    const NSUInteger bytesPerRow = width * sizeof(uint32_t);
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

    // This layer's SubmittedSerial was snapshotted by the caller (under
    // MetaMutex, before UploadInFlight was set) before dispatch, so the
    // handler can tell a merely-superseded write (some other write will
    // finalize this layer) apart from data that changed again while this
    // particular write was in flight (must not be marked valid).
    std::vector<std::pair<int, uint64_t>> layerSerials;
    layerSerials.reserve(affectedLayers.size());
    {
        std::lock_guard<std::mutex> lock(capturedState->MetaMutex);
        for (int layer : affectedLayers)
            layerSerials.emplace_back(layer, capturedState->Meta[layer].SubmittedSerial);
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
            meta.UploadInFlight = false;
            if (!ok)
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
    // is Dirty *and not already UploadInFlight* is eligible for a new
    // dispatch this frame. Without the UploadInFlight check, a GPU that
    // takes longer than one frame to complete a write caused a new upload to
    // be (re)dispatched every single frame for the same still-pending data,
    // permanently superseding the previous in-flight write's completion
    // before it could ever land -- Dirty/Valid then never settled, and the
    // renderer could never consider capture resources coherent again.
    uint32_t upload128Mask = 0;
    uint32_t upload256Mask = 0;
    {
        std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
        for (int layer = 0; layer < 16; layer++)
        {
            const auto& meta = CaptureState->Meta[layer];
            if (!meta.Dirty || meta.UploadInFlight || meta.Size < 0)
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
    // MetalCaptureState. See MetalCaptureState::CaptureMeta::SubmittedSerial
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
            meta.UploadInFlight = true;
            meta.SubmittedSerial = meta.DirtySerial;
        }
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture128,
                layer,
                source,
                128u,
                128u,
                std::vector<int>{static_cast<int>(layer)}))
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
                if (meta.Dirty && !meta.UploadInFlight && meta.Size > 0)
                {
                    meta.UploadInFlight = true;
                    meta.SubmittedSerial = meta.DirtySerial;
                    affectedLayers.push_back(layer);
                }
            }
        }
        if (affectedLayers.empty())
            continue;
        if (!UploadCaptureTexture(
                capturedState,
                (__bridge void*)CaptureState->Capture256,
                bank,
                source,
                256u,
                256u,
                std::move(affectedLayers)))
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

    MetalCaptureReadbackConfigCpu config = {};
    config.Scale = static_cast<uint32_t>(
        std::max(1, CaptureState->Scale));
    config.Use128 = len == 0 ? 1u : 0u;
    config.Width = len == 0 ? 128u : 256u;
    config.Height = len == 0 ? 128u : 256u;
    config.Layer = len == 0 ? metadataLayer : bank;

    id<MTLCommandBuffer> command = [CaptureState->Queue commandBuffer];
    id<MTLComputeCommandEncoder> encoder =
        command ? [command computeCommandEncoder] : nil;
    if (!command || !encoder)
        return;

    [encoder setComputePipelineState:CaptureState->ReadbackPipeline];
    [encoder setBytes:&config length:sizeof(config) atIndex:0];
    [encoder setBuffer:CaptureState->ReadbackBuffer offset:0 atIndex:1];
    [encoder setTexture:CaptureState->Capture128 atIndex:0];
    [encoder setTexture:CaptureState->Capture256 atIndex:1];

    const NSUInteger threadWidth =
        std::max<NSUInteger>(1, CaptureState->ReadbackPipeline.threadExecutionWidth);
    const NSUInteger threadHeight = std::max<NSUInteger>(
        1,
        CaptureState->ReadbackPipeline.maxTotalThreadsPerThreadgroup /
            threadWidth);
    [encoder dispatchThreads:MTLSizeMake(config.Width, config.Height, 1)
           threadsPerThreadgroup:MTLSizeMake(threadWidth, threadHeight, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted)
        return;

    const uint16_t* pixels = static_cast<const uint16_t*>(
        [CaptureState->ReadbackBuffer contents]);
    uint16_t* vram = reinterpret_cast<uint16_t*>(GPU.VRAM[bank]);
    if (len == 0)
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
    return CaptureState && CaptureState->Capture128
        ? (__bridge void*)CaptureState->Capture128
        : nullptr;
}

void* MetalRenderer::GetMetalCapture256Texture() const
{
    return CaptureState && CaptureState->Capture256
        ? (__bridge void*)CaptureState->Capture256
        : nullptr;
}
