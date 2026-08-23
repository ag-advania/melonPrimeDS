/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#ifndef GPU3D_DX12_H
#define GPU3D_DX12_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D.h"
#include "GPU2DNative.h"
#include "GPU3D_FixedVariantIndex.h"
#include "MelonPrimeStructuredComposition.h"
#include "DX12Context.h"
#include "GPU3D_TexcacheDX12.h"

namespace melonDS
{

struct RendererOutput;
struct RendererOutputLease;

// DirectX 12 3D renderer: a port of the OpenGL compute renderer
// (GPU3D_Compute.cpp), i.e. the GPU version of the software rasterizer, with
// the same tile-binned pipeline and the same fixed-point math.
//
// It is paired with the software 2D renderer (see GPU_DX12.h). A native-size
// resolve remains available for display capture, while structured 2D metadata
// is recomposited with FinalFB at the configured internal resolution for the
// visible output.
class DX12Renderer3D : public Renderer3D
{
public:
    // Returns nullptr when DX12 is unavailable or device-side setup failed, so
    // the caller can fall back instead of constructing a dead renderer.
    static std::unique_ptr<DX12Renderer3D> New(melonDS::GPU3D& gpu3D);

    ~DX12Renderer3D() override;

    bool Init() override;
    void Reset() override;
    void ResetAfterSavestateLoad() override;
    void InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason reason) noexcept override;

    // Releases every GPU-visible object while the emulator core is still alive.
    void Stop();

    void SetRenderSettings(int scale, bool hiresCoordinates);
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }
    // Required before changing or destroying XeLL state. This queue-wide
    // fence also retires native-presenter work submitted through the shared
    // direct queue.
    bool WaitForQueueIdle();

    void RenderFrame() override;
    u32* GetLine(int line) override;
    [[nodiscard]] bool UsesStructured2DMetadata() const noexcept override { return true; }
    [[nodiscard]] StructuredPerfBackend GetStructured2DPerfBackend() const noexcept override
    {
        return StructuredPerfBackend::DX12;
    }
    [[nodiscard]] bool HasValidCaptureFrame() const noexcept override { return FrameReadbackValid; }

    bool ComposeStructuredOutput(
        const std::array<const u32*, 14>& planes,
        const std::array<const u32*, 2>& lineMeta,
        const u32* captureCommands,
        const StructuredComposition::ScreenRoutingView& screenRouting,
        u64 generation,
        const StructuredComposition::GenerationState& contentGeneration);
    bool ComposeNativeGPU2D(
        const GPU2DNative::FrameInput& input,
        u64 generation,
        bool finalFBValid,
        const u32* expectedTop = nullptr,
        const u32* expectedBottom = nullptr);
    [[nodiscard]] RendererOutput GetComposedOutput() const;
    [[nodiscard]] bool HasFinalFBContent() const noexcept { return FinalFBHasValidFrame; }
    [[nodiscard]] RendererOutputLease AcquireComposedOutputLease();
    [[nodiscard]] u32 GetComposedWidth() const noexcept { return static_cast<u32>(ScreenWidth); }
    [[nodiscard]] u32 GetComposedHeight() const noexcept { return static_cast<u32>(ScreenHeight); }
    [[nodiscard]] bool HasRuntimeFailure() const noexcept { return RuntimeFailed; }
    [[nodiscard]] bool CanComposeNativeGPU2D() const noexcept;
    [[nodiscard]] GPU2DComposeResult GetLastComposeResult() const noexcept
    {
        return LastComposeResult;
    }
    // Materialize only the requested LCDC capture blocks when the emulation
    // core actually reads them.  The normal native frame path keeps this
    // mirror device-local and never calls this method.
    bool ReadNativeCapture(
        u32 bank,
        u32 start,
        u32 len,
        const CaptureBlockProvenance& expected,
        u8* destination);
    [[nodiscard]] NativeCaptureStateIdentity GetNativeCaptureStateIdentity(
        CaptureOwner owner) const noexcept;
    void InvalidateHighResCaptureRange(
        u32 bank,
        u32 start,
        u32 len,
        GPU2DNative::HighResCaptureFallbackReason reason) noexcept;
    [[nodiscard]] u64 GetPublishedOutputGeneration() const noexcept
    {
        return PublishedOutputGeneration;
    }
    [[nodiscard]] const std::string& GetRuntimeFailureReason() const noexcept
    {
        return RuntimeFailureReason;
    }
    void FailNativeGPU2DExact(const char* reason)
    {
        SetRuntimeFailure(reason ? std::string(reason) : std::string("native GPU2D exact validation failed"));
    }

    bool NeedsShaderCompile() override { return !RuntimeFailed && ShaderStepIdx < ShaderStepCount; }
    void ShaderCompileStep(int& current, int& count) override;

private:
    void ResetInternal(bool preservePresentation);

    explicit DX12Renderer3D(melonDS::GPU3D& gpu3D);

    static constexpr int MaxRenderPolygons = 2048;
    static constexpr int MaxVariants = MaxRenderPolygons;
    static constexpr int MaxYSpanSetups = MaxRenderPolygons * 10;
    static constexpr int BinStride = 2048 / 32;
    static constexpr int CoarseBinStride = BinStride / 32;
    static constexpr int CoarseTileCountX = 8;

    // Layout must stay byte-identical to the HLSL BinResult offsets in
    // GPU3D_DX12_shaders.h.
    struct BinResultHeader
    {
        u32 VariantWorkCount[MaxVariants * 4];
        u32 SortedWorkOffset[MaxVariants];
        u32 VariantWorkRealCount[MaxVariants];
        u32 SortWorkWorkCount[4];
    };

    // Mirrors the HLSL YSpanSetup / XSpanSetup / Polygon structured-buffer
    // layouts (tight 4-byte packing in both languages).
    struct SpanSetupY
    {
        s32 Z0, Z1, W0, W1;
        s32 ColorR0, ColorG0, ColorB0;
        s32 ColorR1, ColorG1, ColorB1;
        s32 TexcoordU0, TexcoordV0;
        s32 TexcoordU1, TexcoordV1;

        s32 I0, I1;
        u32 Linear;
        s32 IRecip;
        s32 W0n, W0d, W1d;

        s32 Increment;

        s32 X0, X1, Y0, Y1;
        s32 XMin, XMax;
        s32 DxInitial;

        s32 XCovIncr;
        u32 IsDummy;
    };

    struct SpanSetupX
    {
        s32 X0, X1;
        s32 InsideStart, InsideEnd, EdgeCovL, EdgeCovR;
        s32 XRecip;
        u32 Flags;
        s32 Z0, Z1, W0, W1;
        s32 ColorR0, ColorG0, ColorB0;
        s32 ColorR1, ColorG1, ColorB1;
        s32 TexcoordU0, TexcoordV0;
        s32 TexcoordU1, TexcoordV1;
        s32 CovLInitial, CovRInitial;
    };

    struct SetupIndices
    {
        u16 PolyIdx, SpanIdxL, SpanIdxR, Y;
    };

    struct RenderPolygon
    {
        s32 FirstXSpan;
        s32 YTop, YBot;

        s32 XMin, XMax;
        s32 XMinY, XMaxY;

        u32 Variant;
        u32 Attr;

        float TextureLayer;
        u32 FacingView;
    };

    static_assert(sizeof(SpanSetupY) == 31 * 4, "SpanSetupY must match the HLSL layout");
    static_assert(sizeof(SpanSetupX) == 24 * 4, "SpanSetupX must match the HLSL layout");
    static_assert(sizeof(RenderPolygon) == 11 * 4, "RenderPolygon must match the HLSL layout");
    static_assert(sizeof(SetupIndices) == 8, "SetupIndices must match the R16G16B16A16_UINT view");

    // One rasterise pipeline per texture/blend combination, exactly like the
    // OpenGL compute renderer's shader table.
    enum RasteriseKind
    {
        RasteriseKind_NoTexture = 0,
        RasteriseKind_NoTextureToon,
        RasteriseKind_NoTextureHighlight,
        RasteriseKind_UseTextureDecal,
        RasteriseKind_UseTextureModulate,
        RasteriseKind_UseTextureToon,
        RasteriseKind_UseTextureHighlight,
        RasteriseKind_ShadowMask,
        RasteriseKind_Count,
    };

    struct Variant
    {
        u32 Texture = 0;
        u32 WrapS = 0;
        u32 WrapT = 0;
        u32 CaptureReference = 0;
        s32 CaptureYOffset = 0;
        u8 CaptureType = 0;
        u8 BlendMode = 0;
        u16 Width = 0;
        u16 Height = 0;

        bool operator==(const Variant& other) const noexcept
        {
            return Texture == other.Texture
                && WrapS == other.WrapS
                && WrapT == other.WrapT
                && CaptureReference == other.CaptureReference
                && CaptureYOffset == other.CaptureYOffset
                && CaptureType == other.CaptureType
                && BlendMode == other.BlendMode;
        }
    };

    static u32 HashVariant(const Variant& variant) noexcept
    {
        u32 hash = 0x811C9DC5u;
        hash = MixVariantHash(hash, variant.Texture);
        hash = MixVariantHash(hash, variant.WrapS);
        hash = MixVariantHash(hash, variant.WrapT);
        hash = MixVariantHash(hash, variant.CaptureReference);
        hash = MixVariantHash(hash, static_cast<u32>(variant.CaptureYOffset));
        hash = MixVariantHash(hash, variant.CaptureType);
        return MixVariantHash(hash, variant.BlendMode);
    }

    // Mirrors the OpenGL compute renderer's MetaUniform. Field order and
    // padding match the HLSL cbuffer in GPU3D_DX12_shaders.h.
    struct MetaUniform
    {
        u32 NumPolygons;
        u32 NumVariants;
        u32 AlphaRef;
        u32 DispCnt;

        u32 ToonTable[4 * 34];

        u32 ClearColor;
        u32 ClearDepth;
        u32 ClearAttr;
        u32 FogOffset;

        u32 FogShift;
        u32 FogColor;
        float ClearBitmapOffset[2];
    };
    static_assert(sizeof(MetaUniform) % 16 == 0, "MetaUniform must stay 16-byte aligned");

    // Root constants, mirroring the HLSL DispatchUniform cbuffer.
    struct DispatchUniform
    {
        u32 CurVariant = 0;
        u32 TexWidth = 8;
        u32 TexHeight = 8;
        u32 TexWrapS = 0;
        u32 TexWrapT = 0;
        u32 InterpSpanBase = 0;
        u32 InterpSpanCount = 0;
        u32 Pad = 0;

        // Scale-dependent values are root constants because raster and
        // compositor command lists are recorded independently. Neither list
        // may rely on a shared, mutable upload-buffer CBV.
        u32 ScreenWidth = 0;
        u32 ScreenHeight = 0;
        u32 ScaleFactor = 0;
        u32 TilesPerLine = 0;

        u32 TileLines = 0;
        u32 FramebufferStride = 0;
        u32 ResultDepthStart = 0;
        u32 ResultAttrStart = 0;

        u32 BinningMaskStart = 0;
        u32 BinningWorkOffsetsStart = 0;
        u32 WorkDescsSortedStart = 0;
        u32 MaxWorkTiles = 0;
    };
    static constexpr u32 DispatchUniformDwords = sizeof(DispatchUniform) / 4;
    static_assert(DispatchUniformDwords <= 64, "DX12 root constants exceed the API limit");

    struct PolygonBatch
    {
        u32 FirstPolygon = 0;
        u32 PolygonCount = 0;
    };

    enum ShaderStep
    {
        ShaderStep_ClearCoarseBinMask = 0,
        ShaderStep_ClearIndirectWorkCount,
        ShaderStep_CalcOffsets,
        ShaderStep_SortWork,
        ShaderStep_BinCombined,
        ShaderStep_InterpSpans0,           // +0 Z-buffer, +1 W-buffer
        ShaderStep_DepthBlend0 = ShaderStep_InterpSpans0 + 2,
        ShaderStep_Rasterise0 = ShaderStep_DepthBlend0 + 2,
        ShaderStep_FinalPass0 = ShaderStep_Rasterise0 + RasteriseKind_Count * 2,
        ShaderStep_Resolve = ShaderStep_FinalPass0 + 8,
        ShaderStep_CaptureSidecar,
        ShaderStep_Compositor,
        ShaderStep_CorrectCoverage,
        ShaderStep_GPU2DNative,
        ShaderStep_GPU2DNativeCapture,
        ShaderStepCount,
    };

    bool CreateRootSignature();
    bool CreateCommandSignature();
    bool CreateFixedResources();
    bool CreateScaleDependentResources();
    bool BuildStaticSrvDescriptors();
    bool BuildFrameUavDescriptors();
    bool BuildCompositorUavDescriptors();
    void ReleaseScaleDependentResources();
    void ReleasePipelines();

    bool BuildPipeline(
        DX12::ComPtr<ID3D12PipelineState>& pipeline,
        int shaderVariant,
        const char* debugName);
    void SetRuntimeFailure(std::string reason);

    void UpdateClearBitmap();
    bool UploadMetaUniform(ID3D12GraphicsCommandList* list, u32 numVariants, u32 numPolygons);
    [[nodiscard]] DispatchUniform MakeDispatchUniform() const noexcept;
    void SetDispatchConstants(ID3D12GraphicsCommandList* list, const DispatchUniform& constants);
    void InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource);
    void InsertUavBarriers(
        ID3D12GraphicsCommandList* list,
        ID3D12Resource* const* resources,
        u32 count);
    void TransitionBuffer(
        ID3D12GraphicsCommandList* list,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after);

    // The UAV table never changes within a frame; the SRV table only changes
    // when the bound texture array does.
    bool BindFrameUavTable(ID3D12GraphicsCommandList* list);
    bool BindCompositionUavTable(
        ID3D12GraphicsCommandList* list,
        DX12DescriptorRing& descriptors,
        D3D12_CPU_DESCRIPTOR_HANDLE canonicalCpu);
    bool BindStaticSrvTable(ID3D12GraphicsCommandList* list);
    bool BindSrvTable(ID3D12GraphicsCommandList* list, ID3D12Resource* texture);
    void ResetFrameSrvCache() noexcept;

    // CPU-side span setup, ported verbatim from the OpenGL compute renderer.
    void SetupAttrs(SpanSetupY* span, Polygon* poly, int from, int to) const;
    void SetupYSpan(RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int from, int to, int side, s32 positions[10][2]) const;
    void SetupYSpanDummy(RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int vertex, int side, s32 positions[10][2]) const;

    // Returns the number of variants collected, filling YSpanSetups /
    // YSpanIndices / RenderPolygons. `numYSpans` / `numSetupIndices` /
    // `numPolygons` are outputs. Degenerate polygons are compacted out; valid
    // DS input otherwise fits the worst-case span allocations exactly.
    u32 BuildPolygons(int& numYSpans, int& numSetupIndices, u32& numPolygons);
    u32 BuildPolygonBatches(u32 numPolygons);

    bool RecordNativeResolveAndReadback();
    void EnsureFrameReadback();

    DX12Context* Context = nullptr;
    DX12CommandContext Commands;
    DX12CommandContext CaptureCommands;
    DX12UploadRing Uploads;
    DX12DescriptorRing Descriptors;
    // Descriptor lifetime classification:
    // A: fixed renderer resources use FrameUavDescriptors and StaticSrvDescriptors.
    // B: each compositor slot owns one canonical UAV block in CompositorUavDescriptors.
    // C: texture SRVs remain frame-local because the texture cache is dynamic.
    DX12DescriptorRing StaticSrvDescriptors;
    DX12DescriptorRing FrameUavDescriptors;
    DX12DescriptorRing CompositorUavDescriptors;
    DX12DescriptorRing NativeUavDescriptors;
    // One shader-visible table for the lazy Resolve submission. It is reset
    // only after CaptureCommands has retired its prior submission.
    DX12DescriptorRing CaptureDescriptors;
    DX12TextureHeap TextureHeap;

    TexcacheDX12 Texcache;

    DX12::ComPtr<ID3D12RootSignature> RootSignature;
    DX12::ComPtr<ID3D12CommandSignature> DispatchSignature;

    DX12::ComPtr<ID3D12PipelineState> PipelineClearCoarseBinMask;
    DX12::ComPtr<ID3D12PipelineState> PipelineClearIndirectWorkCount;
    DX12::ComPtr<ID3D12PipelineState> PipelineCalcOffsets;
    DX12::ComPtr<ID3D12PipelineState> PipelineSortWork;
    DX12::ComPtr<ID3D12PipelineState> PipelineBinCombined;
    std::array<DX12::ComPtr<ID3D12PipelineState>, 2> PipelineInterpSpans;
    std::array<DX12::ComPtr<ID3D12PipelineState>, 2> PipelineDepthBlend;
    std::array<DX12::ComPtr<ID3D12PipelineState>, RasteriseKind_Count * 2> PipelineRasterise;
    std::array<DX12::ComPtr<ID3D12PipelineState>, 8> PipelineFinalPass;
    DX12::ComPtr<ID3D12PipelineState> PipelineResolve;
    DX12::ComPtr<ID3D12PipelineState> PipelineCaptureSidecar;
    DX12::ComPtr<ID3D12PipelineState> PipelineCompositor;
    DX12::ComPtr<ID3D12PipelineState> PipelineCorrectCoverage;
    DX12::ComPtr<ID3D12PipelineState> PipelineGPU2DNative;
    DX12::ComPtr<ID3D12PipelineState> PipelineGPU2DNativeCapture;

    // GPU-side buffers.
    DX12::ComPtr<ID3D12Resource> ResultBuffer;      // color/depth/attr, 2 layers each
    DX12::ComPtr<ID3D12Resource> ResultWinnerBuffer; // winning polygon, 2 layers
    DX12::ComPtr<ID3D12Resource> FinalFBBuffer;     // packed r6g6b6a5 at internal res
    DX12::ComPtr<ID3D12Resource> CaptureSidecarBuffer;
    DX12::ComPtr<ID3D12Resource> ResolveBuffer;     // packed r6g6b6a5 at 256x192
    DX12::ComPtr<ID3D12Resource> ReadbackBuffer;
    DX12::ComPtr<ID3D12Resource> NativeCaptureReadback;
    DX12::ComPtr<ID3D12Resource> TileBuffers[3];    // color / depth / attr tiles
    DX12::ComPtr<ID3D12Resource> BinResultBuffer;
    DX12::ComPtr<ID3D12Resource> WorkDescBuffer;
    DX12::ComPtr<ID3D12Resource> BlendStateBuffer;
    DX12::ComPtr<ID3D12Resource> XSpanSetupBuffer;
    DX12::ComPtr<ID3D12Resource> YSpanSetupBuffer;
    DX12::ComPtr<ID3D12Resource> SetupIndicesBuffer;
    DX12::ComPtr<ID3D12Resource> RenderPolygonBuffer;
    DX12::ComPtr<ID3D12Resource> IndirectArgsBuffer;

    // Persistently mapped staging for the three per-frame CPU uploads.
    DX12::ComPtr<ID3D12Resource> YSpanSetupStaging;
    DX12::ComPtr<ID3D12Resource> SetupIndicesStaging;
    DX12::ComPtr<ID3D12Resource> RenderPolygonStaging;
    u8* YSpanSetupStagingPtr = nullptr;
    u8* SetupIndicesStagingPtr = nullptr;
    u8* RenderPolygonStagingPtr = nullptr;

    DX12::ComPtr<ID3D12Resource> ClearBitmapTex[2];
    DX12::ComPtr<ID3D12Resource> ClearBitmapUpload[2];
    u8* ClearBitmapUploadPtr[2] = { nullptr, nullptr };
    DX12::ComPtr<ID3D12Resource> MetaUniformUpload;
    u8* MetaUniformUploadPtr = nullptr;
    DX12::ComPtr<ID3D12Resource> DummyTexture;
    DX12::ComPtr<ID3D12Resource> DirectOutputDummy;

    std::unique_ptr<u32[]> ClearBitmap[2];
    u8 ClearBitmapDirty = 0x3;
    bool ClearBitmapTexInCopyDest[2] = { true, true };
    bool DummyTextureInitialized = false;

    // CPU-side scratch, mirroring the OpenGL compute renderer's members.
    std::array<Variant, MaxVariants> Variants{};
    static constexpr u32 VariantIndexCapacity = 4096;
    static_assert(VariantIndexCapacity > MaxVariants,
        "variant index must retain an empty probe terminator");
    AdaptiveVariantIndex<64, VariantIndexCapacity, 32> VariantLookup{};
    std::array<PolygonBatch, MaxRenderPolygons> PolygonBatches{};
    std::vector<SetupIndices> YSpanIndices;
    std::unique_ptr<SpanSetupY[]> YSpanSetups;
    std::unique_ptr<RenderPolygon[]> RenderPolygons;

    int TileSize = 8;
    int CoarseTileCountY = 4;
    int CoarseTileArea = 32;
    int CoarseTileW = 64;
    int CoarseTileH = 32;
    int ClearCoarseBinMaskLocalSize = 64;
    int TilesPerLine = 32;
    int TileLines = 24;
    int MaxWorkTiles = 0;
    int MaxYSpanIndices = 0;

    int ScaleFactor = -1;
    int ScreenWidth = 256;
    int ScreenHeight = 192;
    bool HiresCoordinates = false;

    int ShaderStepIdx = 0;
    bool RuntimeFailed = false;
    std::string RuntimeFailureReason;
    GPU2DComposeResult LastComposeResult = GPU2DComposeResult::Unavailable;

    // Cached for the frame so every dispatch does not re-create descriptors.
    D3D12_GPU_DESCRIPTOR_HANDLE FrameUavTable{};
    ID3D12Resource* BoundSrvTexture = nullptr;
    D3D12_GPU_DESCRIPTOR_HANDLE BoundSrvTable{};
    struct FrameSrvCacheEntry
    {
        ID3D12Resource* Texture = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE Table{};
        u32 Epoch = 0;
    };
    static constexpr u32 FrameSrvCacheCapacity = 4096;
    static_assert((FrameSrvCacheCapacity & (FrameSrvCacheCapacity - 1)) == 0,
        "SRV cache capacity must be a power of two");
    std::array<FrameSrvCacheEntry, FrameSrvCacheCapacity> FrameSrvTables{};
    u32 FrameSrvCacheEpoch = 1;
    D3D12_CPU_DESCRIPTOR_HANDLE StaticSrvCpu{};
    D3D12_CPU_DESCRIPTOR_HANDLE FrameUavCpu{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> CompositorUavCpu{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> NativeUavCpu{};
    DX12DescriptorRing SemanticCompositorUavDescriptors;
    DX12DescriptorRing SemanticNativeUavDescriptors;
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> SemanticCompositorUavCpu{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 3> SemanticNativeUavCpu{};

    bool FrameInFlight = false;
    bool FrameReadbackValid = false;
    bool NativeReadbackSubmitted = false;
    bool FinalFBHasValidFrame = false;
    u64 ComposedGeneration = 0;
    u64 PublishedOutputGeneration = 0;
    bool ComposedOutputValid = false;
    bool NativeCaptureStateInitialized = false;
    u64 CurrentEpoch = GPU2DNative::AllocateRendererEpoch();
    u64 LastSemanticFrame = 0;
    u64 LastSemanticCaptureGeneration = 0;
    u64 LastSemanticEpoch = 0;
    // Each semantic slot has a local DX12 fence.  It is not comparable
    // across slots, so capture provenance uses this renderer-global serial.
    u64 NativeSemanticSubmissionSerial = 0;
    u64 LastNativeCaptureCompletionValue = 0;
    GPU2DNative::HighResCaptureProvenanceTracker HighResCaptureProvenance;
    // Resource lifetime generation is owned by the renderer and advances only
    // when a new compositor resource set is created, so presenters can safely
    // cache descriptors by resource lifetime rather than content generation.
    u64 NextOutputResourceGeneration = 1;

    struct OutputState;
    std::shared_ptr<OutputState> ComposedOutput;

    alignas(64) std::array<u32, 256 * 192> ColorBuffer{};
    alignas(8) u32 ScrolledLine[256]{};
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_DX12_H
