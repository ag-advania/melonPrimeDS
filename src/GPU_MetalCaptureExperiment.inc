// Included after GPU_MetalCaptureMethods.inc.
// MELONPRIME_METAL_CAPTURE_EXPERIMENT_V1
//
// Debug-only Display Capture differential scaffold (PR-2).
// When MELONPRIME_METAL_CAPTURE_EXPERIMENT=1:
//   - SoftRenderer::DoCapture remains the production reference (display path)
//   - a separate Metal candidate is generated from the same Soft sources
//   - candidate never feeds RendererOutput / AcquireOutputLease
//   - checksum / pixel diffs + optional artifacts are recorded
// Production Capture128/256 Meta state machine is intentionally unused.

namespace
{

bool MetalCaptureExperimentEnabled()
{
    static const bool enabled = []() {
        const char* env = std::getenv("MELONPRIME_METAL_CAPTURE_EXPERIMENT");
        return env && env[0] == '1';
    }();
    return enabled;
}

const char* MetalCaptureExperimentArtifactDir()
{
    static const char* dir = std::getenv("MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR");
    return (dir && dir[0] != '\0') ? dir : nullptr;
}

static constexpr const char* kMetalCaptureExperimentShaderSource = R"MSL(
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

// SoftRenderer color packing: 6-bit channels with alpha in the high byte.
// Matches SoftRenderer::DoCapture source-A unpack.
static inline uint3 mp_soft_rgb5(uint packed)
{
    return uint3(
        (packed >> 1u) & 0x1Fu,
        (packed >> 9u) & 0x1Fu,
        (packed >> 17u) & 0x1Fu);
}

static inline uint mp_soft_alpha1(uint packed)
{
    return ((packed >> 24u) != 0u) ? 1u : 0u;
}

static inline uint4 mp_rgba8_to_rgb5a1(uint packed)
{
    return uint4(
        (packed >> 3u) & 0x1Fu,
        (packed >> 11u) & 0x1Fu,
        (packed >> 19u) & 0x1Fu,
        ((packed >> 24u) != 0u) ? 1u : 0u);
}

static inline ushort mp_pack_rgb5551(uint3 rgb5, uint a1)
{
    return ushort(
        (rgb5.x & 0x1Fu) |
        ((rgb5.y & 0x1Fu) << 5u) |
        ((rgb5.z & 0x1Fu) << 10u) |
        ((a1 & 1u) << 15u));
}

kernel void mp_metal_capture_experiment(
    constant CaptureLineConfig* lines [[buffer(0)]],
    device const uint* srcA [[buffer(1)]],
    device const uint* auxVram [[buffer(2)]],
    device const uint* auxFifo [[buffer(3)]],
    device ushort* candidate [[buffer(4)]],
    uint2 gid [[thread_position_in_grid]])
{
    uint nativeX = gid.x;
    uint nativeLine = gid.y;
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

    uint srcAPacked = srcA[(nativeLine * 256u) + nativeX];
    // Soft path already applied the 3D X offset when filling srcA for
    // capture; keep the Soft row as-is (srcAXOffset is metadata only here).
    (void)config.srcAXOffset;

    uint3 rgbA = mp_soft_rgb5(srcAPacked);
    uint alphaA = mp_soft_alpha1(srcAPacked);

    uint4 rgbB = uint4(0u);
    const bool fifo = (config.captureCnt & (1u << 25u)) != 0u;
    uint srcBIndex = (config.srcBNativeY * 256u) + nativeX;
    if (fifo)
        rgbB = mp_rgba8_to_rgb5a1(auxFifo[srcBIndex]);
    else if (config.srcBCaptureLayer < 0)
        rgbB = mp_rgba8_to_rgb5a1(auxVram[srcBIndex]);
    // Capture-backed source B is out of scope for this Soft-source scaffold;
    // leave B as zero (same Soft fallback when VRAM is unmapped).

    uint dstMode = (config.captureCnt >> 29u) & 3u;
    ushort outValue = 0;
    if (dstMode == 0u)
    {
        outValue = mp_pack_rgb5551(rgbA, alphaA);
    }
    else if (dstMode == 1u)
    {
        outValue = mp_pack_rgb5551(rgbB.xyz, rgbB.w);
    }
    else
    {
        uint eva = min(config.captureCnt & 0x1Fu, 16u);
        uint evb = min((config.captureCnt >> 8u) & 0x1Fu, 16u);
        uint3 out5 = min(
            ((rgbA * alphaA * eva) + (rgbB.xyz * rgbB.w * evb) + 8u) >> 4u,
            uint3(31u));
        uint outAlpha =
            ((eva > 0u) ? alphaA : 0u) |
            ((evb > 0u) ? rgbB.w : 0u);
        outValue = mp_pack_rgb5551(out5, outAlpha);
    }

    candidate[(nativeLine * 256u) + nativeX] = outValue;
}
)MSL";

struct MetalCaptureExperimentLineMeta
{
    bool Enabled = false;
    uint32_t CaptureCnt = 0;
    uint32_t DispCntA = 0;
    int32_t SrcAXOffset = 0;
    int32_t SrcBCaptureLayer = -1;
    uint32_t SrcBCaptureSize = 0;
    uint32_t SrcBNativeY = 0;
    uint32_t DstBank = 0;
    uint32_t DstOffset = 0;
    uint32_t CaptureSize = 0;
    uint32_t Width = 0;
    uint32_t Height = 0;
    uint32_t EVA = 0;
    uint32_t EVB = 0;
    uint32_t DstMode = 0;
    bool Fifo = false;
    bool ScreenSwap = false;
    uint16_t BrightnessA = 0;
    uint16_t BrightnessB = 0;
    uint32_t ReferenceChecksum = 0;
    uint32_t CandidateChecksum = 0;
    uint32_t MismatchCount = 0;
    int32_t FirstMismatchX = -1;
    uint32_t MaxChannelDelta = 0;
};

uint32_t MetalCaptureExperimentChecksum(const uint16_t* row, uint32_t width)
{
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0; i < width; i++)
    {
        hash ^= row[i];
        hash *= 16777619u;
    }
    return hash;
}

void MetalCaptureExperimentWritePpm(
    const char* path,
    const uint16_t rows[192][256],
    const MetalCaptureExperimentLineMeta metas[192])
{
    FILE* fp = std::fopen(path, "wb");
    if (!fp)
        return;
    std::fprintf(fp, "P6\n256 192\n255\n");
    for (int y = 0; y < 192; y++)
    {
        const uint32_t width = metas[y].Enabled ? metas[y].Width : 0u;
        for (int x = 0; x < 256; x++)
        {
            uint8_t rgb[3] = {0, 0, 0};
            if (metas[y].Enabled && static_cast<uint32_t>(x) < width)
            {
                const uint16_t c = rows[y][x];
                rgb[0] = static_cast<uint8_t>(((c & 0x1Fu) << 3) | ((c & 0x1Fu) >> 2));
                rgb[1] = static_cast<uint8_t>(
                    (((c >> 5) & 0x1Fu) << 3) | (((c >> 5) & 0x1Fu) >> 2));
                rgb[2] = static_cast<uint8_t>(
                    (((c >> 10) & 0x1Fu) << 3) | (((c >> 10) & 0x1Fu) >> 2));
            }
            std::fwrite(rgb, 1, 3, fp);
        }
    }
    std::fclose(fp);
}

} // namespace

struct MetalRenderer::MetalCaptureExperimentState
{
    id<MTLDevice> Device = nil;
    id<MTLCommandQueue> Queue = nil;
    id<MTLLibrary> Library = nil;
    id<MTLComputePipelineState> Pipeline = nil;
    id<MTLBuffer> LineConfigBuffer = nil;
    id<MTLBuffer> SrcABuffer = nil;
    id<MTLBuffer> AuxVramBuffer = nil;
    id<MTLBuffer> AuxFifoBuffer = nil;
    id<MTLBuffer> CandidateBuffer = nil;
    bool Ready = false;
    bool LoggedInit = false;
    bool FrameHadCapture = false;
    uint64_t FrameSerial = 0;
    uint64_t ComparedFrames = 0;
    uint64_t MismatchFrames = 0;
    uint64_t TotalMismatchPixels = 0;
    MetalCaptureExperimentLineMeta Lines[192] = {};
    uint16_t ReferenceNative[192][256] = {};
    uint16_t CandidateNative[192][256] = {};
};

bool MetalRenderer::EnsureMetalCaptureExperimentState(void* preferredDevice)
{
    if (!MetalCaptureExperimentEnabled())
        return false;

    if (!CaptureExperimentState)
        CaptureExperimentState = std::make_unique<MetalCaptureExperimentState>();

    auto& state = *CaptureExperimentState;
    if (state.Ready)
        return true;

    id<MTLDevice> device = preferredDevice
        ? (__bridge id<MTLDevice>)preferredDevice
        : MTLCreateSystemDefaultDevice();
    if (!device)
        return false;

    NSError* error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithSource:@(kMetalCaptureExperimentShaderSource)
                            options:nil
                              error:&error];
    if (!library)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal capture experiment: shader compile failed: %s\n",
            error ? [[error localizedDescription] UTF8String] : "unknown");
        return false;
    }

    id<MTLFunction> function =
        [library newFunctionWithName:@"mp_metal_capture_experiment"];
    if (!function)
        return false;

    id<MTLComputePipelineState> pipeline =
        [device newComputePipelineStateWithFunction:function error:&error];
    if (!pipeline)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal capture experiment: pipeline failed: %s\n",
            error ? [[error localizedDescription] UTF8String] : "unknown");
        return false;
    }

    id<MTLCommandQueue> queue = [device newCommandQueue];
    if (!queue)
        return false;

    constexpr NSUInteger kLineBytes =
        192u * sizeof(MetalCaptureLineConfigCpu);
    constexpr NSUInteger kPixelU32Bytes = 256u * 192u * sizeof(uint32_t);
    constexpr NSUInteger kCandidateBytes = 256u * 192u * sizeof(uint16_t);

    state.Device = device;
    state.Queue = queue;
    state.Library = library;
    state.Pipeline = pipeline;
    state.LineConfigBuffer =
        [device newBufferWithLength:kLineBytes options:MTLResourceStorageModeShared];
    state.SrcABuffer =
        [device newBufferWithLength:kPixelU32Bytes options:MTLResourceStorageModeShared];
    state.AuxVramBuffer =
        [device newBufferWithLength:kPixelU32Bytes options:MTLResourceStorageModeShared];
    state.AuxFifoBuffer =
        [device newBufferWithLength:kPixelU32Bytes options:MTLResourceStorageModeShared];
    state.CandidateBuffer =
        [device newBufferWithLength:kCandidateBytes options:MTLResourceStorageModeShared];

    if (!state.LineConfigBuffer || !state.SrcABuffer || !state.AuxVramBuffer ||
        !state.AuxFifoBuffer || !state.CandidateBuffer)
    {
        return false;
    }

    std::memset([state.LineConfigBuffer contents], 0, kLineBytes);
    std::memset([state.SrcABuffer contents], 0, kPixelU32Bytes);
    std::memset([state.AuxVramBuffer contents], 0, kPixelU32Bytes);
    std::memset([state.AuxFifoBuffer contents], 0, kPixelU32Bytes);
    std::memset([state.CandidateBuffer contents], 0, kCandidateBytes);
    state.Ready = true;

    if (!state.LoggedInit)
    {
        state.LoggedInit = true;
        const char* artifactDir = MetalCaptureExperimentArtifactDir();
        std::fprintf(stderr,
            "[MelonPrime] metal capture experiment: enabled "
            "(candidate never presented; artifacts=%s)\n",
            artifactDir ? artifactDir : "off");
    }
    return true;
}

void MetalRenderer::BeginMetalCaptureExperimentFrame()
{
    if (!CaptureExperimentState)
        return;
    auto& state = *CaptureExperimentState;
    state.FrameHadCapture = false;
    for (auto& line : state.Lines)
        line = MetalCaptureExperimentLineMeta{};
    if (state.Ready)
    {
        std::memset([state.LineConfigBuffer contents], 0,
            192u * sizeof(MetalCaptureLineConfigCpu));
        std::memset([state.SrcABuffer contents], 0,
            256u * 192u * sizeof(uint32_t));
        std::memset([state.AuxVramBuffer contents], 0,
            256u * 192u * sizeof(uint32_t));
        std::memset([state.AuxFifoBuffer contents], 0,
            256u * 192u * sizeof(uint32_t));
        std::memset([state.CandidateBuffer contents], 0,
            256u * 192u * sizeof(uint16_t));
    }
    std::memset(state.ReferenceNative, 0, sizeof(state.ReferenceNative));
    std::memset(state.CandidateNative, 0, sizeof(state.CandidateNative));
}

void MetalRenderer::MetalCaptureExperimentRecordLine(u32 line)
{
    if (!MetalCaptureExperimentEnabled())
        return;
    if (!EnsureMetalCaptureExperimentState(
            CaptureState ? (__bridge void*)CaptureState->Device : nullptr))
    {
        return;
    }
    if (line >= 192u || !GPU.CaptureEnable)
        return;

    auto& state = *CaptureExperimentState;
    if (line == 0u)
        BeginMetalCaptureExperimentFrame();

    const u32 captureCnt = GPU.CaptureCnt;
    u32 width = 256;
    u32 height = 192;
    const u32 sz = (captureCnt >> 20) & 0x3;
    if (sz == 0)
    {
        width = 128;
        height = 128;
    }
    else
    {
        width = 256;
        height = 64 * sz;
    }
    if (line >= height)
        return;

    const u32 dstvram = (captureCnt >> 16) & 0x3;
    if (!(GPU.VRAMMap_LCDC & (1u << dstvram)))
        return;

    auto& meta = state.Lines[line];
    meta.Enabled = true;
    meta.CaptureCnt = captureCnt;
    meta.DispCntA = GPU.GPU2D_A.DispCnt;
    meta.DstBank = dstvram;
    meta.DstOffset = (captureCnt >> 18) & 0x3;
    meta.CaptureSize = sz;
    meta.Width = width;
    meta.Height = height;
    meta.EVA = captureCnt & 0x1F;
    meta.EVB = (captureCnt >> 8) & 0x1F;
    meta.DstMode = (captureCnt >> 29) & 0x3;
    meta.Fifo = (captureCnt & (1u << 25)) != 0;
    meta.ScreenSwap = GPU.ScreenSwap;
    meta.BrightnessA = GPU.MasterBrightnessA;
    meta.BrightnessB = GPU.MasterBrightnessB;
    meta.SrcBCaptureLayer = -1;
    meta.SrcBNativeY = line;

    if ((captureCnt & (1u << 24)) != 0u)
    {
        int xpos = GPU.GPU3D.GetRenderXPos() & 0x1FF;
        xpos -= ((xpos & 0x100) << 1);
        meta.SrcAXOffset = xpos;
    }

    auto* lineConfigs = static_cast<MetalCaptureLineConfigCpu*>(
        [state.LineConfigBuffer contents]);
    auto& config = lineConfigs[line];
    config = MetalCaptureLineConfigCpu{};
    config.Enabled = 1;
    config.DispCntA = meta.DispCntA;
    config.CaptureCnt = captureCnt;
    config.SrcAXOffset = meta.SrcAXOffset;
    config.SrcBCaptureLayer = -1;
    config.SrcBNativeY = line;

    auto* srcA = static_cast<uint32_t*>([state.SrcABuffer contents]) + line * 256u;
    const u32* softSrcA =
        (captureCnt & (1u << 24)) ? Output3D : Output2D[0];
    if (softSrcA)
        std::memcpy(srcA, softSrcA, 256u * sizeof(uint32_t));
    else
        std::memset(srcA, 0, 256u * sizeof(uint32_t));

    const u32 dstMode = meta.DstMode;
    const u32 evb = std::min(meta.EVB, 16u);
    const bool useSourceB =
        dstMode == 1u || ((dstMode == 2u || dstMode == 3u) && evb > 0u);
    if (useSourceB)
    {
        if (meta.Fifo)
        {
            auto* target = static_cast<uint32_t*>(
                [state.AuxFifoBuffer contents]) + line * 256u;
            for (u32 x = 0; x < 256; x++)
                target[x] = MetalPackRgb5551AsRgba8(GPU.DispFIFOBuffer[x]);
        }
        else
        {
            const u32 dispCntA = GPU.GPU2D_A.DispCnt;
            const u32 displayMode = (dispCntA >> 16) & 0x3;
            const u32 srcBank = (dispCntA >> 18) & 0x3;
            const u32 srcOffset =
                displayMode == 2u ? 0u : ((captureCnt >> 26) & 0x3);
            const u32 srcY = (line + srcOffset * 64u) & 0xFFu;
            meta.SrcBNativeY = srcY;
            config.SrcBNativeY = srcY;

            const int captureLayer = GPU.GetCaptureBlock_LCDC(
                (srcBank << 17u) | ((srcY >> 6u) << 15u));
            if (captureLayer >= 0)
            {
                // Scaffold records metadata but does not sample production
                // Capture128/256 textures (those stay production-owned).
                meta.SrcBCaptureLayer = captureLayer;
                config.SrcBCaptureLayer = captureLayer;
                if (CaptureState)
                {
                    std::lock_guard<std::mutex> lock(CaptureState->MetaMutex);
                    meta.SrcBCaptureSize =
                        static_cast<uint32_t>(CaptureState->Meta[captureLayer].Size);
                    config.SrcBCaptureSize = meta.SrcBCaptureSize;
                }
            }
            else
            {
                auto* target = static_cast<uint32_t*>(
                    [state.AuxVramBuffer contents]) + srcY * 256u;
                if ((GPU.VRAMMap_LCDC & (1u << srcBank)) == 0u)
                {
                    std::fill(target, target + 256, 0u);
                }
                else
                {
                    const uint16_t* source =
                        reinterpret_cast<const uint16_t*>(GPU.VRAM[srcBank]) +
                        srcY * 256u;
                    for (u32 x = 0; x < 256; x++)
                        target[x] = MetalPackRgb5551AsRgba8(source[x]);
                }
            }
        }
    }

    // SoftRenderer::DoCapture already wrote the CPU reference into VRAM.
    u16* dst = reinterpret_cast<u16*>(GPU.VRAM[dstvram]);
    const u32 dstaddr =
        (((captureCnt >> 18) & 0x3) << 14) + (line * width);
    dst += (dstaddr & 0xFFFF);
    std::memcpy(state.ReferenceNative[line], dst, width * sizeof(uint16_t));
    meta.ReferenceChecksum =
        MetalCaptureExperimentChecksum(state.ReferenceNative[line], width);

    state.FrameHadCapture = true;
}

void MetalRenderer::MetalCaptureExperimentWriteArtifacts(
    const MetalCaptureExperimentState& state,
    uint32_t frameMismatchPixels) const
{
    const char* dir = MetalCaptureExperimentArtifactDir();
    if (!dir)
        return;

    char path[1024];
    auto makePath = [&](const char* name) -> const char* {
        std::snprintf(path, sizeof(path), "%s/%s", dir, name);
        return path;
    };

    {
        FILE* fp = std::fopen(makePath("reference-native.rgb5551"), "wb");
        if (fp)
        {
            std::fwrite(state.ReferenceNative, sizeof(uint16_t), 256u * 192u, fp);
            std::fclose(fp);
        }
    }
    {
        FILE* fp = std::fopen(makePath("metal-native.rgb5551"), "wb");
        if (fp)
        {
            std::fwrite(state.CandidateNative, sizeof(uint16_t), 256u * 192u, fp);
            std::fclose(fp);
        }
    }

    MetalCaptureExperimentWritePpm(
        makePath("reference.ppm"), state.ReferenceNative, state.Lines);
    MetalCaptureExperimentWritePpm(
        makePath("metal.ppm"), state.CandidateNative, state.Lines);

    {
        FILE* fp = std::fopen(makePath("diff.ppm"), "wb");
        if (fp)
        {
            std::fprintf(fp, "P6\n256 192\n255\n");
            for (int y = 0; y < 192; y++)
            {
                const uint32_t width =
                    state.Lines[y].Enabled ? state.Lines[y].Width : 0u;
                for (int x = 0; x < 256; x++)
                {
                    uint8_t rgb[3] = {0, 0, 0};
                    if (state.Lines[y].Enabled &&
                        static_cast<uint32_t>(x) < width &&
                        state.ReferenceNative[y][x] != state.CandidateNative[y][x])
                    {
                        rgb[0] = 255;
                    }
                    std::fwrite(rgb, 1, 3, fp);
                }
            }
            std::fclose(fp);
        }
    }

    {
        FILE* fp = std::fopen(makePath("capture-lines.csv"), "wb");
        if (fp)
        {
            std::fprintf(fp,
                "line,enabled,captureCnt,dispCntA,srcAXOffset,srcBCaptureLayer,"
                "srcBCaptureSize,srcBNativeY,dstBank,dstOffset,captureSize,width,"
                "height,eva,evb,dstMode,fifo,screenSwap,brightnessA,brightnessB,"
                "refChecksum,candChecksum,mismatchCount,firstMismatchX,"
                "maxChannelDelta\n");
            for (int line = 0; line < 192; line++)
            {
                const auto& m = state.Lines[line];
                std::fprintf(fp,
                    "%d,%d,0x%08x,0x%08x,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,"
                    "%d,0x%04x,0x%04x,0x%08x,0x%08x,%u,%d,%u\n",
                    line,
                    m.Enabled ? 1 : 0,
                    m.CaptureCnt,
                    m.DispCntA,
                    m.SrcAXOffset,
                    m.SrcBCaptureLayer,
                    m.SrcBCaptureSize,
                    m.SrcBNativeY,
                    m.DstBank,
                    m.DstOffset,
                    m.CaptureSize,
                    m.Width,
                    m.Height,
                    m.EVA,
                    m.EVB,
                    m.DstMode,
                    m.Fifo ? 1 : 0,
                    m.ScreenSwap ? 1 : 0,
                    m.BrightnessA,
                    m.BrightnessB,
                    m.ReferenceChecksum,
                    m.CandidateChecksum,
                    m.MismatchCount,
                    m.FirstMismatchX,
                    m.MaxChannelDelta);
            }
            std::fclose(fp);
        }
    }

    {
        FILE* fp = std::fopen(makePath("frame.json"), "wb");
        if (fp)
        {
            std::fprintf(fp,
                "{\n"
                "  \"frameSerial\": %llu,\n"
                "  \"comparedFrames\": %llu,\n"
                "  \"mismatchFrames\": %llu,\n"
                "  \"frameMismatchPixels\": %u,\n"
                "  \"totalMismatchPixels\": %llu,\n"
                "  \"captureCnt\": \"0x%08x\",\n"
                "  \"note\": \"candidate never presented; SoftRenderer remains display authority\"\n"
                "}\n",
                static_cast<unsigned long long>(state.FrameSerial),
                static_cast<unsigned long long>(state.ComparedFrames),
                static_cast<unsigned long long>(state.MismatchFrames),
                frameMismatchPixels,
                static_cast<unsigned long long>(state.TotalMismatchPixels),
                GPU.CaptureCnt);
            std::fclose(fp);
        }
    }

    {
        FILE* fp = std::fopen(makePath("commands.txt"), "wb");
        if (fp)
        {
            std::fprintf(fp,
                "MELONPRIME_METAL_CAPTURE_EXPERIMENT=1\n"
                "MELONPRIME_METAL_CAPTURE_EXPERIMENT_DIR=%s\n"
                "matrix:\n"
                "  sizes: 128x128, 256x64, 256x128, 256x192\n"
                "  sources: A-2D, A-3D, B-VRAM, B-FIFO, A+B\n"
                "  offsets: dst 0-3, src 0-3, signed 3D X\n"
                "artifacts: reference-native.rgb5551 metal-native.rgb5551 "
                "reference.ppm metal.ppm diff.ppm frame.json capture-lines.csv\n",
                dir);
            std::fclose(fp);
        }
    }
}

void MetalRenderer::FinishMetalCaptureExperimentFrame()
{
    if (!MetalCaptureExperimentEnabled())
        return;
    if (!CaptureExperimentState || !CaptureExperimentState->Ready)
        return;
    auto& state = *CaptureExperimentState;
    if (!state.FrameHadCapture)
        return;

    state.FrameSerial++;
    state.ComparedFrames++;

    id<MTLCommandBuffer> command = [state.Queue commandBuffer];
    if (!command)
        return;
    command.label = @"MelonPrime Metal Capture Experiment";

    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder];
    if (!encoder)
        return;

    [encoder setComputePipelineState:state.Pipeline];
    [encoder setBuffer:state.LineConfigBuffer offset:0 atIndex:0];
    [encoder setBuffer:state.SrcABuffer offset:0 atIndex:1];
    [encoder setBuffer:state.AuxVramBuffer offset:0 atIndex:2];
    [encoder setBuffer:state.AuxFifoBuffer offset:0 atIndex:3];
    [encoder setBuffer:state.CandidateBuffer offset:0 atIndex:4];

    const NSUInteger threadWidth =
        std::max<NSUInteger>(1, state.Pipeline.threadExecutionWidth);
    const NSUInteger threadHeight = std::max<NSUInteger>(
        1, state.Pipeline.maxTotalThreadsPerThreadgroup / threadWidth);
    [encoder dispatchThreads:MTLSizeMake(256, 192, 1)
       threadsPerThreadgroup:MTLSizeMake(threadWidth, threadHeight, 1)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];

    const auto* candidate =
        static_cast<const uint16_t*>([state.CandidateBuffer contents]);
    uint32_t frameMismatchPixels = 0;
    int firstMismatchLine = -1;
    int firstMismatchX = -1;

    for (int line = 0; line < 192; line++)
    {
        auto& meta = state.Lines[line];
        if (!meta.Enabled)
            continue;

        // Capture-backed source B is intentionally skipped until PR-3/PR-5
        // wire native capture textures into this scaffold.
        if (meta.SrcBCaptureLayer >= 0)
            continue;

        std::memcpy(
            state.CandidateNative[line],
            candidate + line * 256,
            meta.Width * sizeof(uint16_t));
        meta.CandidateChecksum =
            MetalCaptureExperimentChecksum(state.CandidateNative[line], meta.Width);

        for (uint32_t x = 0; x < meta.Width; x++)
        {
            const uint16_t ref = state.ReferenceNative[line][x];
            const uint16_t cand = state.CandidateNative[line][x];
            if (ref == cand)
                continue;

            meta.MismatchCount++;
            frameMismatchPixels++;
            if (meta.FirstMismatchX < 0)
                meta.FirstMismatchX = static_cast<int32_t>(x);
            if (firstMismatchLine < 0)
            {
                firstMismatchLine = line;
                firstMismatchX = static_cast<int>(x);
            }

            const int dr = std::abs(
                static_cast<int>(ref & 0x1F) - static_cast<int>(cand & 0x1F));
            const int dg = std::abs(
                static_cast<int>((ref >> 5) & 0x1F) -
                static_cast<int>((cand >> 5) & 0x1F));
            const int db = std::abs(
                static_cast<int>((ref >> 10) & 0x1F) -
                static_cast<int>((cand >> 10) & 0x1F));
            const uint32_t delta = static_cast<uint32_t>(std::max({dr, dg, db}));
            if (delta > meta.MaxChannelDelta)
                meta.MaxChannelDelta = delta;
        }
    }

    if (frameMismatchPixels > 0)
    {
        state.MismatchFrames++;
        state.TotalMismatchPixels += frameMismatchPixels;
        if (state.MismatchFrames <= 3 || (state.MismatchFrames % 60) == 0)
        {
            const auto& m =
                state.Lines[firstMismatchLine >= 0 ? firstMismatchLine : 0];
            std::fprintf(stderr,
                "[MelonPrime] metal capture experiment: mismatch "
                "frameSerial=%llu pixels=%u firstLine=%d firstX=%d "
                "captureCnt=0x%08x dispCntA=0x%08x dstMode=%u eva=%u evb=%u "
                "fifo=%d srcBLayer=%d maxDelta=%u\n",
                static_cast<unsigned long long>(state.FrameSerial),
                frameMismatchPixels,
                firstMismatchLine,
                firstMismatchX,
                m.CaptureCnt,
                m.DispCntA,
                m.DstMode,
                m.EVA,
                m.EVB,
                m.Fifo ? 1 : 0,
                m.SrcBCaptureLayer,
                m.MaxChannelDelta);
        }
    }
    else if (state.ComparedFrames <= 3 || (state.ComparedFrames % 120) == 0)
    {
        std::fprintf(stderr,
            "[MelonPrime] metal capture experiment: match "
            "frameSerial=%llu compared=%llu\n",
            static_cast<unsigned long long>(state.FrameSerial),
            static_cast<unsigned long long>(state.ComparedFrames));
    }

    if (MetalCaptureExperimentArtifactDir() &&
        (frameMismatchPixels > 0 || state.ComparedFrames <= 2))
    {
        MetalCaptureExperimentWriteArtifacts(state, frameMismatchPixels);
    }

    state.FrameHadCapture = false;
}
