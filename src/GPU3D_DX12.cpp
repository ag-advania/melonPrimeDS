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

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "GPU3D_DX12.h"

#include "StructuredUploadPlan.h"
#include "DX12GpuTimestamp.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <mutex>
#include <utility>

#include "DX12PresentedFrame.h"
#include "DX12Perf.h"
#include "MelonPrimeStructuredComposition.h"
#include "GPU.h"
#include "GPU3D_RasterEdge.h"
#include "RendererOutputRing.h"
#include "GPU3D_RasterDifferential.h"
#include "GPU3D_DX12_shaders.h"
#include "GPU3D_DX12_ShaderBlobs.inc"
#include "Platform.h"

namespace melonDS
{

namespace
{

constexpr u64 kUploadRingBytes = 32ull * 1024 * 1024;
constexpr u32 kDescriptorCount = 8192;
// The root-signature binding contract lives with the root signature itself.
// Aliased here because the descriptor rings below are sized against it, and
// a ring sized from a different constant than the signature declares is a
// silent GPU-side corruption.
constexpr u32 kStaticSrvCount = DX12RootSignatureLayout::StaticSrvCount;
constexpr u32 kTextureSrvCount = DX12RootSignatureLayout::TextureSrvCount;
constexpr u32 kUavTableSize = DX12RootSignatureLayout::UavTableSize;
constexpr u64 kNativeCaptureWords = (4ull * 128ull * 1024ull) / sizeof(u32);
constexpr u64 kNativeCaptureBytes = 4ull * 128ull * 1024ull;
// One packed Pixel word plus one metadata word for both logical screens.
// This scratch tail is consumed by the native OBJ raw pass and keeps the
// mosaic resolve from re-scanning OAM for every pixel in a mosaic span.
constexpr u32 kNativeObjRawWords = 3u * 256u * 192u * 2u;

// The compositor's own buffer layout, unqualified for the packing code below.
using namespace DX12Gpu2D;

// The generated blob table is a ~190k-line .inc that must stay in exactly one
// translation unit, so the pipeline repository is handed bytecode rather than
// learning which shaders exist.
DX12ShaderBytecode LookupShaderBytecode(u32 bucket, u32 variant) noexcept
{
    const DX12ShaderBlobs::Blob blob = DX12ShaderBlobs::Get(bucket, variant);
    return DX12ShaderBytecode{blob.Data, blob.Size};
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
bool RendererStartupProfileEnabled() noexcept
{
    static const bool enabled = [] {
        const char* value = std::getenv("MELONPRIME_RENDERER_STARTUP_PROFILE");
        return value && value[0] == '1' && value[1] == '\0';
    }();
    return enabled;
}

u64 RendererStartupNowNs() noexcept
{
    return static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void LogRendererStartupStage(
    const char* backend, const char* stage, u64 elapsedNs,
    const char* detail = "")
{
    if (!RendererStartupProfileEnabled())
        return;
    Platform::Log(Platform::LogLevel::Info,
        "[RendererStartup] backend=%s stage=%s elapsed_ms=%.3f %s\n",
        backend, stage, static_cast<double>(elapsedNs) / 1000000.0, detail);
}
#endif

constexpr u32 kRootParamDispatchConstants =
    DX12RootSignatureLayout::ParamDispatchConstants;
constexpr u32 kRootParamMetaCbv = DX12RootSignatureLayout::ParamMetaCbv;
constexpr u32 kRootParamStaticSrvTable =
    DX12RootSignatureLayout::ParamStaticSrvTable;
constexpr u32 kRootParamTextureSrvTable =
    DX12RootSignatureLayout::ParamTextureSrvTable;
constexpr u32 kRootParamUavTable = DX12RootSignatureLayout::ParamUavTable;

constexpr u32 kInterpSpansThreadsPerGroup = 32;
constexpr u32 kMaxDispatchGroupsPerDimension =
    D3D12_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION;
constexpr u32 kMaxInterpSpansPerDispatch =
    kInterpSpansThreadsPerGroup * kMaxDispatchGroupsPerDimension;
static_assert(kMaxInterpSpansPerDispatch == 2097120);

constexpr u32 DivRoundUp(u32 value, u32 divisor) noexcept
{
    return (value + divisor - 1) / divisor;
}

constexpr u64 AlignUp(u64 value, u64 alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}

struct UavDescriptorEntry
{
    ID3D12Resource* Resource = nullptr;
    u32 Elements = 0;
    u32 Stride = 0;
    bool Raw = false;
    bool Texture = false;
    DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
    u32 ArraySize = 1;
};

bool CreateUavDescriptorTable(
    ID3D12Device* device,
    u32 increment,
    D3D12_CPU_DESCRIPTOR_HANDLE destination,
    const UavDescriptorEntry* entries,
    u32 count)
{
    if (!device || !destination.ptr || !entries)
        return false;

    for (u32 i = 0; i < count; ++i)
    {
        if (!entries[i].Resource)
            return false;

        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        if (entries[i].Texture)
        {
            desc.Format = entries[i].Format;
            desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
            desc.Texture2DArray.MipSlice = 0;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize = entries[i].ArraySize;
            desc.Texture2DArray.PlaneSlice = 0;
        }
        else
        {
            desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = 0;
            desc.Buffer.CounterOffsetInBytes = 0;
            if (entries[i].Raw)
            {
                // RWByteAddressBuffer: a raw view is indexed in 32-bit words.
                desc.Format = DXGI_FORMAT_R32_TYPELESS;
                desc.Buffer.NumElements = entries[i].Elements;
                desc.Buffer.StructureByteStride = 0;
                desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            }
            else
            {
                desc.Format = DXGI_FORMAT_UNKNOWN;
                desc.Buffer.NumElements = entries[i].Elements;
                desc.Buffer.StructureByteStride = entries[i].Stride;
                desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
            }
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE handle{
            destination.ptr + static_cast<SIZE_T>(i) * increment };
        device->CreateUnorderedAccessView(entries[i].Resource, nullptr, &desc, handle);
    }

    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCreateCount, count);
    return true;
}

} // namespace

std::unique_ptr<DX12Renderer3D> DX12Renderer3D::New(melonDS::GPU3D& gpu3D)
{
    DX12Context& context = DX12Context::Get();
    if (!context.Acquire())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12: renderer creation failed: %s\n",
            context.GetFailureReason().empty() ? "device unavailable" : context.GetFailureReason().c_str());
        return nullptr;
    }

    std::unique_ptr<DX12Renderer3D> renderer(new DX12Renderer3D(gpu3D));
    renderer->Context = &context;
    return renderer;
}

DX12Renderer3D::DX12Renderer3D(melonDS::GPU3D& gpu3D)
    // TextureHeap is declared before Texcache, so its address is already valid
    // here; the loader only ever dereferences it after Init() populated it.
    : Renderer3D(gpu3D), Texcache(gpu3D.GPU, TexcacheDX12Loader(&TextureHeap))
{
    ClearBitmap[0] = std::make_unique<u32[]>(256 * 256);
    ClearBitmap[1] = std::make_unique<u32[]>(256 * 256);
    YSpanSetups = std::make_unique<SpanSetupY[]>(MaxYSpanSetups);
    RenderPolygons = std::make_unique<RenderPolygon[]>(2048);
}

DX12Renderer3D::~DX12Renderer3D()
{
    Stop();

    if (Context)
    {
        Context->Release();
        Context = nullptr;
    }
}

bool DX12Renderer3D::Init()
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    StartupBeginNs = RendererStartupNowNs();
    StartupFixedNs = 0;
    StartupScaleNs = 0;
    StartupOutputStateNs = 0;
    StartupPipelineNs = 0;
    StartupPipelineCacheHits = 0;
    StartupPipelineCacheMisses = 0;
#endif
    if (!Context || !Context->IsReady())
        return false;

    ID3D12Device* device = Context->GetDevice();

    for (RasterFrameSlot& frame : RasterFrames)
    {
        if (!frame.Commands.Init(device, Context->GetQueue())
            || !frame.Uploads.Init(Context->GetDevice(), kUploadRingBytes)
            || !frame.Descriptors.Init(device, kDescriptorCount, true))
            return false;
    }
    if (!DemandReadbackCommands.Init(device, Context->GetQueue()))
        return false;
    if (!StaticSrvDescriptors.Init(device, kStaticSrvCount, false))
        return false;
    if (!FrameUavDescriptors.Init(device, kUavTableSize, false))
        return false;
    if (!Gpu2D.CreateDescriptors(
            device, kUavTableSize, kCompositorFramesInFlight))
        return false;
    if (!DemandReadbackDescriptors.Init(device, kUavTableSize, true))
        return false;
    if (!PipelineRepo.CreateRootSignature(*Context, DispatchUniformDwords))
        return false;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const u64 pipelineLibraryStartNs = RendererStartupNowNs();
#endif
    PipelineRepo.CreatePipelineLibrary(
        *Context,
        &LookupShaderBytecode,
        DX12ShaderBlobs::BucketCount,
        DX12ShaderBlobs::VariantCount);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    LogRendererStartupStage("DX12", "pipeline_library",
        RendererStartupNowNs() - pipelineLibraryStartNs,
        PipelineRepo.WasLibraryLoadedFromCache() ? "cache=loaded" : "cache=empty");
#endif
    if (!PipelineRepo.CreateCommandSignature(*Context))
        return false;

    CurrentRasterFrameIndex = 0;
    NextRasterFrameIndex = 0;
    TextureHeap.Init(
        Context, &RasterFrames[0].Commands, &RasterFrames[0].Uploads);
    TextureHeap.SetFrameResources(
        &RasterFrames[0].Commands, &RasterFrames[0].Uploads, 0);

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const u64 fixedStartNs = RendererStartupNowNs();
#endif
    if (!CreateFixedResources())
        return false;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    StartupFixedNs = RendererStartupNowNs() - fixedStartNs;
    LogRendererStartupStage("DX12", "fixed_resources", StartupFixedNs);
#endif

    ClearBitmapDirty = 0x3;

    Platform::Log(
        Platform::LogLevel::Info,
        "DX12: 3D renderer initialized on \"%s\"\n",
        Context->GetDeviceProfile().AdapterName.c_str());
    return true;
}

void DX12Renderer3D::Stop()
{
    for (RasterFrameSlot& frame : RasterFrames)
        frame.Commands.WaitIdle();
    DemandReadbackCommands.WaitIdle();

    Texcache.Reset();
    TextureHeap.CollectGarbage();
    TextureHeap.Shutdown();

    // DX12Gpu2DOutput owns independent work/presentation command contexts. Retire
    // them before serializing the pipeline library so no cached PSO is still
    // referenced by an in-flight command list while the driver snapshots it.
    ReleaseScaleDependentResources();
    PipelineRepo.Save(*Context);
    ReleasePipelines();

    const D3D12_RANGE noWrite{ 0, 0 };
    for (RasterFrameSlot& frame : RasterFrames)
    {
        if (frame.RenderPolygonStaging && frame.RenderPolygonStagingPtr)
            frame.RenderPolygonStaging->Unmap(0, &noWrite);
        if (frame.YSpanSetupStaging && frame.YSpanSetupStagingPtr)
            frame.YSpanSetupStaging->Unmap(0, &noWrite);
        if (frame.MetaUniformUpload && frame.MetaUniformUploadPtr)
            frame.MetaUniformUpload->Unmap(0, &noWrite);
        for (u32 bitmap = 0; bitmap < 2; ++bitmap)
        {
            if (frame.ClearBitmapUpload[bitmap] && frame.ClearBitmapUploadPtr[bitmap])
                frame.ClearBitmapUpload[bitmap]->Unmap(0, &noWrite);
            frame.ClearBitmapUploadPtr[bitmap] = nullptr;
            frame.ClearBitmapUpload[bitmap].Reset();
        }
        frame.RenderPolygonStagingPtr = nullptr;
        frame.YSpanSetupStagingPtr = nullptr;
        frame.MetaUniformUploadPtr = nullptr;
        frame.RenderPolygonStaging.Reset();
        frame.YSpanSetupStaging.Reset();
        frame.SetupIndicesStaging.Reset();
        frame.MetaUniformUpload.Reset();
    }
    RenderPolygonBuffer.Reset();
    YSpanSetupBuffer.Reset();

    ReadbackBuffer.Reset();
    Capture.ReleaseReadback();
    ResolveBuffer.Reset();
    IndirectArgsBuffer.Reset();
    BinResultBuffer.Reset();
    ClearBitmapTex[0].Reset();
    ClearBitmapTex[1].Reset();
    DummyTexture.Reset();
    DirectOutputDummy.Reset();
    PipelineRepo.Reset();

    StaticSrvDescriptors.Shutdown();
    FrameUavDescriptors.Shutdown();
    Gpu2D.ShutdownDescriptors();
    DemandReadbackDescriptors.Shutdown();
    for (RasterFrameSlot& frame : RasterFrames)
    {
        frame.Descriptors.Shutdown();
        frame.Uploads.Shutdown();
        frame.Commands.Shutdown();
    }
    DemandReadbackCommands.Shutdown();

    FrameInFlight = false;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
    FinalFBHasValidFrame = false;
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;
    Provenance.ResetSemanticState();
}

void DX12Renderer3D::Reset()
{
    ResetInternal(false);
}

void DX12Renderer3D::ResetAfterSavestateLoad()
{
    ResetInternal(true);
    InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason::SavestateLoad);
}

void DX12Renderer3D::ResetInternal(bool preservePresentation)
{
    for (RasterFrameSlot& frame : RasterFrames)
        frame.Commands.WaitIdle();
    DemandReadbackCommands.WaitIdle();
    Texcache.Reset();
    TextureHeap.CollectGarbage();
    CurrentRasterFrameIndex = 0;
    NextRasterFrameIndex = 0;
    TextureHeap.SetFrameResources(
        &RasterFrames[0].Commands, &RasterFrames[0].Uploads, 0);
    ClearBitmapDirty = 0x3;
    FrameInFlight = false;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
    FinalFBHasValidFrame = false;
    Provenance.BeginNewEpoch();
    InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason::RendererReset);
    ColorBuffer.fill(0);
    bool keepPublishedOutput = false;
    int publishedSlot = -1;
    if (ComposedOutput)
    {
        const auto lock = ComposedOutput->Ring.LockPublication();
        publishedSlot = ComposedOutput->Ring.GetPublishedSlot();
        keepPublishedOutput = preservePresentation
            && ComposedOutputValid
            && publishedSlot >= 0
            && static_cast<std::size_t>(publishedSlot) < ComposedOutput->Slots.size();
        if (!keepPublishedOutput)
        {
            ComposedOutput->Ring.Unpublish();
            ComposedOutputValid = false;
            ComposedGeneration = 0;
            PublishedOutputGeneration = 0;
        }
        else
            ComposedOutputValid = true;
        for (std::size_t slotIndex = 0; slotIndex < ComposedOutput->Slots.size(); ++slotIndex)
        {
            if (keepPublishedOutput && static_cast<int>(slotIndex) == publishedSlot)
            {
                // Keep the last complete presentation surface alive. The
                // unpublished ring slots are rebuilt for the next full frame.
                continue;
            }
            DX12Gpu2DOutput::Slot& slot = ComposedOutput->Slots[slotIndex];
            slot.UploadedContentGeneration = {};
            slot.StructuredUploadInitialized = false;
            slot.Frame.DirectContentValid = false;
            slot.Frame.Epoch = Provenance.GetEpoch();
            slot.PresentationWorkSlot = -1;
        }
        for (DX12Gpu2DOutput::ComposeWorkSlot& slot : ComposedOutput->WorkSlots)
        {
            slot.UploadedNativeGeneration = {};
            slot.SemanticLines.Reset();
            slot.NativeUploadInitialized = false;
        }
    }
    if (!keepPublishedOutput && !ComposedOutput)
    {
        ComposedOutputValid = false;
        ComposedGeneration = 0;
        PublishedOutputGeneration = 0;
    }
}

void DX12Renderer3D::InvalidateHighResCaptureState(
    HighResCaptureInvalidationReason reason) noexcept
{
    (void)reason;
    HighResCaptureProvenance.Invalidate(
        Provenance.GetEpoch(),
        ScaleFactor > 0 ? static_cast<u32>(ScaleFactor) : 0u);
}

// ---------------------------------------------------------------------------
// Device objects
// ---------------------------------------------------------------------------

bool DX12Renderer3D::CreateFixedResources()
{
    constexpr u64 clearBitmapBytes = 256ull * 256ull * sizeof(u32);
    D3D12_RANGE noRead{ 0, 0 };
    for (int i = 0; i < 2; i++)
    {
        ClearBitmapTexInCopyDest[i] = true;
        ClearBitmapTex[i] = Context->CreateTexture2D(
            DXGI_FORMAT_R32_UINT,
            256,
            256,
            1,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            i == 0 ? L"MelonPrime DX12 clear bitmap color" : L"MelonPrime DX12 clear bitmap depth");
        if (!ClearBitmapTex[i])
            return false;

    }

    for (u32 frameIndex = 0; frameIndex < RasterFramesInFlight; ++frameIndex)
    {
        RasterFrameSlot& frame = RasterFrames[frameIndex];
        for (u32 bitmap = 0; bitmap < 2; ++bitmap)
        {
            frame.ClearBitmapUpload[bitmap] = Context->CreateBuffer(
                clearBitmapBytes,
                D3D12_HEAP_TYPE_UPLOAD,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_FLAG_NONE,
                bitmap == 0
                    ? L"MelonPrime DX12 per-frame clear bitmap color upload"
                    : L"MelonPrime DX12 per-frame clear bitmap depth upload");
            if (!frame.ClearBitmapUpload[bitmap]
                || FAILED(frame.ClearBitmapUpload[bitmap]->Map(
                    0, &noRead,
                    reinterpret_cast<void**>(&frame.ClearBitmapUploadPtr[bitmap])))
                || !frame.ClearBitmapUploadPtr[bitmap])
                return false;
        }

        frame.MetaUniformUpload = Context->CreateBuffer(
            AlignUp(sizeof(MetaUniform), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 per-frame uniform upload");
        if (!frame.MetaUniformUpload
            || FAILED(frame.MetaUniformUpload->Map(
                0, &noRead,
                reinterpret_cast<void**>(&frame.MetaUniformUploadPtr)))
            || !frame.MetaUniformUploadPtr)
            return false;
    }

    // Bound at t5 whenever a variant has no texture, so the descriptor table
    // never contains an undefined entry.
    DummyTexture = Context->CreateTexture2D(
        DXGI_FORMAT_R8G8B8A8_UINT,
        1, 1, 1,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        L"MelonPrime DX12 dummy texture");
    if (!DummyTexture)
        return false;

    // Binding u13 is present in every compute descriptor table because the
    // compositor shader has both the direct and fallback output declarations.
    // Unsupported devices bind this valid, never-used UAV instead of leaving
    // a descriptor undefined.
    DirectOutputDummy = Context->CreateTexture2D(
        DXGI_FORMAT_R8G8B8A8_UNORM,
        1, 1, 2,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        L"MelonPrime DX12 direct compositor dummy");
    if (!DirectOutputDummy)
        return false;

    ResolveBuffer = Context->CreateBuffer(
        256ull * 192ull * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 resolve output");
    if (!ResolveBuffer)
        return false;

    ReadbackBuffer = Context->CreateBuffer(
        256ull * 192ull * 4ull,
        D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 readback");
    if (!ReadbackBuffer)
        return false;

    if (!Capture.CreateReadback(*Context, kNativeCaptureBytes))
        return false;

    IndirectArgsBuffer = Context->CreateBuffer(
        sizeof(BinResultHeader),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 indirect arguments");
    if (!IndirectArgsBuffer)
        return false;

    RenderPolygonBuffer = Context->CreateBuffer(
        sizeof(RenderPolygon) * 2048ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 polygons");
    YSpanSetupBuffer = Context->CreateBuffer(
        sizeof(SpanSetupY) * static_cast<u64>(MaxYSpanSetups),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 y-spans");
    if (!RenderPolygonBuffer || !YSpanSetupBuffer)
        return false;

    for (u32 frameIndex = 0; frameIndex < RasterFramesInFlight; ++frameIndex)
    {
        RasterFrameSlot& frame = RasterFrames[frameIndex];
        frame.RenderPolygonStaging = Context->CreateBuffer(
            sizeof(RenderPolygon) * 2048ull,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 per-frame polygon staging");
        frame.YSpanSetupStaging = Context->CreateBuffer(
            sizeof(SpanSetupY) * static_cast<u64>(MaxYSpanSetups),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 per-frame y-span staging");
        if (!frame.RenderPolygonStaging || !frame.YSpanSetupStaging)
            return false;

        void* mapped = nullptr;
        if (FAILED(frame.RenderPolygonStaging->Map(0, &noRead, &mapped)))
            return false;
        frame.RenderPolygonStagingPtr = static_cast<u8*>(mapped);
        if (FAILED(frame.YSpanSetupStaging->Map(0, &noRead, &mapped)))
            return false;
        frame.YSpanSetupStagingPtr = static_cast<u8*>(mapped);
    }
    return true;
}

void DX12Renderer3D::ReleasePipelines()
{
    PipelineClearCoarseBinMask.Reset();
    PipelineClearIndirectWorkCount.Reset();
    PipelineCalcOffsets.Reset();
    PipelineSortWork.Reset();
    PipelineBinCombined.Reset();
    for (auto& pso : PipelineInterpSpans) pso.Reset();
    for (auto& pso : PipelineDepthBlend) pso.Reset();
    for (auto& pso : PipelineRasterise) pso.Reset();
    for (auto& pso : PipelineFinalPass) pso.Reset();
    PipelineResolve.Reset();
    PipelineCaptureSidecar.Reset();
    Gpu2D.ReleasePipelines();
}

void DX12Renderer3D::ReleaseScaleDependentResources()
{
    for (RasterFrameSlot& frame : RasterFrames)
    {
        if (frame.SetupIndicesStaging && frame.SetupIndicesStagingPtr)
        {
            D3D12_RANGE written{ 0, 0 };
            frame.SetupIndicesStaging->Unmap(0, &written);
            frame.SetupIndicesStagingPtr = nullptr;
        }
        frame.SetupIndicesStaging.Reset();
    }

    InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason::DeviceReset);
    ResultBuffer.Reset();
    ResultWinnerBuffer.Reset();
    FinalFBBuffer.Reset();
    Capture.ReleaseSidecar();
    ComposedOutput.reset();
    Provenance.ResetSemanticState();
    TileBuffers[0].Reset();
    TileBuffers[1].Reset();
    TileBuffers[2].Reset();
    WorkDescBuffer.Reset();
    BlendStateBuffer.Reset();
    XSpanSetupBuffer.Reset();
    SetupIndicesBuffer.Reset();
    // Sized from the tile counts, so it is scale-dependent too. Clearing it
    // here is what makes RenderFrame()'s null check catch a partially failed
    // reallocation instead of running against a stale buffer.
    BinResultBuffer.Reset();
    FrameUavDescriptors.Reset();
    Gpu2D.OutputUav.Reset();
    Gpu2D.WorkOutputUav.Reset();
    Gpu2D.WorkNativeUav.Reset();
    FrameUavCpu = {};
    Gpu2D.OutputUavCpu.fill({});
    Gpu2D.WorkOutputUavCpu.fill({});
    Gpu2D.WorkNativeUavCpu.fill({});
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    PublishedOutputGeneration = 0;
    FinalFBHasValidFrame = false;
}

bool DX12Renderer3D::CreateScaleDependentResources()
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const u64 scaleStartNs = RendererStartupNowNs();
#endif
    Provenance.BeginNewEpoch();
    InvalidateHighResCaptureState(
        HighResCaptureInvalidationReason::ScaleChange);
    ReleaseScaleDependentResources();

    // Query live DXGI budget only at the scale-resource boundary. The full
    // requested tile footprint is admitted before the historical halve/retry
    // loop, so a budget refusal is explicit while CreateCommittedResource
    // failures retain the existing tile retry behavior.
    if (!Context->RefreshMemoryAdmission())
        return false;
    const DX12::ScaleFootprint admissionFootprint = DX12::ComputeScaleFootprint(
        ScaleFactor,
        static_cast<u32>(TilesPerLine * TileLines * 16));
    if (!Context->AdmitScaleDependentResources(
            admissionFootprint, "DX12 scale-dependent resource recreation"))
        return false;

    const u64 pixels = static_cast<u64>(ScreenWidth) * static_cast<u64>(ScreenHeight);

    // color/depth/attr, two layers each, one 32-bit word per entry -- the same
    // layout the OpenGL compute renderer calls FinalTileMemory.
    ResultBuffer = Context->CreateBuffer(
        pixels * 3ull * 2ull * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 result buffer");
    if (!ResultBuffer)
        return false;

    const u64 resultWinnerBytes = ScaleFactor == 1 ? pixels * 2ull * 4ull : 4ull;
    ResultWinnerBuffer = Context->CreateBuffer(
        resultWinnerBytes,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 result winners");
    if (!ResultWinnerBuffer)
        return false;

    FinalFBBuffer = Context->CreateBuffer(
        pixels * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 3D framebuffer");
    if (!FinalFBBuffer)
        return false;

    if (!Capture.CreateSidecar(
            *Context,
            8ull * 256ull * 256ull * static_cast<u64>(ScaleFactor)
                * static_cast<u64>(ScaleFactor) * 4ull))
        return false;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const u64 outputStateStartNs = RendererStartupNowNs();
#endif
    ComposedOutput = std::make_shared<DX12Gpu2DOutput>();
    if (!ComposedOutput->Create(
            *Context, static_cast<u32>(ScreenWidth), static_cast<u32>(ScreenHeight),
            kUavTableSize, NextOutputResourceGeneration++, Provenance.GetEpoch()))
        return false;
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    StartupOutputStateNs = RendererStartupNowNs() - outputStateStartNs;
    LogRendererStartupStage("DX12", "gpu2d_output_state",
        StartupOutputStateNs, "native_work_slots=3 native_readbacks=0");
#endif

    // The tile heuristic (tiles * 16) is what the OpenGL renderer uses, but at
    // high internal resolutions the resulting allocation can exceed what a GPU
    // will hand out. Halve it until it fits. Correctness no longer depends on
    // this heuristic: BuildPolygonBatches partitions the frame against the
    // capacity actually allocated, without discarding polygon layers.
    int workTiles = TilesPerLine * TileLines * 16;
    const int minWorkTiles = TilesPerLine * TileLines;
    bool allocated = false;
    while (!allocated)
    {
        const u64 tileBytes = 4ull * static_cast<u64>(TileSize) * static_cast<u64>(TileSize)
            * static_cast<u64>(workTiles);

        allocated = true;
        for (int i = 0; i < 3; i++)
        {
            TileBuffers[i] = Context->CreateBuffer(
                tileBytes,
                D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                L"MelonPrime DX12 tile buffer");
            if (!TileBuffers[i])
            {
                allocated = false;
                break;
            }
        }

        if (allocated)
            break;

        TileBuffers[0].Reset();
        TileBuffers[1].Reset();
        TileBuffers[2].Reset();

        if (workTiles <= minWorkTiles)
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: could not allocate tile memory for %dx internal resolution\n",
                ScaleFactor);
            return false;
        }

        workTiles /= 2;
        Platform::Log(
            Platform::LogLevel::Warn,
            "DX12: tile memory allocation failed, retrying with %d work tiles\n",
            workTiles);
    }
    MaxWorkTiles = workTiles;

    WorkDescBuffer = Context->CreateBuffer(
        static_cast<u64>(MaxWorkTiles) * 2ull * 4ull * 2ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 work descriptors");
    if (!WorkDescBuffer)
        return false;

    BlendStateBuffer = Context->CreateBuffer(
        (static_cast<u64>(pixels) + kNativeCaptureWords + kNativeObjRawWords) * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 depth-blend continuation state");
    if (!BlendStateBuffer)
        return false;

    const u64 binResultBytes = sizeof(BinResultHeader)
        + static_cast<u64>(TilesPerLine) * TileLines * CoarseBinStride * 4ull
        + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull
        + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull;
    BinResultBuffer = Context->CreateBuffer(
        binResultBytes,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 bin results");
    if (!BinResultBuffer)
        return false;

    // Worst case: every valid DS polygon covers every output scanline. The
    // previous 64-lines-per-polygon heuristic silently truncated the frame.
    MaxYSpanIndices = ScreenHeight * MaxRenderPolygons;
    YSpanIndices.resize(MaxYSpanIndices);

    XSpanSetupBuffer = Context->CreateBuffer(
        sizeof(SpanSetupX) * static_cast<u64>(MaxYSpanIndices),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 x-spans");
    if (!XSpanSetupBuffer)
        return false;

    SetupIndicesBuffer = Context->CreateBuffer(
        sizeof(SetupIndices) * static_cast<u64>(MaxYSpanIndices),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 span indices");
    if (!SetupIndicesBuffer)
        return false;

    D3D12_RANGE noRead{ 0, 0 };
    for (u32 frameIndex = 0; frameIndex < RasterFramesInFlight; ++frameIndex)
    {
        RasterFrameSlot& frame = RasterFrames[frameIndex];
        frame.SetupIndicesStaging = Context->CreateBuffer(
            sizeof(SetupIndices) * static_cast<u64>(MaxYSpanIndices),
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 per-frame span index staging");
        if (!frame.SetupIndicesStaging)
            return false;

        void* mapped = nullptr;
        if (FAILED(frame.SetupIndicesStaging->Map(0, &noRead, &mapped)))
            return false;
        frame.SetupIndicesStagingPtr = static_cast<u8*>(mapped);
    }

    const bool descriptorsBuilt = BuildStaticSrvDescriptors()
        && BuildFrameUavDescriptors()
        && BuildCompositorUavDescriptors();
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    StartupScaleNs = RendererStartupNowNs() - scaleStartNs;
    LogRendererStartupStage("DX12", "scale_resources", StartupScaleNs);
#endif
    return descriptorsBuilt;
}

void DX12Renderer3D::SetRenderSettings(int scale, bool hiresCoordinates)
{
    if (scale == ScaleFactor)
    {
        // Like the OpenGL compute renderer, the high-resolution-coordinates
        // toggle must not tear down GPU resources: MelonPrimeDS applies it live
        // during a match.
        HiresCoordinates = hiresCoordinates;
        return;
    }

    for (RasterFrameSlot& frame : RasterFrames)
        frame.Commands.WaitIdle();
    DemandReadbackCommands.WaitIdle();

    const int previousTileSize = TileSize;
    const bool pipelinesReady = ShaderStepIdx >= ShaderStepCount;

    ScaleFactor = scale;
    ScreenWidth = 256 * ScaleFactor;
    ScreenHeight = 192 * ScaleFactor;
    HiresCoordinates = hiresCoordinates;

    // Same tile geometry derivation as the OpenGL compute renderer: the tile
    // size doubles at 5x and again at 9x.
    const int range = (ScaleFactor >= 5) + (ScaleFactor >= 9);
    TileSize = 8 << range;
    CoarseTileCountY = 4 + ((range >> 1) << 1);
    ClearCoarseBinMaskLocalSize = 64 - ((range >> 1) << 4);
    CoarseTileArea = CoarseTileCountX * CoarseTileCountY;
    CoarseTileW = CoarseTileCountX * TileSize;
    CoarseTileH = CoarseTileCountY * TileSize;

    const int tileShift = 3 + range;
    TilesPerLine = ScreenWidth >> tileShift;
    TileLines = ScreenHeight >> tileShift;

    // Screen dimensions are runtime constants. Pipelines only differ when the
    // numthreads tile geometry crosses 5x or 9x, so keep the complete PSO set
    // for scale changes within a bucket.
    if (!pipelinesReady || TileSize != previousTileSize)
    {
        ReleasePipelines();
        ShaderStepIdx = 0;
    }

    if (!CreateScaleDependentResources())
    {
        SetRuntimeFailure(
            "failed to allocate render targets for " + std::to_string(ScaleFactor)
            + "x internal resolution");
    }
    else
    {
        // The DS-size resolve remains available for capture. Presentation uses
        // the separate structured-2D compositor at the full target size.
        Platform::Log(
            Platform::LogLevel::Info,
            "DX12: internal resolution %dx -> 3D/composed output %dx%d, tiles %dx%d (%dpx), capture resolve 256x192\n",
            ScaleFactor,
            ScreenWidth,
            ScreenHeight,
            TilesPerLine,
            TileLines,
            TileSize);
    }

    FrameInFlight = false;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
}

// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

void DX12Renderer3D::SetRuntimeFailure(std::string reason)
{
    if (RuntimeFailed)
        return;

    RuntimeFailed = true;
    LastComposeResult = GPU2DComposeResult::Fatal;
    RuntimeFailureReason = reason.empty() ? "unspecified DX12 renderer failure" : std::move(reason);
    Platform::Log(
        Platform::LogLevel::Error,
        "DX12: runtime failure: %s\n",
        RuntimeFailureReason.c_str());
}

bool DX12Renderer3D::BuildPipeline(
    DX12::ComPtr<ID3D12PipelineState>& pipeline,
    int shaderVariant,
    const char* debugName)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    const u64 pipelineStartNs = RendererStartupNowNs();
#endif
    static_assert(ShaderStepCount == static_cast<int>(DX12ShaderBlobs::VariantCount));

    if (!Context)
    {
        pipeline.Reset();
        return false;
    }

    const u32 geometryBucket = TileSize == 8 ? 0u : (TileSize == 16 ? 1u : 2u);
    const DX12PipelineBuildResult result = PipelineRepo.BuildComputePipeline(
        *Context,
        pipeline,
        geometryBucket,
        static_cast<u32>(shaderVariant),
        LookupShaderBytecode(geometryBucket, static_cast<u32>(shaderVariant)));
    if (result == DX12PipelineBuildResult::Failed)
        return false;

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    // Startup profiling is the renderer's concern, not the repository's --
    // which is why BuildComputePipeline() reports where the pipeline came
    // from instead of counting it itself.
    const u64 elapsedNs = RendererStartupNowNs() - pipelineStartNs;
    StartupPipelineNs += elapsedNs;
    const char* cacheDetail = "miss";
    switch (result)
    {
    case DX12PipelineBuildResult::LibraryHit:
        ++StartupPipelineCacheHits;
        cacheDetail = "hit";
        break;
    case DX12PipelineBuildResult::CachedBlobHit:
        ++StartupPipelineCacheHits;
        cacheDetail = "blob_hit";
        break;
    case DX12PipelineBuildResult::Compiled:
    case DX12PipelineBuildResult::Failed:
        ++StartupPipelineCacheMisses;
        break;
    }
    char detail[96]{};
    std::snprintf(detail, sizeof(detail), "variant=%d bucket=%u cache=%s",
        shaderVariant, geometryBucket, cacheDetail);
    LogRendererStartupStage("DX12", "pipeline", elapsedNs, detail);
    if (shaderVariant == ShaderStepCount - 1 && RendererStartupProfileEnabled())
    {
        Platform::Log(Platform::LogLevel::Info,
            "[RendererStartupSummary] backend=DX12 total_ms=%.3f fixed_ms=%.3f "
            "scale_ms=%.3f output_state_ms=%.3f pipelines_ms=%.3f "
            "cache_hits=%u cache_misses=%u\n",
            static_cast<double>(RendererStartupNowNs() - StartupBeginNs) / 1000000.0,
            static_cast<double>(StartupFixedNs) / 1000000.0,
            static_cast<double>(StartupScaleNs) / 1000000.0,
            static_cast<double>(StartupOutputStateNs) / 1000000.0,
            static_cast<double>(StartupPipelineNs) / 1000000.0,
            StartupPipelineCacheHits, StartupPipelineCacheMisses);
    }
#endif
    (void)debugName;
    return true;
}

void DX12Renderer3D::ShaderCompileStep(int& current, int& count)
{
    current = ShaderStepIdx;
    count = ShaderStepCount;

    if (ShaderStepIdx >= ShaderStepCount)
        return;

    const int step = ShaderStepIdx++;
    char name[64];
    auto build = [this, step](
        DX12::ComPtr<ID3D12PipelineState>& pipeline,
        const std::string&,
        const std::vector<std::string>&,
        const char* debugName)
    {
        if (!BuildPipeline(pipeline, step, debugName))
            SetRuntimeFailure(std::string("pipeline creation failed: ") + debugName);
    };

    switch (step)
    {
    case ShaderStep_ClearCoarseBinMask:
        build(PipelineClearCoarseBinMask, DX12Shaders::ClearCoarseBinMask,
            { "ClearCoarseBinMask" }, "DX12ClearCoarseBinMask");
        return;
    case ShaderStep_ClearIndirectWorkCount:
        build(PipelineClearIndirectWorkCount, DX12Shaders::ClearIndirectWorkCount,
            { "ClearIndirectWorkCount" }, "DX12ClearIndirectWorkCount");
        return;
    case ShaderStep_CalcOffsets:
        build(PipelineCalcOffsets, DX12Shaders::CalcOffsets,
            { "CalculateWorkOffsets" }, "DX12CalcOffsets");
        return;
    case ShaderStep_SortWork:
        build(PipelineSortWork, DX12Shaders::SortWork,
            { "SortWork" }, "DX12SortWork");
        return;
    case ShaderStep_BinCombined:
        build(PipelineBinCombined, DX12Shaders::BinCombined,
            { "BinCombined" }, "DX12BinCombined");
        return;
    default:
        break;
    }

    if (step >= ShaderStep_InterpSpans0 && step < ShaderStep_DepthBlend0)
    {
        const int wbuffer = step - ShaderStep_InterpSpans0;
        std::snprintf(name, sizeof(name), "DX12InterpSpans%c", wbuffer ? 'W' : 'Z');
        build(PipelineInterpSpans[wbuffer], DX12Shaders::InterpSpans,
            { "InterpSpans", wbuffer ? "WBuffer" : "ZBuffer" }, name);
        return;
    }

    if (step >= ShaderStep_DepthBlend0 && step < ShaderStep_Rasterise0)
    {
        const int wbuffer = step - ShaderStep_DepthBlend0;
        std::snprintf(name, sizeof(name), "DX12DepthBlend%c", wbuffer ? 'W' : 'Z');
        build(PipelineDepthBlend[wbuffer], DX12Shaders::DepthBlend,
            { "DepthBlend", wbuffer ? "WBuffer" : "ZBuffer" }, name);
        return;
    }

    if (step >= ShaderStep_Rasterise0 && step < ShaderStep_FinalPass0)
    {
        const int index = step - ShaderStep_Rasterise0;
        const int kind = index >> 1;
        const int wbuffer = index & 1;

        std::vector<std::string> defines{ "Rasterise", wbuffer ? "WBuffer" : "ZBuffer" };
        switch (kind)
        {
        case RasteriseKind_NoTexture:
            defines.emplace_back("NoTexture");
            break;
        case RasteriseKind_NoTextureToon:
            defines.emplace_back("NoTexture");
            defines.emplace_back("Toon");
            break;
        case RasteriseKind_NoTextureHighlight:
            defines.emplace_back("NoTexture");
            defines.emplace_back("Highlight");
            break;
        case RasteriseKind_UseTextureDecal:
            defines.emplace_back("UseTexture");
            defines.emplace_back("Decal");
            break;
        case RasteriseKind_UseTextureModulate:
            defines.emplace_back("UseTexture");
            defines.emplace_back("Modulate");
            break;
        case RasteriseKind_UseTextureToon:
            defines.emplace_back("UseTexture");
            defines.emplace_back("Toon");
            break;
        case RasteriseKind_UseTextureHighlight:
            defines.emplace_back("UseTexture");
            defines.emplace_back("Highlight");
            break;
        case RasteriseKind_ShadowMask:
            defines.emplace_back("ShadowMask");
            break;
        default:
            break;
        }

        std::snprintf(name, sizeof(name), "DX12Rasterise%d%c", kind, wbuffer ? 'W' : 'Z');
        build(PipelineRasterise[index], DX12Shaders::Rasterise, defines, name);
        return;
    }

    if (step >= ShaderStep_FinalPass0 && step < ShaderStep_Resolve)
    {
        const int variant = step - ShaderStep_FinalPass0;
        std::vector<std::string> defines{ "FinalPass" };
        if (variant & 0x1) defines.emplace_back("EdgeMarking");
        if (variant & 0x2) defines.emplace_back("Fog");
        if (variant & 0x4) defines.emplace_back("AntiAliasing");

        std::snprintf(name, sizeof(name), "DX12FinalPass%d", variant);
        build(PipelineFinalPass[variant], DX12Shaders::FinalPass, defines, name);
        return;
    }

    if (step == ShaderStep_Resolve)
    {
        build(PipelineResolve, DX12Shaders::Resolve, { "Resolve" }, "DX12Resolve");
        return;
    }

    if (step == ShaderStep_CaptureSidecar)
    {
        build(PipelineCaptureSidecar, DX12Shaders::CaptureSidecar,
            { "CaptureSidecar" }, "DX12CaptureSidecar");
        return;
    }

    if (step == ShaderStep_Compositor)
    {
        build(Gpu2D.Compositor, DX12Shaders::Compositor,
            { "Compositor" }, "DX12Compositor");
        return;
    }

    if (step == ShaderStep_CorrectCoverage)
    {
        build(Gpu2D.CorrectCoverage, DX12Shaders::CorrectCoverage,
            { "CorrectCoverage" }, "DX12CorrectCoverage");
        return;
    }

    if (step == ShaderStep_GPU2DNative)
    {
        build(Gpu2D.Native, DX12Shaders::GPU2DNative,
            { "GPU2DNative" }, "DX12GPU2DNative");
        return;
    }

    if (step == ShaderStep_GPU2DNativeCapture)
    {
        build(Gpu2D.NativeCapture, DX12Shaders::GPU2DNative,
            { "GPU2DNative", "GPU2DNativeCapture" }, "DX12GPU2DNativeCapture");
        return;
    }
}

// ---------------------------------------------------------------------------
// Per-frame helpers
// ---------------------------------------------------------------------------

void DX12Renderer3D::UpdateClearBitmap()
{
    if (!(GPU3D.RenderDispCnt & (1 << 14)))
        return;

    RasterFrameSlot& frame = ActiveRasterFrame();
    ID3D12GraphicsCommandList* list = frame.Commands.GetList();
    if (!list)
        return;

    for (int slot = 0; slot < 2; slot++)
    {
        if (!(ClearBitmapDirty & (1 << slot)))
            continue;

        if (slot == 0)
        {
            const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x40000]);
            for (int i = 0; i < 256 * 256; i++)
            {
                const u16 color = vram[i];
                u32 r = (color << 1) & 0x3E; if (r) r++;
                u32 g = (color >> 4) & 0x3E; if (g) g++;
                u32 b = (color >> 9) & 0x3E; if (b) b++;
                const u32 a = (color & 0x8000) ? 31 : 0;

                ClearBitmap[0][i] = r | (g << 8) | (b << 16) | (a << 24);
            }
        }
        else
        {
            const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x60000]);
            for (int i = 0; i < 256 * 256; i++)
            {
                const u16 val = vram[i];
                const u32 depth = ((val & 0x7FFF) * 0x200) + 0x1FF;
                const u32 fog = static_cast<u32>(val & 0x8000) << 9;

                ClearBitmap[1][i] = depth | fog;
            }
        }

        constexpr u64 rowPitch = 256ull * 4ull; // already 256-byte aligned
        constexpr u64 totalBytes = rowPitch * 256ull;

        if (!frame.ClearBitmapUpload[slot] || !frame.ClearBitmapUploadPtr[slot])
            continue;

        std::memcpy(
            frame.ClearBitmapUploadPtr[slot], ClearBitmap[slot].get(),
            static_cast<size_t>(totalBytes));

        if (!ClearBitmapTexInCopyDest[slot])
        {
            TransitionBuffer(list, ClearBitmapTex[slot].Get(),
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        }

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = ClearBitmapTex[slot].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = frame.ClearBitmapUpload[slot].Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = 0;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
        src.PlacedFootprint.Footprint.Width = 256;
        src.PlacedFootprint.Footprint.Height = 256;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        TransitionBuffer(list, ClearBitmapTex[slot].Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        ClearBitmapTexInCopyDest[slot] = false;
        ClearBitmapDirty &= static_cast<u8>(~(1u << slot));
    }
}

bool DX12Renderer3D::UploadMetaUniform(ID3D12GraphicsCommandList* list, u32 numVariants, u32 numPolygons)
{
    MetaUniform meta{};
    meta.DispCnt = GPU3D.RenderDispCnt;
    meta.NumPolygons = numPolygons;
    meta.NumVariants = numVariants;
    meta.AlphaRef = GPU3D.RenderAlphaRef;

    {
        u32 r = (GPU3D.RenderClearAttr1 << 1) & 0x3E; if (r) r++;
        u32 g = (GPU3D.RenderClearAttr1 >> 4) & 0x3E; if (g) g++;
        u32 b = (GPU3D.RenderClearAttr1 >> 9) & 0x3E; if (b) b++;
        const u32 a = (GPU3D.RenderClearAttr1 >> 16) & 0x1F;

        meta.ClearColor = r | (g << 8) | (b << 16) | (a << 24);
        meta.ClearDepth = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
        meta.ClearAttr = GPU3D.RenderClearAttr1 & 0x3F008000;

        const u8 xoff = (GPU3D.RenderClearAttr2 >> 16) & 0xFF;
        const u8 yoff = (GPU3D.RenderClearAttr2 >> 24) & 0xFF;
        meta.ClearBitmapOffset[0] = static_cast<float>(xoff) / 256.0f;
        meta.ClearBitmapOffset[1] = static_cast<float>(yoff) / 256.0f;
    }

    for (u32 i = 0; i < 32; i++)
    {
        const u32 color = GPU3D.RenderToonTable[i];
        u32 r = (color << 1) & 0x3E; if (r) r++;
        u32 g = (color >> 4) & 0x3E; if (g) g++;
        u32 b = (color >> 9) & 0x3E; if (b) b++;

        meta.ToonTable[i * 4 + 0] = r | (g << 8) | (b << 16);
    }
    for (u32 i = 0; i < 34; i++)
        meta.ToonTable[i * 4 + 1] = GPU3D.RenderFogDensityTable[i];
    for (u32 i = 0; i < 8; i++)
    {
        const u32 color = GPU3D.RenderEdgeTable[i];
        u32 r = (color << 1) & 0x3E; if (r) r++;
        u32 g = (color >> 4) & 0x3E; if (g) g++;
        u32 b = (color >> 9) & 0x3E; if (b) b++;

        meta.ToonTable[i * 4 + 2] = r | (g << 8) | (b << 16);
    }

    meta.FogOffset = GPU3D.RenderFogOffset;
    meta.FogShift = GPU3D.RenderFogShift;
    {
        u32 fogR = (GPU3D.RenderFogColor << 1) & 0x3E; if (fogR) fogR++;
        u32 fogG = (GPU3D.RenderFogColor >> 4) & 0x3E; if (fogG) fogG++;
        u32 fogB = (GPU3D.RenderFogColor >> 9) & 0x3E; if (fogB) fogB++;
        const u32 fogA = (GPU3D.RenderFogColor >> 16) & 0x1F;
        meta.FogColor = fogR | (fogG << 8) | (fogB << 16) | (fogA << 24);
    }

    RasterFrameSlot& frame = ActiveRasterFrame();
    if (!frame.MetaUniformUpload || !frame.MetaUniformUploadPtr)
        return false;

    std::memcpy(frame.MetaUniformUploadPtr, &meta, sizeof(meta));
    list->SetComputeRootConstantBufferView(
        kRootParamMetaCbv, frame.MetaUniformUpload->GetGPUVirtualAddress());
    return true;
}

DX12Renderer3D::DispatchUniform DX12Renderer3D::MakeDispatchUniform() const noexcept
{
    DispatchUniform constants{};
    const u32 framebufferStride = static_cast<u32>(ScreenWidth * ScreenHeight);
    const u32 tileCount = static_cast<u32>(TilesPerLine * TileLines);
    constants.ScreenWidth = static_cast<u32>(ScreenWidth);
    constants.ScreenHeight = static_cast<u32>(ScreenHeight);
    constants.ScaleFactor = static_cast<u32>(ScaleFactor);
    constants.TilesPerLine = static_cast<u32>(TilesPerLine);
    constants.TileLines = static_cast<u32>(TileLines);
    constants.FramebufferStride = framebufferStride;
    constants.ResultDepthStart = framebufferStride * 2u;
    constants.ResultAttrStart = framebufferStride * 4u;
    constants.BinningMaskStart = tileCount * static_cast<u32>(CoarseBinStride);
    constants.BinningWorkOffsetsStart = constants.BinningMaskStart
        + tileCount * static_cast<u32>(BinStride);
    constants.WorkDescsSortedStart = static_cast<u32>(MaxWorkTiles);
    constants.MaxWorkTiles = static_cast<u32>(MaxWorkTiles);
    return constants;
}

void DX12Renderer3D::SetDispatchConstants(ID3D12GraphicsCommandList* list, const DispatchUniform& constants)
{
    list->SetComputeRoot32BitConstants(kRootParamDispatchConstants, DispatchUniformDwords, &constants, 0);
}

void DX12Renderer3D::InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

void DX12Renderer3D::InsertUavBarriers(
    ID3D12GraphicsCommandList* list,
    ID3D12Resource* const* resources,
    u32 count)
{
    if (!resources || count == 0u)
        return;
    D3D12_RESOURCE_BARRIER barriers[4]{};
    count = std::min<u32>(count, 4u);
    for (u32 index = 0u; index < count; ++index)
    {
        barriers[index].Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[index].Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barriers[index].UAV.pResource = resources[index];
    }
    list->ResourceBarrier(count, barriers);
}

void DX12Renderer3D::InsertRasterScratchReuseBarriers(
    ID3D12GraphicsCommandList* list)
{
    // The two command allocators decouple CPU recording from GPU completion,
    // while these large UAVs intentionally remain single-copy. Direct-queue
    // order serializes frame N before frame N+1; resource-scoped UAV barriers
    // make the cross-list visibility dependency explicit without a CPU fence.
    ID3D12Resource* group0[] = {
        TileBuffers[0].Get(), TileBuffers[1].Get(),
        TileBuffers[2].Get(), ResultBuffer.Get(),
    };
    InsertUavBarriers(list, group0, static_cast<u32>(std::size(group0)));

    ID3D12Resource* group1[] = {
        ResultWinnerBuffer.Get(), BinResultBuffer.Get(),
        WorkDescBuffer.Get(), BlendStateBuffer.Get(),
    };
    InsertUavBarriers(list, group1, static_cast<u32>(std::size(group1)));

    ID3D12Resource* group2[] = {
        XSpanSetupBuffer.Get(), IndirectArgsBuffer.Get(),
        FinalFBBuffer.Get(), Capture.GetSidecarBuffer(),
    };
    InsertUavBarriers(list, group2, static_cast<u32>(std::size(group2)));
}

void DX12Renderer3D::TransitionBuffer(
    ID3D12GraphicsCommandList* list,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    list->ResourceBarrier(1, &barrier);
}

bool DX12Renderer3D::BindFrameUavTable(ID3D12GraphicsCommandList* list)
{
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!ActiveRasterFrame().Descriptors.Allocate(kUavTableSize, cpu, gpu))
        return false;

    if (!FrameUavCpu.ptr)
        return false;
    Context->GetDevice()->CopyDescriptorsSimple(
        kUavTableSize,
        cpu,
        FrameUavCpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    FrameUavTable = gpu;
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCopyCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kUavTableSize);
    list->SetComputeRootDescriptorTable(kRootParamUavTable, gpu);
    return true;
}

bool DX12Renderer3D::BindCompositionUavTable(
    ID3D12GraphicsCommandList* list,
    DX12DescriptorRing& descriptors,
    D3D12_CPU_DESCRIPTOR_HANDLE canonicalCpu)
{
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!descriptors.Allocate(kUavTableSize, cpu, gpu))
        return false;

    if (!canonicalCpu.ptr)
        return false;
    Context->GetDevice()->CopyDescriptorsSimple(
        kUavTableSize,
        cpu,
        canonicalCpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCopyCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::CompositorDescriptorUpdateCount, kUavTableSize);
    list->SetComputeRootDescriptorTable(kRootParamUavTable, gpu);
    return true;
}

bool DX12Renderer3D::BindStaticSrvTable(ID3D12GraphicsCommandList* list)
{
    static_assert(
        kStaticSrvCount + (MaxVariants + 1) * kTextureSrvCount + kUavTableSize <=
            kDescriptorCount,
        "the shader-visible heap must cover every texture variant plus dummy binding");
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!ActiveRasterFrame().Descriptors.Allocate(kStaticSrvCount, cpu, gpu))
        return false;

    Context->GetDevice()->CopyDescriptorsSimple(
        kStaticSrvCount,
        cpu,
        StaticSrvCpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCopyCount, kStaticSrvCount);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kStaticSrvCount);
    list->SetComputeRootDescriptorTable(kRootParamStaticSrvTable, gpu);
    return true;
}

bool DX12Renderer3D::WaitForQueueIdle()
{
    // The queue is shared, so placing the idle fence through the last raster
    // context covers every earlier raster/compositor/presenter submission.
    return ActiveRasterFrame().Commands.WaitQueueIdle();
}

bool DX12Renderer3D::BindSrvTable(ID3D12GraphicsCommandList* list, u32 textureHandle)
{
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    const DX12TextureHeap::Entry* textureEntry =
        TextureHeap.Lookup(textureHandle);
    ID3D12Resource* texture = textureEntry
        ? textureEntry->Resource.Get() : DummyTexture.Get();
    if (!texture)
        return false;

    if (textureHandle < PersistentTextureDescriptorCount)
    {
        const u64 key = textureEntry
            ? (static_cast<u64>(textureEntry->Generation) << 32u)
                | textureHandle
            : 1u;
        u64& cachedKey =
            PersistentTextureDescriptorKeys[CurrentRasterFrameIndex][textureHandle];
        D3D12_CPU_DESCRIPTOR_HANDLE cpu =
            ActiveRasterFrame().Descriptors.GetCpu(textureHandle);
        D3D12_GPU_DESCRIPTOR_HANDLE gpu =
            ActiveRasterFrame().Descriptors.GetGpu(textureHandle);
        if (!cpu.ptr || !gpu.ptr)
            return false;
        if (cachedKey != key)
        {
            D3D12_RESOURCE_DESC texDesc = texture->GetDesc();
            D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
            desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            desc.Texture2DArray.MostDetailedMip = 0;
            desc.Texture2DArray.MipLevels = 1;
            desc.Texture2DArray.FirstArraySlice = 0;
            desc.Texture2DArray.ArraySize = texDesc.DepthOrArraySize;
            desc.Texture2DArray.PlaneSlice = 0;
            desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
            Context->GetDevice()->CreateShaderResourceView(texture, &desc, cpu);
            cachedKey = key;
            DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCreateCount);
            DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::PersistentDescriptorCreateCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::PersistentDescriptorMissCount);
        }
        else
        {
            DX12Perf::AddCounter(
                DX12Perf::Counter::PersistentDescriptorHitCount);
        }
        BoundSrvTexture = texture;
        BoundSrvTable = gpu;
        list->SetComputeRootDescriptorTable(kRootParamTextureSrvTable, gpu);
        return true;
    }

    if (texture == BoundSrvTexture && BoundSrvTable.ptr != 0)
    {
        list->SetComputeRootDescriptorTable(kRootParamTextureSrvTable, BoundSrvTable);
        return true;
    }

    constexpr u32 cacheMask = FrameSrvCacheCapacity - 1;
    const auto pointerBits = reinterpret_cast<std::uintptr_t>(texture);
    u32 cacheIndex = static_cast<u32>((pointerBits >> 4u) & cacheMask);
    FrameSrvCacheEntry* insertion = nullptr;
    for (u32 probe = 0; probe < FrameSrvCacheCapacity; ++probe)
    {
        FrameSrvCacheEntry& entry = FrameSrvTables[cacheIndex];
        if (entry.Epoch != FrameSrvCacheEpoch)
        {
            insertion = &entry;
            break;
        }
        if (entry.Texture == texture)
        {
            BoundSrvTexture = texture;
            BoundSrvTable = entry.Table;
            list->SetComputeRootDescriptorTable(kRootParamTextureSrvTable, BoundSrvTable);
            return true;
        }
        cacheIndex = (cacheIndex + 1u) & cacheMask;
    }

    if (!insertion)
        return false;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!ActiveRasterFrame().Descriptors.Allocate(kTextureSrvCount, cpu, gpu))
        return false;

    ID3D12Device* device = Context->GetDevice();

    // t5: decoded texture array for the current variant
    {
        D3D12_RESOURCE_DESC texDesc = texture->GetDesc();

        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R8G8B8A8_UINT;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2DArray.MostDetailedMip = 0;
        desc.Texture2DArray.MipLevels = 1;
        desc.Texture2DArray.FirstArraySlice = 0;
        desc.Texture2DArray.ArraySize = texDesc.DepthOrArraySize;
        desc.Texture2DArray.PlaneSlice = 0;
        desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
        device->CreateShaderResourceView(texture, &desc, cpu);
    }

    BoundSrvTexture = texture;
    BoundSrvTable = gpu;
    *insertion = { texture, gpu, FrameSrvCacheEpoch };
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCreateCount, kTextureSrvCount);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kTextureSrvCount);
    list->SetComputeRootDescriptorTable(kRootParamTextureSrvTable, gpu);
    return true;
}

void DX12Renderer3D::ResetFrameSrvCache() noexcept
{
    FrameSrvCacheEpoch++;
    if (FrameSrvCacheEpoch != 0)
        return;

    for (FrameSrvCacheEntry& entry : FrameSrvTables)
        entry.Epoch = 0;
    FrameSrvCacheEpoch = 1;
}

// ---------------------------------------------------------------------------
// CPU-side span setup (ported from the OpenGL compute renderer)
// ---------------------------------------------------------------------------

void DX12Renderer3D::SetupAttrs(SpanSetupY* span, Polygon* poly, int from, int to) const
{
    span->Z0 = poly->FinalZ[from];
    span->W0 = poly->FinalW[from];
    span->Z1 = poly->FinalZ[to];
    span->W1 = poly->FinalW[to];
    span->ColorR0 = poly->Vertices[from]->FinalColor[0];
    span->ColorG0 = poly->Vertices[from]->FinalColor[1];
    span->ColorB0 = poly->Vertices[from]->FinalColor[2];
    span->ColorR1 = poly->Vertices[to]->FinalColor[0];
    span->ColorG1 = poly->Vertices[to]->FinalColor[1];
    span->ColorB1 = poly->Vertices[to]->FinalColor[2];
    span->TexcoordU0 = poly->Vertices[from]->TexCoords[0];
    span->TexcoordV0 = poly->Vertices[from]->TexCoords[1];
    span->TexcoordU1 = poly->Vertices[to]->TexCoords[0];
    span->TexcoordV1 = poly->Vertices[to]->TexCoords[1];
}

void DX12Renderer3D::SetupYSpanDummy(
    RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int vertex, int side, s32 positions[10][2]) const
{
    const s32 x0 = positions[vertex][0];
    span->DxInitial = 0;

    span->X0 = span->X1 = x0;
    span->XMin = x0;
    span->XMax = x0;
    span->Y0 = span->Y1 = positions[vertex][1];

    const s32 boundsXMin = RasterEdge::ConservativeRightVerticalMin(x0, side != 0);
    if (boundsXMin < rp->XMin)
    {
        rp->XMin = boundsXMin;
        rp->XMinY = span->Y0;
    }
    if (span->XMax > rp->XMax)
    {
        rp->XMax = span->XMax;
        rp->XMaxY = span->Y0;
    }

    span->Increment = 0;

    span->I0 = span->I1 = span->IRecip = 0;
    span->Linear = 1;

    span->XCovIncr = 0;

    span->IsDummy = 1;

    SetupAttrs(span, poly, vertex, vertex);
}

void DX12Renderer3D::SetupYSpan(
    RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int from, int to, int side, s32 positions[10][2]) const
{
    span->X0 = positions[from][0];
    span->X1 = positions[to][0];
    span->Y0 = positions[from][1];
    span->Y1 = positions[to][1];

    SetupAttrs(span, poly, from, to);

    s32 minXY, maxXY;
    bool negative = false;
    if (span->X1 > span->X0)
    {
        span->XMin = span->X0;
        span->XMax = span->X1 - 1;

        minXY = span->Y0;
        maxXY = span->Y1;
    }
    else if (span->X1 < span->X0)
    {
        span->XMin = span->X1;
        span->XMax = span->X0 - 1;
        negative = true;

        minXY = span->Y1;
        maxXY = span->Y0;
    }
    else
    {
        span->XMin = span->X0;
        span->XMax = span->XMin;

        minXY = span->Y0;
        maxXY = span->Y0;
    }

    const s32 boundsXMin = RasterEdge::ConservativeRightVerticalMin(
        span->XMin, side && span->X0 == span->X1);
    if (boundsXMin < rp->XMin)
    {
        rp->XMin = boundsXMin;
        rp->XMinY = minXY;
    }
    if (span->XMax > rp->XMax)
    {
        rp->XMax = span->XMax;
        rp->XMaxY = maxXY;
    }

    span->IsDummy = 0;

    const s32 xlen = span->XMax + 1 - span->XMin;
    const s32 ylen = span->Y1 - span->Y0;
    span->Increment = RasterEdge::CalculateSlopeIncrement(
        span->X0, span->X1, span->XMin, span->XMax, span->Y0, span->Y1);

    const bool xMajor = (span->Increment > 0x40000);

    if (side)
    {
        if (xMajor)
            span->DxInitial = negative ? (0x20000 + 0x40000) : (span->Increment - 0x20000);
        else if (span->Increment != 0)
            span->DxInitial = negative ? 0x40000 : 0;
        else
            span->DxInitial = 0;
    }
    else
    {
        if (xMajor)
            span->DxInitial = negative ? ((span->Increment - 0x20000) + 0x40000) : 0x20000;
        else if (span->Increment != 0)
            span->DxInitial = negative ? 0x40000 : 0;
        else
            span->DxInitial = 0;
    }

    if (xMajor)
    {
        // used for calculating AA coverage
        span->XCovIncr = (ylen << 10) / xlen;
    }

    const s32 interpolationOffset = RasterEdge::InterpolationOriginOffset(
        span->Increment, side != 0, negative);
    span->I0 = span->Y0 - interpolationOffset;
    span->I1 = span->Y1 - interpolationOffset;

    if (span->I0 != span->I1)
        span->IRecip = (1 << 30) / (span->I1 - span->I0);
    else
        span->IRecip = 0;

    span->Linear = ((span->W0 == span->W1) && !(span->W0 & 0x7E) && !(span->W1 & 0x7E)) ? 1u : 0u;

    if ((span->W0 & 0x1) && !(span->W1 & 0x1))
    {
        span->W0n = (span->W0 - 1) >> 1;
        span->W0d = (span->W0 + 1) >> 1;
        span->W1d = span->W1 >> 1;
    }
    else
    {
        span->W0n = span->W0 >> 1;
        span->W0d = span->W0 >> 1;
        span->W1d = span->W1 >> 1;
    }
}

u32 DX12Renderer3D::BuildPolygons(int& numYSpans, int& numSetupIndices, u32& numPolygons)
{
    numYSpans = 0;
    numSetupIndices = 0;
    numPolygons = 0;

    // Games spam small textures, so same-sized textures share an array texture
    // and polygons that agree on texture + blend mode + wrap mode share a
    // rasterise dispatch. Fewer variants means bigger batches.
    u32 numVariants = 0;
    u32 prevVariant = 0;
    u32 prevTexLayer = 0;
    Variant* variants = Variants.data();
    VariantLookup.Reset();
    u32 captureLastVariant[16]{};

    int captureInfo[16];
    GPU.GetCaptureInfo_Texture(captureInfo);

    const bool enableTextureMaps = (GPU3D.RenderDispCnt & (1 << 0)) != 0;
    Polygon* previousPolygon = nullptr;

    for (u32 sourceIndex = 0; sourceIndex < GPU3D.RenderNumPolygons; sourceIndex++)
    {
        Polygon* polygon = GPU3D.RenderPolygonRAM[sourceIndex];
        if (polygon->Degenerate)
            continue;

        // Match Software's early degenerate rejection while preserving the
        // dense indices consumed by setup, binning and depth/blend stages.
        const u32 i = numPolygons;

        const u32 nverts = polygon->NumVertices;
        u32 vtop = polygon->VTop, vbot = polygon->VBottom;

        u32 curVL = vtop, curVR = vtop;
        u32 nextVL, nextVR;

        RenderPolygons[i].FirstXSpan = numSetupIndices;
        RenderPolygons[i].Attr = polygon->Attr;
        RenderPolygons[i].FacingView = polygon->FacingView ? 1u : 0u;

        bool foundVariant = false;
        if (previousPolygon)
        {
            // If the whole texture attribute matches, the texture layer will
            // also match.
            foundVariant = previousPolygon->TexParam == polygon->TexParam
                && previousPolygon->TexPalette == polygon->TexPalette
                && (previousPolygon->Attr & 0x30) == (polygon->Attr & 0x30)
                && previousPolygon->IsShadowMask == polygon->IsShadowMask;
        }

        if (!foundVariant)
        {
            Variant variant;
            variant.BlendMode = polygon->IsShadowMask ? 4 : ((polygon->Attr >> 4) & 0x3);
            variant.Texture = 0;
            variant.WrapS = 0;
            variant.WrapT = 0;
            variant.CaptureReference = 0;
            variant.CaptureYOffset = 0;
            variant.CaptureType = 0;

            u32* textureLastVariant = nullptr;
            const u32 textype = (polygon->TexParam >> 26) & 0x7;
            if (enableTextureMaps && textype)
            {
                const u32 texaddr = polygon->TexParam & 0xFFFFu;
                const u32 texwidth = TextureWidth(polygon->TexParam);
                const u32 texheight = TextureHeight(polygon->TexParam);
                int captureBlock = -1;
                if (textype == 7u && (texwidth == 128u || texwidth == 256u))
                {
                    const u32 startBlock = (texaddr << 3u) >> 15u;
                    const u32 endBlock =
                        ((texaddr << 3u) + texwidth * texheight * 2u + 0x7FFFu) >> 15u;
                    for (u32 block = startBlock; block < endBlock && block < 16u; ++block)
                    {
                        if (captureInfo[block] != -1)
                            captureBlock = captureInfo[block];
                    }
                }

                if (captureBlock != -1)
                {
                    const u32 bank = static_cast<u32>(captureBlock) >> 2u;
                    const u32 yOffset = texwidth == 128u
                        ? ((texaddr >> 5u) & 0x7Fu)
                        : ((texaddr >> 6u) & 0xFFu);
                    const u32 layerBase = texwidth == 128u
                        ? (static_cast<u32>(captureBlock) & 3u) * 16384u
                        : 0u;
                    const u32 queryAddress = layerBase + yOffset * texwidth;
                    u32 reference = GPU.GetRenderer().GetCaptureTextureReference(bank, queryAddress);
                    if (reference != 0u)
                    {
                        variant.CaptureType = texwidth == 128u ? 1u : 2u;
                        variant.CaptureYOffset = static_cast<s32>(yOffset);
                        variant.CaptureReference =
                            (reference & ~StructuredComposition::kCaptureReferenceAddressMask)
                            | layerBase;
                        prevTexLayer = texwidth == 128u
                            ? static_cast<u32>(captureBlock)
                            : bank;
                        textureLastVariant = &captureLastVariant[captureBlock];
                    }
                }

                if (variant.CaptureType == 0u)
                {
                    Texcache.GetTexture(polygon->TexParam, polygon->TexPalette,
                        variant.Texture, prevTexLayer, textureLastVariant);
                }

                const bool wrapS = (polygon->TexParam >> 16) & 1;
                const bool wrapT = (polygon->TexParam >> 17) & 1;
                const bool mirrorS = (polygon->TexParam >> 18) & 1;
                const bool mirrorT = (polygon->TexParam >> 19) & 1;
                variant.WrapS = wrapS ? (mirrorS ? 2u : 1u) : 0u;
                variant.WrapT = wrapT ? (mirrorT ? 2u : 1u) : 0u;

                if (textureLastVariant && *textureLastVariant < numVariants
                    && variants[*textureLastVariant] == variant)
                {
                    foundVariant = true;
                    prevVariant = *textureLastVariant;
                }
            }

            if (!foundVariant)
            {
                const u32 variantHash = HashVariant(variant);
                u32 indexedVariant = 0;
                const bool indexedFound = VariantLookup.Find(variantHash,
                    [&](u32 index) noexcept {
                        return index < numVariants && variants[index] == variant;
                    }, indexedVariant);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
                if (RasterDifferential::Enabled())
                {
                    bool legacyFound = false;
                    u32 legacyIndex = 0;
                    for (u32 candidate = numVariants; candidate != 0; --candidate)
                    {
                        if (variants[candidate - 1] == variant)
                        {
                            legacyFound = true;
                            legacyIndex = candidate - 1;
                            break;
                        }
                    }
                    if (indexedFound != legacyFound ||
                        (indexedFound && indexedVariant != legacyIndex))
                    {
                        SetRuntimeFailure("variant index disagreed with legacy insertion order");
                    }
                }
#endif
                if (indexedFound)
                {
                    foundVariant = true;
                    prevVariant = indexedVariant;
                }

                if (!foundVariant && numVariants < MaxVariants)
                {
                    prevVariant = numVariants;
                    variants[numVariants] = variant;
                    variants[numVariants].Width = static_cast<u16>(TextureWidth(polygon->TexParam));
                    variants[numVariants].Height = static_cast<u16>(TextureHeight(polygon->TexParam));
                    const bool inserted = VariantLookup.Insert(
                        variantHash, numVariants,
                        [&](u32 index) noexcept { return HashVariant(variants[index]); });
                    assert(inserted);
                    (void)inserted;
                    numVariants++;
                }

                if (textureLastVariant)
                    *textureLastVariant = prevVariant;
            }
        }
        RenderPolygons[i].Variant = prevVariant;
        RenderPolygons[i].TextureLayer = static_cast<float>(prevTexLayer);

        if (polygon->FacingView)
        {
            nextVL = curVL + 1;
            if (nextVL >= nverts) nextVL = 0;
            nextVR = curVR - 1;
            if (static_cast<s32>(nextVR) < 0) nextVR = nverts - 1;
        }
        else
        {
            nextVL = curVL - 1;
            if (static_cast<s32>(nextVL) < 0) nextVL = nverts - 1;
            nextVR = curVR + 1;
            if (nextVR >= nverts) nextVR = 0;
        }

        s32 scaledPositions[10][2];
        s32 ytop = ScreenHeight, ybot = 0;
        for (u32 v = 0; v < polygon->NumVertices; v++)
        {
            if (HiresCoordinates && ScaleFactor > 1)
            {
                scaledPositions[v][0] = (polygon->Vertices[v]->HiresPosition[0] * ScaleFactor) >> 4;
                scaledPositions[v][1] = (polygon->Vertices[v]->HiresPosition[1] * ScaleFactor) >> 4;
            }
            else
            {
                scaledPositions[v][0] = polygon->Vertices[v]->FinalPosition[0] * ScaleFactor;
                scaledPositions[v][1] = polygon->Vertices[v]->FinalPosition[1] * ScaleFactor;
            }
            ytop = std::min(scaledPositions[v][1], ytop);
            ybot = std::max(scaledPositions[v][1], ybot);
        }
        RenderPolygons[i].YTop = ytop;
        RenderPolygons[i].YBot = ybot;
        RenderPolygons[i].XMin = ScreenWidth;
        RenderPolygons[i].XMax = 0;

        if (ybot == ytop)
        {
            vtop = 0; vbot = 0;

            RenderPolygons[i].YBot++;

            u32 j = 1;
            if (scaledPositions[j][0] < scaledPositions[vtop][0]) vtop = j;
            if (scaledPositions[j][0] > scaledPositions[vbot][0]) vbot = j;

            j = nverts - 1;
            if (scaledPositions[j][0] < scaledPositions[vtop][0]) vtop = j;
            if (scaledPositions[j][0] > scaledPositions[vbot][0]) vbot = j;

            if (numYSpans + 2 > MaxYSpanSetups || numSetupIndices >= MaxYSpanIndices)
                break;

            const u32 curSpanL = numYSpans;
            SetupYSpanDummy(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, vtop, 0, scaledPositions);
            const u32 curSpanR = numYSpans;
            SetupYSpanDummy(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, vbot, 1, scaledPositions);

            YSpanIndices[numSetupIndices].PolyIdx = static_cast<u16>(i);
            YSpanIndices[numSetupIndices].SpanIdxL = static_cast<u16>(curSpanL);
            YSpanIndices[numSetupIndices].SpanIdxR = static_cast<u16>(curSpanR);
            YSpanIndices[numSetupIndices].Y = static_cast<u16>(ytop);
            numSetupIndices++;
        }
        else
        {
            if (numYSpans + 2 > MaxYSpanSetups)
                break;

            u32 curSpanL = numYSpans;
            SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVL, nextVL, 0, scaledPositions);
            u32 curSpanR = numYSpans;
            SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVR, nextVR, 1, scaledPositions);

            for (s32 y = ytop; y < ybot; y++)
            {
                if (y >= scaledPositions[nextVL][1] && curVL != polygon->VBottom)
                {
                    while (y >= scaledPositions[nextVL][1] && curVL != polygon->VBottom)
                    {
                        curVL = nextVL;
                        if (polygon->FacingView)
                        {
                            nextVL = curVL + 1;
                            if (nextVL >= nverts)
                                nextVL = 0;
                        }
                        else
                        {
                            nextVL = curVL - 1;
                            if (static_cast<s32>(nextVL) < 0)
                                nextVL = nverts - 1;
                        }
                    }

                    if (numYSpans >= MaxYSpanSetups)
                        break;
                    curSpanL = numYSpans;
                    SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVL, nextVL, 0, scaledPositions);
                }
                if (y >= scaledPositions[nextVR][1] && curVR != polygon->VBottom)
                {
                    while (y >= scaledPositions[nextVR][1] && curVR != polygon->VBottom)
                    {
                        curVR = nextVR;
                        if (polygon->FacingView)
                        {
                            nextVR = curVR - 1;
                            if (static_cast<s32>(nextVR) < 0)
                                nextVR = nverts - 1;
                        }
                        else
                        {
                            nextVR = curVR + 1;
                            if (nextVR >= nverts)
                                nextVR = 0;
                        }
                    }

                    if (numYSpans >= MaxYSpanSetups)
                        break;
                    curSpanR = numYSpans;
                    SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVR, nextVR, 1, scaledPositions);
                }

                if (numSetupIndices >= MaxYSpanIndices)
                    break;

                YSpanIndices[numSetupIndices].PolyIdx = static_cast<u16>(i);
                YSpanIndices[numSetupIndices].SpanIdxL = static_cast<u16>(curSpanL);
                YSpanIndices[numSetupIndices].SpanIdxR = static_cast<u16>(curSpanR);
                YSpanIndices[numSetupIndices].Y = static_cast<u16>(y);
                numSetupIndices++;
            }
        }

        // Counts are committed only after the complete polygon is built. The
        // arrays cover the valid DS worst case (2048 polygons, ten edges and
        // every output scanline); the guards above remain defensive for
        // malformed input and must never expose partial records to the GPU.
        numPolygons = i + 1;
        previousPolygon = polygon;
    }

    return numVariants;
}

u32 DX12Renderer3D::BuildPolygonBatches(u32 numPolygons)
{
    if (numPolygons == 0)
    {
        PolygonBatches[0] = { 0, 0 };
        DX12Perf::AddCounter(DX12Perf::Counter::PolygonBatchCount);
        DX12Perf::SetCounter(
            DX12Perf::Counter::PolygonBatchCapacity, MaxWorkTiles);
        return 1;
    }

    const u64 capacity = static_cast<u64>(MaxWorkTiles);
    u32 first = 0;
    u32 count = 0;
    u32 batchCount = 0;
    u64 batchTiles = 0;
    u64 maxBatchTiles = 0;

    for (u32 i = 0; i < numPolygons; ++i)
    {
        const RenderPolygon& polygon = RenderPolygons[i];
        const s32 minX = std::clamp(polygon.XMin, 0, ScreenWidth - 1);
        const s32 maxX = std::clamp(polygon.XMax, 0, ScreenWidth - 1);
        const s32 minY = std::clamp(polygon.YTop, 0, ScreenHeight - 1);
        const s32 maxY = static_cast<s32>(std::clamp<s64>(
            static_cast<s64>(polygon.YBot) - 1, 0, ScreenHeight - 1));

        u64 polygonTiles = 0;
        if (minX <= maxX && minY <= maxY)
        {
            const u64 tileColumns = static_cast<u64>(maxX / TileSize - minX / TileSize + 1);
            const u64 tileRows = static_cast<u64>(maxY / TileSize - minY / TileSize + 1);
            polygonTiles = tileColumns * tileRows;
        }

        if (count != 0 && batchTiles + polygonTiles > capacity)
        {
            maxBatchTiles = std::max(maxBatchTiles, batchTiles);
            PolygonBatches[batchCount++] = { first, count };
            first = i;
            count = 0;
            batchTiles = 0;
        }

        assert(polygonTiles <= capacity);
        batchTiles += polygonTiles;
        ++count;
    }

    if (count != 0)
    {
        maxBatchTiles = std::max(maxBatchTiles, batchTiles);
        PolygonBatches[batchCount++] = { first, count };
    }
    DX12Perf::AddCounter(DX12Perf::Counter::PolygonBatchCount, batchCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::PolygonBatchSplitCount,
        batchCount > 0u ? batchCount - 1u : 0u);
    DX12Perf::SetCounter(
        DX12Perf::Counter::PolygonBatchMaxTiles, maxBatchTiles);
    DX12Perf::SetCounter(DX12Perf::Counter::PolygonBatchCapacity, capacity);
    return batchCount;
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void DX12Renderer3D::RenderFrame()
{
    if (RuntimeFailed)
        return;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
    if (!Context || !PipelineRepo.GetRootSignature() || !ResultBuffer || !ResultWinnerBuffer
        || !FinalFBBuffer || !BinResultBuffer || !IndirectArgsBuffer)
    {
        SetRuntimeFailure("required frame resources are unavailable");
        return;
    }
    if (ShaderStepIdx < ShaderStepCount)
        return; // pipelines are still being compiled

    // Select the CPU-owned slot before texture preparation so cache evictions
    // and decoded uploads inherit the same fence lifetime as this frame.
    CurrentRasterFrameIndex = NextRasterFrameIndex;
    RasterFrameSlot& rasterFrame = ActiveRasterFrame();
    TextureHeap.SetFrameResources(
        &rasterFrame.Commands, &rasterFrame.Uploads,
        CurrentRasterFrameIndex);

    DX12Perf::SetScale(static_cast<u32>(ScaleFactor));
    DX12Perf::AddCounter(DX12Perf::Counter::Frames);

    u8 texcacheClearBitmapDirty = 0;
    bool textureCacheChanged = false;
    int numYSpans = 0;
    int numSetupIndices = 0;
    u32 numPolygons = 0;
    u32 numVariants = 0;
    {
        TextureHeap.ResetFailures();
        DX12Perf::ScopedCpuTimer prepareTimer(DX12Perf::CpuMetric::RasterCpuPrepare);
        {
            DX12Perf::ScopedCpuTimer texcacheTimer(DX12Perf::CpuMetric::TexcacheUpdate);
            textureCacheChanged = Texcache.Update(texcacheClearBitmapDirty);
        }
        if (TextureHeap.HadFailure())
        {
            SetRuntimeFailure("texture cache logical reservation or CPU upload preparation failed");
            return;
        }
        ClearBitmapDirty |= texcacheClearBitmapDirty;
        // A dirty clear-image mirror only affects rendering while the clear
        // image itself is enabled. Keep the dirty bits latched while disabled
        // so a later enable uploads the exact current contents, but do not let
        // that dormant work permanently defeat exact identical-frame reuse.
        const bool clearBitmapRefreshRequired =
            (GPU3D.RenderDispCnt & (1u << 14u)) != 0u
            && ClearBitmapDirty != 0u;
        if (!textureCacheChanged && GPU3D.RenderFrameIdentical
            && FinalFBHasValidFrame && !clearBitmapRefreshRequired)
        {
            DX12Perf::AddCounter(DX12Perf::Counter::IdenticalFrames);
            DX12Perf::MaybeReport();
            return;
        }

        // BuildPolygons is CPU-only until the texture heap records its queued
        // uploads below. This lets the previous GPU submission continue while
        // the current frame resolves polygon/span and texture identity.
        DX12Perf::ScopedCpuTimer polygonTimer(DX12Perf::CpuMetric::BuildPolygons);
        numVariants = BuildPolygons(numYSpans, numSetupIndices, numPolygons);
        if (TextureHeap.HadFailure())
        {
            SetRuntimeFailure("texture cache CPU decode/upload preparation failed");
            return;
        }
    }

    // Physical resource creation is host-side and independent of the
    // frame-local command allocator, descriptor heap, and upload ring. Start
    // it before Begin(true) waits for the previous raster submission.
    const TextureMaterializeResult materializeResult =
        TextureHeap.MaterializePendingCreates();
    if (materializeResult == TextureMaterializeResult::Fatal)
    {
        SetRuntimeFailure("could not materialize a DX12 texture resource");
        return;
    }

    ID3D12GraphicsCommandList* list = nullptr;
    list = rasterFrame.Commands.Begin(true);
    if (!list)
    {
        SetRuntimeFailure("could not begin a frame command list");
        return;
    }

    // A retryable allocation failure is the only path allowed to create a
    // texture after Begin(): the begin has retired the prior frame and the
    // reset below releases this heap's deferred resources before one retry.
    auto resetFrameResources = [&] {
        rasterFrame.Descriptors.Reset(PersistentTextureDescriptorCount);
        rasterFrame.Uploads.Reset();
        TextureHeap.CollectGarbage();
        BoundSrvTexture = nullptr;
        BoundSrvTable = {};
        ResetFrameSrvCache();
    };
    resetFrameResources();

    if (materializeResult == TextureMaterializeResult::RetryAfterRetire)
    {
        DX12Perf::AddCounter(DX12Perf::Counter::TextureMaterializeRetryAfterRetireCount);
        TextureHeap.ClearRetryableCreationFailure();
        if (TextureHeap.MaterializePendingCreates() != TextureMaterializeResult::Ready)
        {
            DX12Perf::AddCounter(DX12Perf::Counter::TextureMaterializeRetryFailCount);
            rasterFrame.Commands.Submit();
            SetRuntimeFailure("could not materialize a DX12 texture resource after frame retirement");
            return;
        }
        DX12Perf::AddCounter(DX12Perf::Counter::TextureMaterializeRetrySuccessCount);
    }

    DX12Perf::ScopedCpuTimer rasterRecordTimer(DX12Perf::CpuMetric::RasterRecordSubmit);
    RecordDX12GpuMetric(
        rasterFrame.Commands, GpuMetric::Raster, DX12Perf::Counter::RasterGpuTimeNs);
    rasterFrame.Commands.WriteTimestamp(GpuMetricQueryIndex(GpuMetric::Raster, false));

    InsertRasterScratchReuseBarriers(list);

    UpdateClearBitmap();
    TextureHeap.RecordPendingUploads();
    if (TextureHeap.HadFailure())
    {
        rasterFrame.Commands.Submit();
        SetRuntimeFailure(TextureHeap.HadCreationFailure()
            ? "could not materialize a DX12 texture resource"
            : "could not allocate or map a texture spill upload");
        return;
    }
    DX12Perf::RecordGeometry(
        numPolygons, numVariants, static_cast<u32>(numYSpans), static_cast<u32>(numSetupIndices));
    TextureHeap.FlushUploadBarriers();

    // Texture-cache setup can use retained spill uploads if the main upload
    // ring fills, while continuing to record this same command list.
    list = rasterFrame.Commands.GetList();
    if (!list)
    {
        SetRuntimeFailure("frame command list was lost during texture upload");
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { rasterFrame.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(PipelineRepo.GetRootSignature());

    if (numYSpans > 0)
    {
        const u64 spanBytes = sizeof(SpanSetupY) * static_cast<u64>(numYSpans)
            + sizeof(SetupIndices) * static_cast<u64>(numSetupIndices)
            + sizeof(RenderPolygon) * static_cast<u64>(numPolygons);
        {
            DX12Perf::ScopedCpuTimer copyTimer(DX12Perf::CpuMetric::SpanStagingCopy);
            std::memcpy(rasterFrame.YSpanSetupStagingPtr, YSpanSetups.get(),
                sizeof(SpanSetupY) * numYSpans);
            std::memcpy(rasterFrame.SetupIndicesStagingPtr, YSpanIndices.data(),
                sizeof(SetupIndices) * numSetupIndices);
            std::memcpy(rasterFrame.RenderPolygonStagingPtr, RenderPolygons.get(),
                sizeof(RenderPolygon) * numPolygons);
        }
        DX12Perf::AddCounter(DX12Perf::Counter::SpanUploadBytes, spanBytes);

        TransitionBuffer(list, YSpanSetupBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        TransitionBuffer(list, SetupIndicesBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        TransitionBuffer(list, RenderPolygonBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        list->CopyBufferRegion(YSpanSetupBuffer.Get(), 0, rasterFrame.YSpanSetupStaging.Get(), 0,
            sizeof(SpanSetupY) * numYSpans);
        list->CopyBufferRegion(SetupIndicesBuffer.Get(), 0, rasterFrame.SetupIndicesStaging.Get(), 0,
            sizeof(SetupIndices) * numSetupIndices);
        list->CopyBufferRegion(RenderPolygonBuffer.Get(), 0, rasterFrame.RenderPolygonStaging.Get(), 0,
            sizeof(RenderPolygon) * numPolygons);

        TransitionBuffer(list, YSpanSetupBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        TransitionBuffer(list, SetupIndicesBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        TransitionBuffer(list, RenderPolygonBuffer.Get(),
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    }

    if (!UploadMetaUniform(list, numVariants, numPolygons))
    {
        rasterFrame.Commands.Submit();
        SetRuntimeFailure("could not upload the frame uniform block");
        return;
    }

    if (!BindFrameUavTable(list) || !BindStaticSrvTable(list) ||
        !BindSrvTable(list, 0u))
    {
        rasterFrame.Commands.Submit();
        SetRuntimeFailure("could not bind the frame descriptor tables");
        return;
    }

    DispatchUniform constants = MakeDispatchUniform();
    SetDispatchConstants(list, constants);

    const bool wbuffer = numYSpans > 0 && GPU3D.RenderPolygonRAM[0]->WBuffer;

    const u32 polygonBatchCount = BuildPolygonBatches(numPolygons);

    for (u32 batchIndex = 0; batchIndex < polygonBatchCount; ++batchIndex)
    {
        const PolygonBatch& batch = PolygonBatches[batchIndex];

        // Reuse the bounded tile working set for a consecutive polygon batch.
        list->SetPipelineState(PipelineClearCoarseBinMask.Get());
        list->Dispatch(
            static_cast<UINT>(TilesPerLine * TileLines / ClearCoarseBinMaskLocalSize), 1, 1);
        InsertUavBarrier(list, BinResultBuffer.Get());

        if (batch.PolygonCount > 0)
        {
        // 2. reset the indirect work counts
        list->SetPipelineState(PipelineClearIndirectWorkCount.Get());
        list->Dispatch(DivRoundUp(numVariants, 32), 1, 1);
        InsertUavBarrier(list, BinResultBuffer.Get());

        if (batchIndex == 0)
        {
            // X spans are shared by every batch and are generated once.
            list->SetPipelineState(PipelineInterpSpans[wbuffer ? 1 : 0].Get());
            const u32 setupIndexCount = static_cast<u32>(numSetupIndices);
            for (u32 base = 0; base < setupIndexCount;)
            {
                const u32 chunkCount = std::min(
                    setupIndexCount - base, kMaxInterpSpansPerDispatch);
                constants.InterpSpanBase = base;
                constants.InterpSpanCount = chunkCount;
                SetDispatchConstants(list, constants);
                list->Dispatch(
                    DivRoundUp(chunkCount, kInterpSpansThreadsPerGroup), 1, 1);
                base += chunkCount;
            }
            InsertUavBarrier(list, XSpanSetupBuffer.Get());
        }

        // 4. bin polygons into coarse and fine tiles
        constants = MakeDispatchUniform();
        constants.CurVariant = batch.FirstPolygon;
        constants.TexWidth = batch.PolygonCount;
        SetDispatchConstants(list, constants);
        list->SetPipelineState(PipelineBinCombined.Get());
        list->Dispatch(
            DivRoundUp(batch.PolygonCount, 32),
            static_cast<UINT>(ScreenWidth / CoarseTileW),
            static_cast<UINT>(ScreenHeight / CoarseTileH));
        InsertUavBarrier(list, BinResultBuffer.Get());
        InsertUavBarrier(list, WorkDescBuffer.Get());

        // 5. turn the per-variant counts into dispatch arguments and offsets
        list->SetPipelineState(PipelineCalcOffsets.Get());
        list->Dispatch(DivRoundUp(numVariants, 32), 1, 1);
        InsertUavBarrier(list, BinResultBuffer.Get());
        InsertUavBarrier(list, IndirectArgsBuffer.Get());
        DX12Perf::AddCounter(
            DX12Perf::Counter::DX12IndirectArgsDirectWriteCount,
            static_cast<u64>(numVariants) + 1ull);

        // CalcOffsets writes the same header layout directly into the dedicated
        // indirect-argument UAV. BinResult stays UAV for the following sort and
        // raster passes; only the argument buffer changes state here.
        TransitionBuffer(list, IndirectArgsBuffer.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);

        // 6. sort the work list by variant
        list->SetPipelineState(PipelineSortWork.Get());
        list->ExecuteIndirect(
            PipelineRepo.GetDispatchSignature(), 1, IndirectArgsBuffer.Get(),
            offsetof(BinResultHeader, SortWorkWorkCount), nullptr, 0);
        InsertUavBarrier(list, WorkDescBuffer.Get());

        // 7. rasterise, one indirect dispatch per variant
        {
            const bool highlightMode = (GPU3D.RenderDispCnt & (1 << 1)) != 0;

            static const int kNoTextureKinds[5] = {
                RasteriseKind_NoTexture,
                RasteriseKind_NoTexture,
                -1, // toon or highlight, decided below
                RasteriseKind_NoTexture,
                RasteriseKind_ShadowMask,
            };
            static const int kUseTextureKinds[5] = {
                RasteriseKind_UseTextureModulate,
                RasteriseKind_UseTextureDecal,
                -1,
                RasteriseKind_UseTextureDecal,
                RasteriseKind_ShadowMask,
            };

            ID3D12PipelineState* prevPipeline = nullptr;

            bool descriptorsValid = true;
            for (u32 i = 0; i < numVariants; i++)
            {
                const Variant& variant = Variants[i];
                // Retained display captures bypass Texcache, but they are
                // still sampled by the textured raster pipeline.
                const bool hasTexture = variant.Texture != 0 || variant.CaptureType != 0;
                const int blendMode = std::min<int>(variant.BlendMode, 4);

                int kind;
                if (blendMode == 2)
                {
                    if (hasTexture)
                        kind = highlightMode ? RasteriseKind_UseTextureHighlight : RasteriseKind_UseTextureToon;
                    else
                        kind = highlightMode ? RasteriseKind_NoTextureHighlight : RasteriseKind_NoTextureToon;
                }
                else
                {
                    kind = hasTexture ? kUseTextureKinds[blendMode] : kNoTextureKinds[blendMode];
                }

                ID3D12PipelineState* pipeline = PipelineRasterise[kind * 2 + (wbuffer ? 1 : 0)].Get();
                if (!pipeline)
                    continue;

                if (pipeline != prevPipeline)
                {
                    list->SetPipelineState(pipeline);
                    prevPipeline = pipeline;
                }

                const DX12TextureHeap::Entry* texture = TextureHeap.Lookup(variant.Texture);
                if (variant.Texture != 0 && (!texture || !texture->PhysicalReady))
                {
                    descriptorsValid = false;
                    break;
                }
                if (!BindSrvTable(list, variant.Texture))
                {
                    descriptorsValid = false;
                    break;
                }

                DispatchUniform variantConstants = MakeDispatchUniform();
                variantConstants.CurVariant = i;
                variantConstants.TexWidth = variant.Width ? variant.Width : 8;
                variantConstants.TexHeight = variant.Height ? variant.Height : 8;
                variantConstants.TexWrapS = variant.WrapS;
                variantConstants.TexWrapT = variant.WrapT;
                // The InterpSpans-only fields are reused by Rasterise for the
                // backend-neutral retained-capture reference.
                variantConstants.InterpSpanBase = variant.CaptureType;
                variantConstants.InterpSpanCount = static_cast<u32>(variant.CaptureYOffset);
                variantConstants.Pad = variant.CaptureReference;
                SetDispatchConstants(list, variantConstants);

                list->ExecuteIndirect(
                    PipelineRepo.GetDispatchSignature(), 1, IndirectArgsBuffer.Get(),
                    offsetof(BinResultHeader, VariantWorkCount) + i * 16, nullptr, 0);
            }
            if (!descriptorsValid)
            {
                rasterFrame.Commands.Submit();
                SetRuntimeFailure("the shader-visible descriptor heap was exhausted");
                return;
            }
        }

        TransitionBuffer(list, IndirectArgsBuffer.Get(),
            D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

        InsertUavBarrier(list, TileBuffers[0].Get());
        InsertUavBarrier(list, TileBuffers[1].Get());
        InsertUavBarrier(list, TileBuffers[2].Get());
    }

        // 8. Continue from the preceding batch's exact two-layer result and
        // shadow state. Batch zero initializes from the configured clear.
        constants = MakeDispatchUniform();
        constants.CurVariant = batch.FirstPolygon;
        constants.TexHeight = batchIndex != 0 ? 1u : 0u;
        SetDispatchConstants(list, constants);
        list->SetPipelineState(PipelineDepthBlend[wbuffer ? 1 : 0].Get());
        list->Dispatch(
            static_cast<UINT>(ScreenWidth / TileSize),
            static_cast<UINT>(ScreenHeight / TileSize),
            1);
        InsertUavBarrier(list, ResultBuffer.Get());
        InsertUavBarrier(list, BlendStateBuffer.Get());
        InsertUavBarrier(list, TileBuffers[2].Get());
        InsertUavBarrier(list, ResultWinnerBuffer.Get());

        // Software-exact coverage is defined on the native DS raster grid.
        // High-resolution targets retain the separate scaled-raster contract,
        // matching the Metal compute backend.
        if (ScaleFactor == 1
            && (GPU3D.RenderDispCnt & (1u << 4)) != 0u
            && numSetupIndices > 0)
        {
            constants = MakeDispatchUniform();
            constants.CurVariant = batch.FirstPolygon;
            constants.TexWidth = batch.PolygonCount;
            constants.TexHeight = static_cast<u32>(numSetupIndices);
            SetDispatchConstants(list, constants);
            list->SetPipelineState(Gpu2D.CorrectCoverage.Get());
            list->Dispatch(DivRoundUp(static_cast<u32>(numSetupIndices), 64), 1, 1);
            InsertUavBarrier(list, ResultBuffer.Get());
        }
    }

    // 9. final pass: edge marking / fog / anti-aliasing resolve
    u32 finalPassVariant = 0;
    if (GPU3D.RenderDispCnt & (1 << 5)) finalPassVariant |= 0x1; // edge marking
    if (GPU3D.RenderDispCnt & (1 << 7)) finalPassVariant |= 0x2; // fog
    if (GPU3D.RenderDispCnt & (1 << 4)) finalPassVariant |= 0x4; // anti-aliasing

    if (PipelineFinalPass[finalPassVariant])
    {
        list->SetPipelineState(PipelineFinalPass[finalPassVariant].Get());
        list->Dispatch(DivRoundUp(static_cast<u32>(ScreenWidth), 32), static_cast<UINT>(ScreenHeight), 1);
        InsertUavBarrier(list, FinalFBBuffer.Get());
    }

    bool submitted = false;
    {
        DX12Perf::ScopedCpuTimer submitTimer(DX12Perf::CpuMetric::QueueSubmit);
        rasterFrame.Commands.WriteTimestamp(GpuMetricQueryIndex(GpuMetric::Raster, true));
        submitted = rasterFrame.Commands.Submit();
    }
    if (submitted)
    {
        FrameInFlight = true;
        FrameReadbackValid = false;
        FinalFBHasValidFrame = true;
        NextRasterFrameIndex =
            (CurrentRasterFrameIndex + 1u) % RasterFramesInFlight;
    }
    else
    {
        SetRuntimeFailure("frame command submission failed");
    }
    DX12Perf::MaybeReport();
}

bool DX12Renderer3D::RecordNativeResolveAndReadback()
{
    if (!DemandReadbackCommands.GetList() || !DemandReadbackDescriptors.GetHeap()
        || !PipelineRepo.GetRootSignature() || !PipelineResolve || !FinalFBBuffer || !ResolveBuffer
        || !ReadbackBuffer || !FrameUavCpu.ptr)
        return false;

    // Retire only the previous lazy-capture submission before recycling its
    // command allocator and descriptor table. This is not a queue-wide idle.
    if (!DemandReadbackCommands.WaitForSubmittedValue())
        return false;

    ID3D12GraphicsCommandList* list = DemandReadbackCommands.TryBegin();
    if (!list)
        return false;

    DemandReadbackDescriptors.Reset();
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    {
        DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
        if (!DemandReadbackDescriptors.Allocate(kUavTableSize, cpu, gpu))
        {
            DemandReadbackCommands.Submit();
            return false;
        }
        Context->GetDevice()->CopyDescriptorsSimple(
            kUavTableSize,
            cpu,
            FrameUavCpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCopyCount, kUavTableSize);
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorWriteCount, kUavTableSize);

    ID3D12DescriptorHeap* heaps[] = { DemandReadbackDescriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(PipelineRepo.GetRootSignature());
    SetDispatchConstants(list, MakeDispatchUniform());
    list->SetComputeRootDescriptorTable(kRootParamUavTable, gpu);

    // The main render and compositor use the same direct queue. This UAV
    // barrier makes FinalFB writes visible to the resolve in this later list.
    InsertUavBarrier(list, FinalFBBuffer.Get());
    InsertUavBarrier(list, Capture.GetSidecarBuffer());
    list->SetPipelineState(PipelineResolve.Get());
    list->Dispatch(DivRoundUp(256, 8), DivRoundUp(192, 8), 1);
    InsertUavBarrier(list, ResolveBuffer.Get());
    TransitionBuffer(list, ResolveBuffer.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(ReadbackBuffer.Get(), 0, ResolveBuffer.Get(), 0,
        256ull * 192ull * 4ull);
    TransitionBuffer(list, ResolveBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (!DemandReadbackCommands.Submit())
        return false;

    NativeReadbackSubmitted = true;
    DX12Perf::AddCounter(DX12Perf::Counter::NativeResolveCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeReadbackCopyBytes, 256ull * 192ull * 4ull);
    return true;
}

void DX12Renderer3D::EnsureFrameReadback()
{
    if (FrameReadbackValid || NativeReadbackSubmitted || !FinalFBHasValidFrame
        || !ReadbackBuffer)
        return;

    DX12Perf::AddCounter(DX12Perf::Counter::NativeReadbackDemandCount);
    if (!RecordNativeResolveAndReadback())
    {
        SetRuntimeFailure("could not submit the demand-driven capture resolve/readback");
        return;
    }

#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto waitStart = DX12Perf::Clock::now();
#endif
    {
        DX12Perf::ScopedCpuTimer waitTimer(DX12Perf::CpuMetric::CaptureWait);
        if (!DemandReadbackCommands.WaitForSubmittedValue())
        {
            SetRuntimeFailure("the demand-driven capture readback did not complete in time");
            FrameInFlight = false;
            return;
        }
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    DX12Perf::RecordNativeReadbackWait(static_cast<u64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            DX12Perf::Clock::now() - waitStart).count()));
#endif

    DX12Perf::ScopedCpuTimer mapTimer(DX12Perf::CpuMetric::CaptureMapCopy);
    DX12Perf::AddCounter(DX12Perf::Counter::CaptureReadCount);
    D3D12_RANGE readRange{ 0, 256ull * 192ull * 4ull };
    void* mapped = nullptr;
    if (SUCCEEDED(ReadbackBuffer->Map(0, &readRange, &mapped)) && mapped)
    {
        std::memcpy(ColorBuffer.data(), mapped, ColorBuffer.size() * sizeof(u32));
        D3D12_RANGE noWrite{ 0, 0 };
        ReadbackBuffer->Unmap(0, &noWrite);
    }
    else
    {
        SetRuntimeFailure("native capture readback mapping failed");
        FrameInFlight = false;
        return;
    }

    FrameInFlight = false;
    FrameReadbackValid = true;
}

bool DX12Renderer3D::ReadNativeCapture(
    u32 bank,
    u32 start,
    u32 len,
    const CaptureBlockProvenance& expected,
    u8* destination)
{
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    if (const char* forcedFailure = std::getenv(
            "MELONPRIME_TEST_GPU2D_CAPTURE_READBACK_FAIL");
        forcedFailure && forcedFailure[0] == '1')
    {
        return false;
    }
#endif
    if (bank >= 4u || start >= 4u || !destination
        || !BlendStateBuffer
        || !Capture.HasReadbackBuffer()
        || !Provenance.AcceptsBlock(expected, CaptureOwner::NativeDX12))
    {
        return false;
    }

    // The current emulated frame may still be recording its 192 lines. The
    // expected block identity, not FrameRecorder finalization, is the
    // authority for this readback.

    // The 3D resolve shares this context. Retire its optional submission
    // before recycling the command allocator.
    if (NativeReadbackSubmitted && !FrameReadbackValid)
        EnsureFrameReadback();
    if (RuntimeFailed)
        return false;

    // The capture region lives past the 3D framebuffer in the same buffer.
    const u64 captureBase = static_cast<u64>(ScreenWidth)
        * static_cast<u64>(ScreenHeight) * sizeof(u32);
    return Capture.ReadBlocks(
        DemandReadbackCommands,
        BlendStateBuffer.Get(),
        captureBase,
        bank,
        start,
        len,
        destination);
}

NativeCaptureStateIdentity DX12Renderer3D::GetNativeCaptureStateIdentity(
    CaptureOwner owner) const noexcept
{
    return Provenance.GetIdentity(owner);
}

void DX12Renderer3D::InvalidateHighResCaptureRange(
    u32 bank,
    u32 start,
    u32 len,
    GPU2DNative::HighResCaptureFallbackReason reason) noexcept
{
    GPU2DNative::InvalidateHighResCaptureBlocks(
        HighResCaptureProvenance, bank, start, len, reason);
}

bool DX12Renderer3D::ComposeStructuredOutput(
    const std::array<const u32*, 14>& planes,
    const std::array<const u32*, 2>& lineMeta,
    const u32* captureCommands,
    const StructuredComposition::ScreenRoutingView& screenRouting,
    u64 generation,
    const StructuredComposition::GenerationState& contentGeneration)
{
    LastComposeResult = GPU2DComposeResult::Unavailable;
    if (RuntimeFailed)
    {
        LastComposeResult = GPU2DComposeResult::Fatal;
        return false;
    }
    if (ShaderStepIdx < ShaderStepCount)
        return false;
    if (ComposedOutputValid && ComposedGeneration == generation)
    {
        LastComposeResult = GPU2DComposeResult::Success;
        return true;
    }
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!Context || !PipelineCaptureSidecar || !Gpu2D.Compositor
        || !state || !FinalFBBuffer || !Capture.GetSidecarBuffer())
    {
        SetRuntimeFailure("required compositor resources are unavailable");
        return false;
    }
    for (const u32* plane : planes)
    {
        if (!plane)
            return false;
    }
    for (const u32* meta : lineMeta)
    {
        if (!meta)
            return false;
    }
    if (!captureCommands)
        return false;
    const StructuredComposition::CaptureLineAnalysis captureAnalysis =
        StructuredComposition::AnalyzeCaptureDependencies(planes, captureCommands);
    u32 slotIndex = 0;
    bool slotLeased = false;
    {
        const auto lock = state->Ring.LockPublication();
        slotIndex = state->Ring.TakeCursorSlot();
        slotLeased = state->Ring.IsLeased(slotIndex);
    }
    DX12Gpu2DOutput::Slot& slot = state->Slots[slotIndex];
    if (slotLeased)
    {
        // Presentation is still reading this slot. Reusing the last published
        // frame is preferable to blocking the emulation thread.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        LastComposeResult = GPU2DComposeResult::Backpressure;
        return false;
    }
    ID3D12GraphicsCommandList* list = slot.Commands.TryBegin();
    if (!list)
    {
        // The GPU has not retired this ring slot after three frames. Keep the
        // previous output and let the emulator continue without a fence wait.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        LastComposeResult = GPU2DComposeResult::Backpressure;
        return false;
    }
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::CaptureSidecar,
        DX12Perf::Counter::CaptureSidecarGpuTimeNs);
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::StructuredCompositor,
        DX12Perf::Counter::StructuredCompositorGpuTimeNs);
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::StructuredCompositor,
        DX12Perf::Counter::CompositorGpuTimeNs);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, false));

    // Which units changed, and which byte runs that collapses into, is a
    // content-generation question rather than a D3D12 one, so both backends
    // ask the same function.
    constexpr u32 logicalUnitCount = StructuredComposition::UploadUnitCount;
    const u64 planeBytes = static_cast<u64>(kStructuredPixelCount) * sizeof(u32);
    const u64 lineMetaBytes = 192u * sizeof(u32);
    const u64 captureCommandBytes =
        192u * StructuredComposition::kCaptureCommandWords * sizeof(u32);
    const StructuredComposition::StructuredUploadPlan structuredUpload =
        StructuredComposition::BuildStructuredUploadPlan(
            contentGeneration,
            slot.UploadedContentGeneration,
            slot.StructuredUploadInitialized,
            planeBytes,
            lineMetaBytes,
            captureCommandBytes);
    // Retained for the upload-shape counters below: a slot that had never
    // been written is a different event from one whose planes all changed.
    const bool fullUpload = !slot.StructuredUploadInitialized;
    const auto& dirty = structuredUpload.Dirty;
    const auto& unitOffsets = structuredUpload.UnitOffsets;
    const auto& unitSizes = structuredUpload.UnitSizes;
    const auto& ranges = structuredUpload.Ranges;
    const std::size_t rangeCount = structuredUpload.RangeCount;
    const bool captureClassificationDirty =
        dirty[StructuredComposition::CaptureCommandUnit];
    const bool uploadRequired = structuredUpload.Required();
    u64 packedBytes = 0;
    u32 routeRuns = 0;
    std::array<bool, 2> routeRunsCounted{};

    if (uploadRequired)
    {
        u32* staging = slot.StructuredMapped;
        if (!staging)
        {
            slot.Commands.Submit();
            SetRuntimeFailure("the compositor staging slot is not mapped");
            return false;
        }
        {
            DX12Perf::ScopedCpuTimer packTimer(DX12Perf::CpuMetric::ComposePack);
            for (u32 unit = 0; unit < logicalUnitCount; ++unit)
            {
                if (!dirty[unit])
                    continue;
                if (unit < 8u)
                {
                    // The per-plane path preserves the same routing contract
                    // as PackRoutedScreenPlanes(staging, screenRouting).
                    const u32 screen = unit / StructuredComposition::kPlaneCount;
                    const u32 plane = unit % StructuredComposition::kPlaneCount;
                    const StructuredComposition::ScreenPackResult screenPack =
                        StructuredComposition::PackRoutedScreenPlane(
                            staging + static_cast<std::size_t>(unit) * kStructuredPixelCount,
                            screen, plane, screenRouting);
                    if (!screenPack.Valid)
                    {
                        slot.Commands.Submit();
                        return false;
                    }
                    if (!routeRunsCounted[screen])
                    {
                        routeRuns += screenPack.RouteRuns;
                        routeRunsCounted[screen] = true;
                    }
                }
                else if (unit < 14u)
                {
                    std::memcpy(
                        staging + static_cast<std::size_t>(unit) * kStructuredPixelCount,
                        planes[unit], static_cast<std::size_t>(kStructuredPixelCount) * sizeof(u32));
                }
                else if (unit < 16u)
                {
                    std::memcpy(
                        staging + unitOffsets[unit] / sizeof(u32),
                        lineMeta[unit - 14u], 192u * sizeof(u32));
                }
                else
                {
                    u32* stagedCommands = staging + unitOffsets[unit] / sizeof(u32);
                    std::memcpy(stagedCommands, captureCommands, captureCommandBytes);
                    for (u32 line = 0; line < 192u; ++line)
                    {
                        const u32 commandBase =
                            line * StructuredComposition::kCaptureCommandWords;
                        stagedCommands[commandBase + 1u] &=
                            ~StructuredComposition::kCaptureCommandIndependent;
                        if (captureAnalysis.Independent[line] != 0u)
                        {
                            stagedCommands[commandBase + 1u] |=
                                StructuredComposition::kCaptureCommandIndependent;
                        }
                    }
                }
                packedBytes += unitSizes[unit];
            }
        }
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredPackBytes, packedBytes);
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredInputBytesPacked, packedBytes);
        DX12Perf::AddCounter(DX12Perf::Counter::StructuredRouteRuns, routeRuns);
    }

    DX12Perf::ScopedCpuTimer recordTimer(DX12Perf::CpuMetric::ComposeRecord);

    slot.Descriptors.Reset();
    BoundSrvTexture = nullptr;
    BoundSrvTable = {};
    ResetFrameSrvCache();

    if (uploadRequired)
    {
        for (std::size_t i = 0; i < rangeCount; ++i)
        {
            list->CopyBufferRegion(
                slot.StructuredInput.Get(), ranges[i].Offset,
                slot.StructuredStaging.Get(), ranges[i].Offset, ranges[i].Size);
        }
    }
    TransitionBuffer(
        list,
        slot.StructuredInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (slot.DirectTexture && slot.DirectTextureInShaderResource)
    {
        TransitionBuffer(
            list,
            slot.DirectTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        slot.DirectTextureInShaderResource = false;
    }

    // The 3D final pass was submitted immediately before this list on the same
    // queue. This cross-list UAV barrier makes those writes visible without a
    // CPU fence wait.
    InsertUavBarrier(list, FinalFBBuffer.Get());
    InsertUavBarrier(list, Capture.GetSidecarBuffer());

    ID3D12DescriptorHeap* heaps[] = { slot.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(PipelineRepo.GetRootSignature());
    if (!BindCompositionUavTable(
            list, slot.Descriptors, Gpu2D.OutputUavCpu[slotIndex]))
    {
        TransitionBuffer(
            list,
            slot.StructuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        slot.Commands.Submit();
        SetRuntimeFailure("could not bind the compositor descriptor table");
        return false;
    }

    DispatchUniform constants = MakeDispatchUniform();
    // The 3D X scroll now travels per scanline in the structured line
    // metadata, so the compositor no longer needs it as a frame-global value.
    constants.TexWidth = GPU3D.AbortFrame ? 0u : 1u;
    constants.Pad = slot.DirectTexture ? 1u : 0u;
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, false));
    list->SetPipelineState(PipelineCaptureSidecar.Get());
    u32 sidecarDispatchCount = 0;
    u32 sidecarBarrierCount = 0;
    for (u32 captureLine = 0; captureLine < 192u;)
    {
        if ((captureCommands[captureLine * StructuredComposition::kCaptureCommandWords + 1u]
                & StructuredComposition::kCaptureCommandValid) == 0u)
        {
            ++captureLine;
            continue;
        }

        if (captureAnalysis.Independent[captureLine] != 0u)
        {
            const u32 runStart = captureLine;
            do
            {
                ++captureLine;
            }
            while (captureLine < 192u
                && captureAnalysis.Independent[captureLine] != 0u);
            constants.TexHeight = runStart;
            constants.Pad = (slot.DirectTexture ? 1u : 0u) | 2u;
            SetDispatchConstants(list, constants);
            list->Dispatch(
                DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
                DivRoundUp(static_cast<u32>(ScaleFactor), 8u),
                captureLine - runStart);
            ++sidecarDispatchCount;
            InsertUavBarrier(list, Capture.GetSidecarBuffer());
            ++sidecarBarrierCount;
            continue;
        }

        constants.TexHeight = captureLine;
        constants.Pad = slot.DirectTexture ? 1u : 0u;
        SetDispatchConstants(list, constants);
        list->Dispatch(
            DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
            DivRoundUp(static_cast<u32>(ScaleFactor), 8u),
            1u);
        ++sidecarDispatchCount;
        InsertUavBarrier(list, Capture.GetSidecarBuffer());
        ++sidecarBarrierCount;
        ++captureLine;
    }
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureValidLineCount, captureAnalysis.ValidLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureIndependentLineCount,
        captureAnalysis.IndependentLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureLegacyOrderedLineCount,
        captureAnalysis.LegacyOrderedLineCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureSidecarDispatchCount, sidecarDispatchCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::CaptureSidecarBarrierCount, sidecarBarrierCount);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::CaptureSidecar, true));

    constants.TexHeight = 0u;
    constants.Pad = slot.DirectTexture ? 1u : 0u;
    SetDispatchConstants(list, constants);
    list->SetPipelineState(Gpu2D.Compositor.Get());
    list->Dispatch(
        DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ScreenHeight) * 2u, 8u),
        1u);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, true));
    if (slot.DirectTexture)
    {
        InsertUavBarrier(list, slot.DirectTexture.Get());
        TransitionBuffer(
            list,
            slot.DirectTexture.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        slot.DirectTextureInShaderResource = true;
        DX12Perf::AddCounter(DX12Perf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        InsertUavBarrier(list, slot.Composed.Get());
        DX12Perf::AddCounter(DX12Perf::Counter::FallbackCompositorBufferFrames);
    }
    TransitionBuffer(
        list,
        slot.StructuredInput.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);

    bool submitted = false;
    {
        DX12Perf::ScopedCpuTimer submitTimer(DX12Perf::CpuMetric::QueueSubmit);
        submitted = slot.Commands.Submit();
    }
    if (!submitted)
    {
        SetRuntimeFailure("compositor command submission failed");
        return false;
    }

    if (uploadRequired)
    {
        u64 uploadedBytes = 0;
        for (std::size_t i = 0; i < rangeCount; ++i)
            uploadedBytes += ranges[i].Size;
        DX12Perf::AddCounter(
            DX12Perf::Counter::StructuredInputBytesUploaded, uploadedBytes);
        DX12Perf::AddCounter(
            DX12Perf::Counter::StructuredInputCopyRegionCount,
            static_cast<u64>(rangeCount));
        DX12Perf::AddCounter(
            fullUpload
                ? DX12Perf::Counter::StructuredInputFullUploadCount
                : DX12Perf::Counter::StructuredInputPartialUploadCount);
    }
    slot.UploadedContentGeneration = contentGeneration;
    slot.StructuredUploadInitialized = true;

    {
        const auto lock = state->Ring.LockPublication();
        slot.Frame.Serial = state->Ring.PublishNext(slotIndex);
        slot.Frame.Generation = generation;
        slot.Frame.DirectContentValid = slot.DirectTexture.Get() != nullptr;
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    LastComposeResult = GPU2DComposeResult::Success;
    return true;
}

bool DX12Renderer3D::CanComposeNativeGPU2D() const noexcept
{
    return !RuntimeFailed
        && ShaderStepIdx >= ShaderStepCount
        && Context
        && Gpu2D.Native
        && ComposedOutput
        && FinalFBBuffer;
}

bool DX12Renderer3D::ComposeNativeGPU2D(
    const GPU2DNative::FrameInput& input,
    u64 generation,
    bool finalFBValid,
    const u32* expectedTop,
    const u32* expectedBottom)
{
    LastComposeResult = GPU2DComposeResult::Unavailable;
    const bool exactValidation = expectedTop != nullptr && expectedBottom != nullptr;
    const bool stageDiagnostics = GPU2DNative::StageDiagnosticsEnabled();
    const bool diagnosticReadback = exactValidation || stageDiagnostics;
    if (exactValidation && ScaleFactor != 1)
    {
        SetRuntimeFailure("native GPU2D exact validation requires scale=1");
        return false;
    }
    if (RuntimeFailed)
    {
        LastComposeResult = GPU2DComposeResult::Fatal;
        return false;
    }
    if (ShaderStepIdx < ShaderStepCount)
        return false;
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!Context || !Gpu2D.Native || !Gpu2D.Compositor
        || !state || !FinalFBBuffer)
    {
        SetRuntimeFailure("required native GPU2D resources are unavailable");
        return false;
    }

    const u32 workIndex = static_cast<u32>(
        input.Generation.Frame % kCompositorFramesInFlight);
    DX12Gpu2DOutput::ComposeWorkSlot& workSlot = state->WorkSlots[workIndex];
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const bool workSlotFencePending = !workSlot.Commands.IsIdle();
    const auto workSlotWaitStart = std::chrono::steady_clock::now();
#endif
    ID3D12GraphicsCommandList* list = workSlot.Commands.Begin();
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const auto workSlotWaitEnd = std::chrono::steady_clock::now();
    if (workSlotFencePending)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DWorkSlotFenceWaitCount);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DWorkSlotFenceWaitNs,
            static_cast<u64>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                workSlotWaitEnd - workSlotWaitStart).count()));
    }
#endif
    if (!list)
    {
        SetRuntimeFailure("native GPU2D semantic command admission failed");
        return false;
    }

    // "Is this slot free" splits cleanly in two: the ring answers published
    // and leased, and this answers the only half that needs D3D12 -- whether
    // the slot's own command list, and the work slot it was last composed
    // against, have both retired.
    struct SlotReadiness
    {
        DX12Gpu2DOutput* State;
        u32 WorkIndex;
    } readiness{state.get(), workIndex};
    const auto slotReady = +[](void* userData, u32 candidate) -> bool {
        auto* ctx = static_cast<SlotReadiness*>(userData);
        DX12Gpu2DOutput::Slot& candidateSlot = ctx->State->Slots[candidate];
        if (!candidateSlot.Commands.IsIdle())
            return false;
        if (candidateSlot.PresentationWorkSlot >= 0
            && candidateSlot.PresentationWorkSlot != static_cast<int>(ctx->WorkIndex)
            && !ctx->State->WorkSlots[static_cast<u32>(
                candidateSlot.PresentationWorkSlot)].Commands.IsIdle())
        {
            return false;
        }
        return true;
    };

    u32 slotIndex = kCompositorFramesInFlight;
    {
        const auto lock = state->Ring.LockPublication();
        const u32 candidate = state->Ring.FindFreeSlot(
            state->Ring.GetCursor(), slotReady, &readiness);
        if (candidate != RendererOutputRing::InvalidSlot)
        {
            slotIndex = candidate;
            state->Ring.SetCursorAfter(candidate);
        }
    }
    DX12Gpu2DOutput::Slot* outputSlot = slotIndex < kCompositorFramesInFlight
        ? &state->Slots[slotIndex] : nullptr;
    bool presentationAvailable = outputSlot != nullptr;
    const bool forcedPresentationStall = presentationAvailable
        && GPU2DNative::ConsumeForcedPresentationStallFrame();
    if (forcedPresentationStall)
    {
        outputSlot = nullptr;
        slotIndex = kCompositorFramesInFlight;
        presentationAvailable = false;
    }
    // Presentation backpressure is allowed to drop a visible frame, but must
    // never drop DS display-capture semantics. The persistent LCDC capture
    // mirror is emulated hardware state, not a presentation cache.
    auto& nativeStaging = workSlot.NativeStaging;
    auto& nativeInput = workSlot.NativeInput;
    auto& structuredInput = workSlot.StructuredInput;
    u32*& nativeMapped = workSlot.NativeMapped;
    auto& uploadedNativeGeneration = workSlot.UploadedNativeGeneration;
    bool& nativeUploadInitialized = workSlot.NativeUploadInitialized;
    const u64 composedOutputBytes = static_cast<u64>(ScreenWidth)
        * static_cast<u64>(ScreenHeight) * 2ull * sizeof(u32);
    const u64 diagnosticRowPitch = AlignUp(
        static_cast<u64>(ScreenWidth) * sizeof(u32),
        D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
    const u64 diagnosticReadbackBytes = std::max(
        composedOutputBytes,
        diagnosticRowPitch * static_cast<u64>(ScreenHeight) * 2ull);
    const bool hadDiagnosticReadback = workSlot.NativeReadback.Get() != nullptr;
    const bool hadFallbackComposed = workSlot.DiagnosticComposed.Get() != nullptr;
    if (diagnosticReadback
        && !workSlot.EnsureDiagnosticResources(
            *Context, diagnosticReadbackBytes,
            static_cast<u64>(kCompositionInputDwords) * sizeof(u32),
            outputSlot == nullptr, stageDiagnostics))
    {
        workSlot.Commands.Submit();
        SetRuntimeFailure("could not create lazy native GPU2D diagnostic resources");
        return false;
    }
    if (diagnosticReadback && !hadDiagnosticReadback
        && workSlot.NativeReadback.Get() != nullptr)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DDiagnosticReadbackCreateCount);
    }
    if (!hadFallbackComposed && workSlot.DiagnosticComposed.Get() != nullptr)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFallbackComposedCreateCount);
    }
    if (diagnosticReadback && outputSlot == nullptr
        && !BuildWorkDiagnosticCompositorUavDescriptor(workIndex))
    {
        workSlot.Commands.Submit();
        SetRuntimeFailure("could not build native GPU2D diagnostic descriptors");
        return false;
    }
    auto& nativeReadback = workSlot.NativeReadback;
    auto& structuredReadback = workSlot.StructuredReadback;
    const u32 descriptorIndex = workIndex;
    u64 rendererSerial = 0;
    if (outputSlot)
    {
        const auto lock = state->Ring.LockPublication();
        rendererSerial = state->Ring.PeekNextSerial();
    }
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DLogical,
        DX12Perf::Counter::NativeGPU2DLogicalGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DCapture,
        DX12Perf::Counter::NativeGPU2DCaptureGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DResolve,
        DX12Perf::Counter::NativeGPU2DResolveGpuTimeNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DRaw,
        DX12Perf::Counter::NativeGPU2DObjRawGpuNs);
    RecordDX12GpuMetric(
        workSlot.Commands, GpuMetric::NativeGPU2DResolve,
        DX12Perf::Counter::CompositorGpuTimeNs);

    u64 pendingCompletionValue = Provenance.PeekNextSubmissionSerial();
    if (pendingCompletionValue == 0u)
        pendingCompletionValue = 1u;
    const NativeCaptureStateIdentity pendingCaptureIdentity{
        true,
        CaptureOwner::NativeDX12,
        Provenance.GetEpoch(),
        input.Generation.Frame,
        input.Generation.CaptureGeneration,
        pendingCompletionValue,
    };
    HighResCaptureProvenance.BeginFrame(
        input, pendingCaptureIdentity, static_cast<u32>(ScaleFactor));
    const GPU2DNative::UploadDecision uploadDecision =
        GPU2DNative::DetermineUploadDecision(
            nativeUploadInitialized, Provenance.GetEpoch(), Provenance.GetSemanticEpoch(),
            Provenance.GetSemanticFrame(), Provenance.GetSemanticCaptureGeneration(),
            input.Generation);
    const bool semanticFrameContiguous =
        uploadDecision.SemanticFrameContiguous;
    const bool semanticCaptureGenerationRegressed =
        uploadDecision.CaptureGenerationRegressed;
    const bool fullNativeUpload = uploadDecision.RequiresFullUpload();
    const GPU2DNative::SemanticLinePlan semanticLinePlan =
        GPU2DNative::BuildSemanticLinePlan(
            input, workSlot.SemanticLines,
            fullNativeUpload || input.CaptureEnable != 0u);
    DX12Perf::SetCounter(
        DX12Perf::Counter::NativeGPU2DWorkgroupWidth, 256u);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRowsDirty,
        semanticLinePlan.DirtyRows);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRowsReused,
        semanticLinePlan.ReusedRows);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DSemanticRunCount,
        semanticLinePlan.RunCount);
    DX12Perf::AddCounter(
        DX12Perf::Counter::NativeGPU2DObjPrepareGroups,
        semanticLinePlan.DirtyRows);
    const GPU2DNative::UploadPlan uploadPlan = GPU2DNative::BuildUploadPlan(
        input, uploadedNativeGeneration, fullNativeUpload);
    DX12Perf::AddCounter(
        fullNativeUpload
            ? DX12Perf::Counter::NativeGPU2DFullUploadFrames
            : DX12Perf::Counter::NativeGPU2DPartialUploadFrames);
    DX12Perf::AddCounter(
        fullNativeUpload
            ? DX12Perf::Counter::NativeGPU2DFullUploadBytes
            : DX12Perf::Counter::NativeGPU2DPartialUploadBytes,
        uploadPlan.TotalBytes);
    switch (uploadDecision.Reason)
    {
    case GPU2DNative::FullUploadReason::FirstUse:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadFirstUseCount);
        break;
    case GPU2DNative::FullUploadReason::EpochChange:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadEpochChangeCount);
        break;
    case GPU2DNative::FullUploadReason::SemanticFrameGap:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadSemanticFrameGapCount);
        break;
    case GPU2DNative::FullUploadReason::CaptureGenerationRegression:
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DFullUploadCaptureRegressionCount);
        break;
    case GPU2DNative::FullUploadReason::None:
        break;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packStartNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
    u32* staging = nativeMapped;
    bool packedNativeInput = staging != nullptr;
    if (packedNativeInput)
    {
        packedNativeInput = fullNativeUpload
            ? GPU2DNative::PackFrame(input, staging, GPU2DNative::PackedFrameWords)
            : GPU2DNative::PackFrameRanges(
                input, staging, GPU2DNative::PackedFrameWords, uploadPlan);
    }
    if (packedNativeInput)
    {
        packedNativeInput = GPU2DNative::PackHighResCaptureProvenance(
            staging, GPU2DNative::PackedFrameWords,
            HighResCaptureProvenance.States(), input, pendingCompletionValue);
    }
    if (!packedNativeInput)
    {
        HighResCaptureProvenance.AbortFrame();
        workSlot.Commands.Submit();
        SetRuntimeFailure("the native GPU2D input staging upload failed");
        return false;
    }
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    const u64 packEndNs = static_cast<u64>(std::chrono::duration_cast<
        std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DPackNs,
        packEndNs - packStartNs);
#endif
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBlocksScanned,
        input.Recorder.BlocksScanned);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBytesScanned,
        input.Recorder.BytesScanned);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBlocksCopied,
        input.Recorder.BlocksCopied);
    DX12Perf::AddCounter(DX12Perf::Counter::RecorderBytesCopied,
        input.Recorder.BytesCopied);
    DX12Perf::AddCounter(DX12Perf::Counter::CaptureCPU2DLines,
        input.Recorder.CaptureCPU2DLines);
    DX12Perf::AddCounter(DX12Perf::Counter::CaptureCPU2DNs,
        input.Recorder.CaptureCPU2DNs);
    DX12Perf::AddCounter(DX12Perf::Counter::GPU2DRecorderNs,
        input.Recorder.GPU2DRecorderNs);
    DX12Perf::AddCounter(DX12Perf::Counter::TimelineRowDedupNs,
        input.Recorder.TimelineRowDedupNs);
    DX12Perf::AddCounter(DX12Perf::Counter::SpriteTimelineRowDedupNs,
        input.Recorder.SpriteTimelineRowDedupNs);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DInputPackBytes,
        uploadPlan.TotalBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DVRAMUploadBytes,
        uploadPlan.EngineMemoryBytes + uploadPlan.FIFOBytes
            + uploadPlan.LCDVRAMBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DPaletteUploadBytes,
        uploadPlan.PaletteBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DOAMUploadBytes,
        uploadPlan.OAMBytes);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadWordCalls,
        input.Recorder.MappedReadWordCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadFastPathCalls,
        input.Recorder.MappedReadFastPathCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::MappedReadSlowPathCalls,
        input.Recorder.MappedReadSlowPathCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeCaptureHistoryScanLines,
        input.Recorder.NativeCaptureHistoryScanLines);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingBuildCalls,
        input.Recorder.NativeMappingBuildCalls);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingRowsUploaded,
        input.Recorder.NativeMappingRowsUploaded);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeMappingBytesUploaded,
        input.Recorder.NativeMappingBytesUploaded);
    DX12Perf::AddCounter(DX12Perf::Counter::BGOverlayFastPath,
        input.Recorder.BGOverlayFastPath);
    DX12Perf::AddCounter(DX12Perf::Counter::BGOverlaySlowPath,
        input.Recorder.BGOverlaySlowPath);
    DX12Perf::AddCounter(DX12Perf::Counter::OBJOverlayFastPath,
        input.Recorder.OBJOverlayFastPath);
    DX12Perf::AddCounter(DX12Perf::Counter::OBJOverlaySlowPath,
        input.Recorder.OBJOverlaySlowPath);

    workSlot.Descriptors.Reset();
    BoundSrvTexture = nullptr;
    BoundSrvTable = {};
    ResetFrameSrvCache();
    if (nativeUploadInitialized)
    {
        TransitionBuffer(
            list,
            nativeInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
    }
    for (u32 i = 0; i < uploadPlan.Count; ++i)
    {
        const GPU2DNative::DirtyRange& range = uploadPlan.Ranges[i];
        list->CopyBufferRegion(
            nativeInput.Get(), range.Offset,
            nativeStaging.Get(), range.Offset, range.Size);
    }
    TransitionBuffer(
        list,
        nativeInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // The tail of BlendStateBuffer is the persistent GPU LCDC capture mirror.
    // Synchronize only changed serialized LCD ranges from the mapped input;
    // native capture commands below update it in scanline order.
    InsertUavBarrier(list, BlendStateBuffer.Get());
    TransitionBuffer(
        list,
        BlendStateBuffer.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);
    const u32 lcdBegin = GPU2DNative::PackedLCDVRAMBase * sizeof(u32);
    const u32 lcdEnd = GPU2DNative::PackedRouteBase * sizeof(u32);
    const u64 captureBase = (static_cast<u64>(ScreenWidth)
        * static_cast<u64>(ScreenHeight)) * sizeof(u32);
    const bool mirrorNeedsFullCopy = Provenance.MirrorNeedsFullCopy()
        || !semanticFrameContiguous
        || semanticCaptureGenerationRegressed;
    const auto copyCoherentCaptureRange = [&](u32 requestedBegin, u32 requestedEnd) {
        if (requestedEnd <= requestedBegin)
            return;
        for (u32 physicalIndex = 0;
            physicalIndex < CapturePhysicalBlockCount;
            ++physicalIndex)
        {
            const u32 blockBegin = lcdBegin
                + physicalIndex * CapturePhysicalBlockBytes;
            const u32 blockEnd = blockBegin + CapturePhysicalBlockBytes;
            const u32 begin = std::max(requestedBegin, blockBegin);
            const u32 end = std::min(requestedEnd, blockEnd);
            if (end <= begin)
                continue;

            const bool nativeOwner = IsNativeCaptureOwner(
                input.LCDVRAMProvenance[physicalIndex].Owner);
            if (nativeOwner)
            {
                // A native-owned block survives the FrameRecorder rollover;
                // copying its old CPU bytes would erase the GPU capture
                // mirror before the next semantic dispatch consumes it.
                GPU2DNative::RecordNativeOwnedCaptureCopySkipped();
                continue;
            }

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
            // Keep this assertion adjacent to the actual host upload. Any
            // future refactor that lets a native-owned block reach this point
            // would replay stale CPU VRAM into the persistent capture mirror.
            assert(!nativeOwner);
            if (nativeOwner)
            {
                GPU2DNative::RecordNativeOwnedHostReupload();
                continue;
            }
#endif

            list->CopyBufferRegion(
                BlendStateBuffer.Get(), captureBase + (begin - lcdBegin),
                nativeStaging.Get(), begin, end - begin);
        }
    };
    if (mirrorNeedsFullCopy)
        copyCoherentCaptureRange(lcdBegin, lcdEnd);
    else
    {
        for (u32 i = 0; i < input.DirtyRangeCount; ++i)
        {
            const GPU2DNative::DirtyRange& range = input.DirtyRanges[i];
            const u32 begin = std::max(range.Offset, lcdBegin);
            const u32 end = std::min(range.Offset + range.Size, lcdEnd);
            if (end <= begin)
                continue;
            copyCoherentCaptureRange(begin, end);
        }
    }
    TransitionBuffer(
        list,
        BlendStateBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (outputSlot && outputSlot->DirectTexture
        && outputSlot->DirectTextureInShaderResource)
    {
        TransitionBuffer(
            list,
            outputSlot->DirectTexture.Get(),
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        outputSlot->DirectTextureInShaderResource = false;
    }

    InsertUavBarrier(list, FinalFBBuffer.Get());
    InsertUavBarrier(list, Capture.GetSidecarBuffer());

    // The logical Stage A owns the structured output resource for this slot.
    // It is kept separate from NativeInput so the shader can read the packed
    // frame while filling the first fourteen compositor planes.
    TransitionBuffer(
        list,
        structuredInput.Get(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { workSlot.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(PipelineRepo.GetRootSignature());
    if (!BindCompositionUavTable(
            list, workSlot.Descriptors, Gpu2D.WorkNativeUavCpu[workIndex]))
    {
        TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        workSlot.Commands.Submit();
        SetRuntimeFailure("could not bind the native logical GPU2D descriptor table");
        return false;
    }

    DispatchUniform constants = MakeDispatchUniform();
    constants.TexWidth = finalFBValid ? 1u : 0u;
    constants.Pad = 16u;
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, false));
    if (input.CaptureEnable != 0u)
    {
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, false));
        if (!Gpu2D.NativeCapture)
        {
            SetRuntimeFailure("native GPU2D capture pipeline is unavailable");
            return false;
        }
        const bool batchIndependentCapture =
            GPU2DNative::CanBatchIndependentCaptureFrame(input, finalFBValid);
        if (batchIndependentCapture)
        {
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureRunCount);
            // The destination remains LCDC-only for the full frame, so it
            // cannot feed its own writes back through BG/OBJ. Submit all
            // logical lines before one frame-wide capture dispatch.
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalFrame(input);
            list->SetPipelineState(Gpu2D.Native.Get());
            constants.InterpSpanCount = 0u;
            constants.Pad = 32u
                | (fuseObjRawLogical ? (16u | 64u) : 0u);
            SetDispatchConstants(list, constants);
            list->Dispatch(1u, 384u, 1u);
            if (!fuseObjRawLogical)
            {
                InsertUavBarrier(list, BlendStateBuffer.Get());
                constants.Pad = 16u;
                SetDispatchConstants(list, constants);
                list->Dispatch(1u, 384u, 1u);
            }

            InsertUavBarrier(list, structuredInput.Get());

            constants.Pad = 4u | 128u;
            SetDispatchConstants(list, constants);
            list->SetPipelineState(Gpu2D.NativeCapture.Get());
            list->Dispatch(
                DivRoundUp(static_cast<u32>(ScreenWidth), 256u),
                GPU2DNative::ScreenHeight * static_cast<u32>(ScaleFactor), 1u);
            ID3D12Resource* captureOutputs[2] = {
                BlendStateBuffer.Get(), Capture.GetSidecarBuffer()};
            InsertUavBarriers(list, captureOutputs, 2u);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureDispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureBarrierCount, 1u);
            DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DDispatchCount,
                fuseObjRawLogical ? 2u : 3u);
        }
        else
        {
            const GPU2DNative::CaptureRunPlan capturePlan =
                GPU2DNative::BuildCaptureRunPlan(input, finalFBValid);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureRunCount,
                capturePlan.RunCount);
            u64 dispatchCount = 0u;
            u64 captureDispatchCount = 0u;
            u64 captureBarrierCount = 0u;
            for (u32 runIndex = 0u; runIndex < capturePlan.RunCount; ++runIndex)
            {
                const GPU2DNative::CaptureLineRun& run =
                    capturePlan.Runs[runIndex];
                const bool fuseObjRawLogical =
                    GPU2DNative::CanFuseObjRawCaptureRun(input, run);
                if (run.Independent)
                {
                    list->SetPipelineState(Gpu2D.Native.Get());
                    for (u32 screen = 0u; screen < 2u; ++screen)
                    {
                        constants.InterpSpanCount =
                            screen * GPU2DNative::ScreenHeight + run.LineBase;
                        constants.Pad = 32u | 256u
                            | (fuseObjRawLogical ? (16u | 64u) : 0u);
                        SetDispatchConstants(list, constants);
                        list->Dispatch(1u, run.LineCount, 1u);
                        ++dispatchCount;
                    }
                    if (!fuseObjRawLogical)
                    {
                        InsertUavBarrier(list, BlendStateBuffer.Get());
                        ++captureBarrierCount;
                        for (u32 screen = 0u; screen < 2u; ++screen)
                        {
                            constants.InterpSpanCount =
                                screen * GPU2DNative::ScreenHeight + run.LineBase;
                            constants.Pad = 16u | 256u;
                            SetDispatchConstants(list, constants);
                            list->Dispatch(1u, run.LineCount, 1u);
                            ++dispatchCount;
                        }
                    }

                    ID3D12Resource* logicalOutputs[2] = {
                        structuredInput.Get(), BlendStateBuffer.Get()};
                    InsertUavBarriers(list, logicalOutputs, 2u);
                    ++captureBarrierCount;
                    constants.InterpSpanCount = run.LineBase;
                    constants.Pad = 4u | 128u | 512u;
                    SetDispatchConstants(list, constants);
                    list->SetPipelineState(Gpu2D.NativeCapture.Get());
                    list->Dispatch(
                        DivRoundUp(static_cast<u32>(ScreenWidth), 256u),
                        run.LineCount * static_cast<u32>(ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    ID3D12Resource* captureOutputs[2] = {
                        BlendStateBuffer.Get(), Capture.GetSidecarBuffer()};
                    InsertUavBarriers(list, captureOutputs, 2u);
                    ++captureBarrierCount;
                    continue;
                }

                const u32 lineNumber = run.LineBase;
                const bool captureLineActive =
                    GPU2DNative::IsEffectiveCaptureLine(input, lineNumber);
                constants.InterpSpanCount = lineNumber;
                constants.Pad = 32u | 8u
                    | (fuseObjRawLogical ? (16u | 64u) : 0u);
                SetDispatchConstants(list, constants);
                list->SetPipelineState(Gpu2D.Native.Get());
                list->Dispatch(1u, 2u, 1u);
                ++dispatchCount;
                if (!fuseObjRawLogical)
                {
                    InsertUavBarrier(list, BlendStateBuffer.Get());
                    ++captureBarrierCount;
                    constants.Pad = 16u | 8u;
                    SetDispatchConstants(list, constants);
                    list->Dispatch(1u, 2u, 1u);
                    ++dispatchCount;
                }
                if (captureLineActive)
                {
                    ID3D12Resource* logicalOutputs[2] = {
                        structuredInput.Get(), BlendStateBuffer.Get()};
                    InsertUavBarriers(list, logicalOutputs, 2u);
                    ++captureBarrierCount;
                    constants.Pad = 4u;
                    SetDispatchConstants(list, constants);
                    list->SetPipelineState(Gpu2D.NativeCapture.Get());
                    list->Dispatch(
                        DivRoundUp(static_cast<u32>(ScreenWidth), 256u),
                        static_cast<u32>(ScaleFactor), 1u);
                    ++dispatchCount;
                    ++captureDispatchCount;
                    ID3D12Resource* captureOutputs[2] = {
                        BlendStateBuffer.Get(), Capture.GetSidecarBuffer()};
                    InsertUavBarriers(list, captureOutputs, 2u);
                    ++captureBarrierCount;
                }
            }
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DDispatchCount, dispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureDispatchCount,
                captureDispatchCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DCaptureBarrierCount,
                captureBarrierCount);
        }
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DCapture, true));
    }
    else
    {
        list->SetPipelineState(Gpu2D.Native.Get());
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, false));
        u64 dispatchCount = 0u;
        for (u32 runIndex = 0u;
            runIndex < semanticLinePlan.RunCount; ++runIndex)
        {
            const GPU2DNative::SemanticLineRun& run =
                semanticLinePlan.Runs[runIndex];
            const bool fuseObjRawLogical =
                GPU2DNative::CanFuseObjRawLogicalRun(input, run);
            constants.InterpSpanCount = run.RowBase;
            constants.Pad = 32u | 256u
                | (fuseObjRawLogical ? (16u | 64u) : 0u);
            SetDispatchConstants(list, constants);
            list->Dispatch(1u, run.RowCount, 1u);
            ++dispatchCount;
            if (!fuseObjRawLogical)
            {
                InsertUavBarrier(list, BlendStateBuffer.Get());
                constants.Pad = 16u | 256u;
                SetDispatchConstants(list, constants);
                list->Dispatch(1u, run.RowCount, 1u);
                ++dispatchCount;
            }
        }
        workSlot.Commands.WriteTimestamp(
            GpuMetricQueryIndex(GpuMetric::NativeGPU2DRaw, true));
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DDispatchCount, dispatchCount);
    }
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DLogical, true));

    InsertUavBarrier(list, structuredInput.Get());
    InsertUavBarrier(list, Capture.GetSidecarBuffer());
    if (stageDiagnostics)
    {
        // Developer-only Stage A readback.  Keep the structured planes in a
        // UAV state for the compositor after the copy; shipping never creates
        // or touches this readback resource.
        TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(
            structuredReadback.Get(), 0,
            structuredInput.Get(), 0,
            static_cast<u64>(kCompositionInputDwords) * sizeof(u32));
        TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    bool compositorDirectOutput = false;
    bool directOutputReadback = false;
    // A semantic-only frame has no visible consumer. Stage A and every
    // capture/provenance transition above still execute, while the discarded
    // Stage B compositor is skipped unless a developer diagnostic requested
    // an observable result.
    if (outputSlot || diagnosticReadback)
    {
    auto& composedOutput = outputSlot
        ? outputSlot->Composed : workSlot.DiagnosticComposed;
    compositorDirectOutput = outputSlot && outputSlot->DirectTexture
        && (!diagnosticReadback
            || (GPU2DNative::DirectOutputDiagnosticsEnabled() && ScaleFactor == 1));
    if (!BindCompositionUavTable(
            list, workSlot.Descriptors,
            Gpu2D.WorkOutputUavCpu[workIndex * 4u
                + (outputSlot ? slotIndex : 3u)]))
    {
        TransitionBuffer(
            list,
            structuredInput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_DEST);
        workSlot.Commands.Submit();
        SetRuntimeFailure("could not bind the structured compositor descriptor table");
        return false;
    }
    constants.InterpSpanCount = 0u;
    constants.Pad = compositorDirectOutput ? 1u : 0u;
    SetDispatchConstants(list, constants);
    list->SetPipelineState(Gpu2D.Compositor.Get());
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, false));
    list->Dispatch(
        DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ScreenHeight) * 2u, 8u), 1u);
    workSlot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::NativeGPU2DResolve, true));

    directOutputReadback = compositorDirectOutput
        && diagnosticReadback
        && GPU2DNative::DirectOutputDiagnosticsEnabled();
    if (compositorDirectOutput)
    {
        InsertUavBarrier(list, outputSlot->DirectTexture.Get());
        if (directOutputReadback)
        {
            TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_COPY_SOURCE);
            const UINT rowPitch = static_cast<UINT>(
                AlignUp(static_cast<u64>(ScreenWidth) * sizeof(u32),
                    D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
            const u64 screenBytes = static_cast<u64>(rowPitch)
                * static_cast<u64>(ScreenHeight);
            for (UINT screen = 0; screen < 2u; ++screen)
            {
                D3D12_TEXTURE_COPY_LOCATION destination{};
                destination.pResource = nativeReadback.Get();
                destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                destination.PlacedFootprint.Offset = screenBytes * screen;
                destination.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                destination.PlacedFootprint.Footprint.Width = static_cast<UINT>(ScreenWidth);
                destination.PlacedFootprint.Footprint.Height = static_cast<UINT>(ScreenHeight);
                destination.PlacedFootprint.Footprint.Depth = 1;
                destination.PlacedFootprint.Footprint.RowPitch = rowPitch;

                D3D12_TEXTURE_COPY_LOCATION source{};
                source.pResource = outputSlot->DirectTexture.Get();
                source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                source.SubresourceIndex = screen;
                list->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
            }
            TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_COPY_SOURCE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        else
        {
            TransitionBuffer(
                list,
                outputSlot->DirectTexture.Get(),
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        outputSlot->DirectTextureInShaderResource = true;
        DX12Perf::AddCounter(DX12Perf::Counter::DirectCompositorImageFrames);
    }
    else
    {
        InsertUavBarrier(list, composedOutput.Get());
        DX12Perf::AddCounter(DX12Perf::Counter::FallbackCompositorBufferFrames);
    }

    if (diagnosticReadback && !directOutputReadback)
    {
        InsertUavBarrier(list, composedOutput.Get());
        TransitionBuffer(
            list,
            composedOutput.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        list->CopyBufferRegion(
            nativeReadback.Get(), 0,
            composedOutput.Get(), 0,
            composedOutputBytes);
        TransitionBuffer(
            list,
            composedOutput.Get(),
            D3D12_RESOURCE_STATE_COPY_SOURCE,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    }
    }
    TransitionBuffer(
        list,
        structuredInput.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_DEST);

    if (!workSlot.Commands.Submit())
    {
        HighResCaptureProvenance.AbortFrame();
        SetRuntimeFailure("native GPU2D command submission failed");
        return false;
    }
    // Each semantic slot owns a separate DX12 command context and local fence.
    // Its fence values are therefore not comparable across slots.  The
    // renderer-global serial is the provenance identity; direct queue ordering
    // guarantees that a later scoped capture copy observes this submission.
    Provenance.CommitSubmissionSerial(pendingCompletionValue);
    Provenance.SetCompletionValue(pendingCompletionValue);
    HighResCaptureProvenance.CommitFrame(pendingCaptureIdentity);

    if (diagnosticReadback)
    {
        workSlot.Commands.WaitIdle();
        if (!workSlot.NativeReadbackMapped)
        {
            SetRuntimeFailure("native GPU2D exact readback mapping failed");
            return false;
        }
        const u8* source = workSlot.NativeReadbackMapped;
        const u32 representative = GPU2DNative::RepresentativeSubpixel(
            static_cast<u32>(ScaleFactor));
        const u64 sourceRowPitch = directOutputReadback
            ? diagnosticRowPitch
            : static_cast<u64>(ScreenWidth) * sizeof(u32);
        const u64 sourceScreenBytes = sourceRowPitch
            * static_cast<u64>(ScreenHeight);
        std::unique_ptr<u32[]> actual(new u32[2u * GPU2DNative::ScreenPixelCount]);
        for (u32 screen = 0; screen < 2u; ++screen)
        {
            for (u32 y = 0; y < GPU2DNative::ScreenHeight; ++y)
            {
                for (u32 x = 0; x < GPU2DNative::ScreenWidth; ++x)
                {
                    const u64 sourceOffset = static_cast<u64>(screen)
                            * sourceScreenBytes
                        + static_cast<u64>(y * static_cast<u32>(ScaleFactor)
                                + representative)
                            * sourceRowPitch
                        + static_cast<u64>(x * static_cast<u32>(ScaleFactor)
                                + representative)
                            * sizeof(u32);
                    u32 bgra8 = 0;
                    std::memcpy(&bgra8, source + sourceOffset, sizeof(u32));
                    const u32 red = directOutputReadback
                        ? (bgra8 & 0xFFu) : (bgra8 >> 16u);
                    const u32 green = (bgra8 >> 8u) & 0xFFu;
                    const u32 blue = directOutputReadback
                        ? (bgra8 >> 16u) : (bgra8 & 0xFFu);
                    const u32 index = screen * GPU2DNative::ScreenPixelCount
                        + y * GPU2DNative::ScreenWidth + x;
                    actual[index] = ((red & 0xFFu) >> 2u)
                        | (((green >> 2u) & 0x3Fu) << 8u)
                        | (((blue >> 2u) & 0x3Fu) << 16u);
                }
            }
        }
        DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DReadbackCount);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DReadbackBytes,
            directOutputReadback ? diagnosticReadbackBytes : composedOutputBytes);

        if (stageDiagnostics)
        {
            if (!workSlot.StructuredReadbackMapped)
            {
                SetRuntimeFailure("native GPU2D Stage A readback mapping failed");
                return false;
            }
            GPU2DNative::LogStageSnapshot(
                "DX12", input.Generation.Frame, input.Generation.Frame,
                rendererSerial, generation, descriptorIndex, input,
                workSlot.StructuredReadbackMapped,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount,
                directOutputReadback ? "direct_image" : "composed_buffer",
                expectedTop, expectedBottom);
        }

        if (exactValidation)
        {
            const GPU2DNative::CompareResult result = GPU2DNative::CompareExact(
                expectedTop, expectedBottom,
                actual.get(), actual.get() + GPU2DNative::ScreenPixelCount);
            DX12Perf::AddCounter(
                DX12Perf::Counter::NativeGPU2DMismatchCount,
                result.TotalMismatchCount);
            if (!result.Exact())
            {
                if (result.SampleCount != 0u)
                {
                    const GPU2DNative::Mismatch& sample = result.Samples[0];
                    const u32 engine = input.ScreenSource[
                        sample.Screen * GPU2DNative::ScreenHeight + sample.Y] & 1u;
                    const GPU2DNative::LineState& state = input.Lines[
                        engine * GPU2DNative::ScreenHeight + sample.Y];
                    Platform::Log(Platform::LogLevel::Error,
                        "DX12 native GPU2D exact mismatch frame=%llu total=%u top=%u bottom=%u "
                        "first=screen%u(%u,%u) expected=0x%08X actual=0x%08X engine=%u "
                        "DispCnt=0x%08X Layer=0x%08X BGCnt0=0x%08X WinRegs=0x%08X "
                        "BlendCnt=0x%08X Master=0x%08X Screens=%u/%u LineScreens=%u "
                        "ExpectedRow8=%08X/%08X Capture=0x%08X\n",
                        static_cast<unsigned long long>(generation),
                        result.TotalMismatchCount, result.TopMismatchCount,
                        result.BottomMismatchCount, sample.Screen, sample.X, sample.Y,
                        sample.Expected, sample.Actual, engine, state.DispCnt,
                        state.LayerEnable, state.BGCnt[0], state.WinRegs,
                        state.BlendCnt, state.MasterBrightness, input.ScreensEnabled,
                        input.ScreenSwap, state.ScreensEnabled,
                        expectedTop[8u * GPU2DNative::ScreenWidth],
                        expectedBottom[8u * GPU2DNative::ScreenWidth], state.CaptureCnt);

                }
                SetRuntimeFailure("native GPU2D exact differential mismatch");
                return false;
            }
        }
    }

    nativeUploadInitialized = true;
    uploadedNativeGeneration = input.Generation;
    Provenance.RecordSemanticSubmission(
        input.Generation.Frame, input.Generation.CaptureGeneration);
    DX12Perf::AddCounter(DX12Perf::Counter::NativeGPU2DFrames);
    GPU2DNative::LogSemanticIdentity(
        "DX12", input.Generation.Frame, input.Generation.CaptureGeneration,
        Provenance.GetEpoch(), outputSlot != nullptr, forcedPresentationStall,
        mirrorNeedsFullCopy,
        outputSlot != nullptr ? slotIndex : kCompositorFramesInFlight);
    if (!outputSlot)
    {
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DPresentationBackpressureFrames);
        DX12Perf::AddCounter(
            DX12Perf::Counter::NativeGPU2DSemanticOnlyFrames);
        LastComposeResult = GPU2DComposeResult::SemanticOnly;
        return false;
    }
    {
        const auto lock = state->Ring.LockPublication();
        outputSlot->Frame.Serial = rendererSerial;
        outputSlot->Frame.Generation = generation;
        outputSlot->Frame.Epoch = Provenance.GetEpoch();
        outputSlot->Frame.DirectContentValid = compositorDirectOutput;
        outputSlot->PresentationWorkSlot = static_cast<int>(workIndex);
        state->Ring.PublishReserved(slotIndex);
        ComposedGeneration = generation;
        PublishedOutputGeneration = generation;
        ComposedOutputValid = true;
    }
    if (stageDiagnostics)
    {
        GPU2DNative::LogPresentedIdentity(
            "DX12", input.Generation.Frame, outputSlot->Frame.Serial,
            generation, outputSlot->Frame.Epoch, slotIndex);
    }
    LastComposeResult = GPU2DComposeResult::Success;
    return true;
}

bool DX12Renderer3D::BuildFrameUavDescriptors()
{
    FrameUavDescriptors.Reset();
    D3D12_GPU_DESCRIPTOR_HANDLE ignored{};
    if (!FrameUavDescriptors.Allocate(kUavTableSize, FrameUavCpu, ignored))
        return false;

    const u32 pixels = static_cast<u32>(ScreenWidth) * static_cast<u32>(ScreenHeight);
    const u32 resultWinnerElements = ScaleFactor == 1 ? pixels * 2u : 1u;
    const u32 tileElements = static_cast<u32>(TileSize) * static_cast<u32>(TileSize)
        * static_cast<u32>(MaxWorkTiles);
    const u32 binResultDwords = static_cast<u32>(
        (sizeof(BinResultHeader)
            + static_cast<u64>(TilesPerLine) * TileLines * CoarseBinStride * 4ull
            + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull
            + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull) / 4ull);

    UavDescriptorEntry entries[kUavTableSize] = {};
    entries[DX12UavSlot::DispatchInput] =
        {ResultBuffer.Get(), pixels * 3u * 2u, 4, false};
    entries[DX12UavSlot::FinalFB] = {FinalFBBuffer.Get(), pixels, 4, false};
    entries[DX12UavSlot::TileColor] = {TileBuffers[0].Get(), tileElements, 4, false};
    entries[DX12UavSlot::TileDepth] = {TileBuffers[1].Get(), tileElements, 4, false};
    entries[DX12UavSlot::TileAttr] = {TileBuffers[2].Get(), tileElements, 4, false};
    entries[DX12UavSlot::BinResult] = {BinResultBuffer.Get(), binResultDwords, 4, true};
    entries[DX12UavSlot::WorkDesc] =
        {WorkDescBuffer.Get(), static_cast<u32>(MaxWorkTiles) * 2u, 8, false};
    entries[DX12UavSlot::XSpanSetup] =
        {XSpanSetupBuffer.Get(), static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false};
    entries[DX12UavSlot::DispatchOutput] = {ResolveBuffer.Get(), 256u * 192u, 4, false};
    entries[DX12UavSlot::CaptureSidecar] =
        {Capture.GetSidecarBuffer(), 8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor), 4, false};
    entries[DX12UavSlot::BlendState] =
        {BlendStateBuffer.Get(), pixels + static_cast<u32>(kNativeCaptureWords) + kNativeObjRawWords, 4, false};
    entries[DX12UavSlot::ResultWinner] =
        {ResultWinnerBuffer.Get(), resultWinnerElements, 4, false};
    entries[DX12UavSlot::IndirectArgs] =
        {IndirectArgsBuffer.Get(), static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true};
    entries[DX12UavSlot::DirectOutput] =
        {DirectOutputDummy.Get(), 0, 0, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 2};
    return CreateUavDescriptorTable(
        Context->GetDevice(), FrameUavDescriptors.GetIncrement(), FrameUavCpu,
        entries, kUavTableSize);
}

bool DX12Renderer3D::BuildCompositorUavDescriptors()
{
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!state)
        return false;

    Gpu2D.OutputUav.Reset();
    Gpu2D.WorkOutputUav.Reset();
    Gpu2D.WorkNativeUav.Reset();
    D3D12_CPU_DESCRIPTOR_HANDLE base{};
    D3D12_CPU_DESCRIPTOR_HANDLE workCompositorBase{};
    D3D12_CPU_DESCRIPTOR_HANDLE workNativeBase{};
    D3D12_GPU_DESCRIPTOR_HANDLE ignored{};
    if (!Gpu2D.OutputUav.Allocate(
            kUavTableSize * kCompositorFramesInFlight, base, ignored))
        return false;
    if (!Gpu2D.WorkOutputUav.Allocate(
            kUavTableSize * kCompositorFramesInFlight * 4u,
            workCompositorBase, ignored))
        return false;
    if (!Gpu2D.WorkNativeUav.Allocate(
            kUavTableSize * kCompositorFramesInFlight, workNativeBase, ignored))
        return false;

    const u32 pixels = static_cast<u32>(ScreenWidth) * static_cast<u32>(ScreenHeight);
    const u32 resultWinnerElements = ScaleFactor == 1 ? pixels * 2u : 1u;
    const u32 tileElements = static_cast<u32>(TileSize) * static_cast<u32>(TileSize)
        * static_cast<u32>(MaxWorkTiles);
    const u32 binResultDwords = static_cast<u32>(
        (sizeof(BinResultHeader)
            + static_cast<u64>(TilesPerLine) * TileLines * CoarseBinStride * 4ull
            + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull
            + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 4ull) / 4ull);
    const u32 increment = Gpu2D.OutputUav.GetIncrement();
    Gpu2D.OutputUavCpu.fill(D3D12_CPU_DESCRIPTOR_HANDLE{});
    Gpu2D.WorkOutputUavCpu.fill(D3D12_CPU_DESCRIPTOR_HANDLE{});
    Gpu2D.WorkNativeUavCpu.fill(D3D12_CPU_DESCRIPTOR_HANDLE{});

    const auto buildCompositorTable = [&](D3D12_CPU_DESCRIPTOR_HANDLE destination,
        ID3D12Resource* structured, ID3D12Resource* composed,
        ID3D12Resource* directTexture) {
        UavDescriptorEntry entries[kUavTableSize] = {};
        entries[DX12UavSlot::DispatchInput] =
            {structured, kCompositionInputDwords, 4, false};
        entries[DX12UavSlot::FinalFB] = {FinalFBBuffer.Get(), pixels, 4, false};
        entries[DX12UavSlot::TileColor] =
            {TileBuffers[0].Get(), tileElements, 4, false};
        entries[DX12UavSlot::TileDepth] =
            {TileBuffers[1].Get(), tileElements, 4, false};
        entries[DX12UavSlot::TileAttr] = {TileBuffers[2].Get(), tileElements, 4, false};
        entries[DX12UavSlot::BinResult] =
            {BinResultBuffer.Get(), binResultDwords, 4, true};
        entries[DX12UavSlot::WorkDesc] =
            {WorkDescBuffer.Get(), static_cast<u32>(MaxWorkTiles) * 2u, 8, false};
        entries[DX12UavSlot::XSpanSetup] =
            {XSpanSetupBuffer.Get(), static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false};
        entries[DX12UavSlot::DispatchOutput] = {composed, pixels * 2u, 4, false};
        entries[DX12UavSlot::CaptureSidecar] =
            {Capture.GetSidecarBuffer(), 8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor), 4, false};
        entries[DX12UavSlot::BlendState] =
            {BlendStateBuffer.Get(), pixels + static_cast<u32>(kNativeCaptureWords) + kNativeObjRawWords, 4, false};
        entries[DX12UavSlot::ResultWinner] =
            {ResultWinnerBuffer.Get(), resultWinnerElements, 4, false};
        entries[DX12UavSlot::IndirectArgs] =
            {IndirectArgsBuffer.Get(), static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true};
        entries[DX12UavSlot::DirectOutput] =
            {directTexture ? directTexture : DirectOutputDummy.Get(), 0, 0, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 2};
        return CreateUavDescriptorTable(
            Context->GetDevice(), increment, destination, entries, kUavTableSize);
    };

    const auto buildNativeTable = [&](D3D12_CPU_DESCRIPTOR_HANDLE destination,
        const DX12Gpu2DOutput::ComposeWorkSlot& work) {
        UavDescriptorEntry entries[kUavTableSize] = {};
        entries[DX12UavSlot::DispatchInput] =
            {work.NativeInput.Get(), static_cast<u32>(kNativeGPU2DInputBytes / 4u), 4, false};
        entries[DX12UavSlot::FinalFB] = {FinalFBBuffer.Get(), pixels, 4, false};
        entries[DX12UavSlot::TileColor] =
            {TileBuffers[0].Get(), tileElements, 4, false};
        entries[DX12UavSlot::TileDepth] =
            {TileBuffers[1].Get(), tileElements, 4, false};
        entries[DX12UavSlot::TileAttr] = {TileBuffers[2].Get(), tileElements, 4, false};
        entries[DX12UavSlot::BinResult] =
            {BinResultBuffer.Get(), binResultDwords, 4, true};
        entries[DX12UavSlot::WorkDesc] =
            {WorkDescBuffer.Get(), static_cast<u32>(MaxWorkTiles) * 2u, 8, false};
        entries[DX12UavSlot::XSpanSetup] =
            {XSpanSetupBuffer.Get(), static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false};
        entries[DX12UavSlot::DispatchOutput] =
            {work.StructuredInput.Get(), kCompositionInputDwords, 4, false};
        entries[DX12UavSlot::CaptureSidecar] =
            {Capture.GetSidecarBuffer(), 8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor), 4, false};
        entries[DX12UavSlot::BlendState] =
            {BlendStateBuffer.Get(), pixels + static_cast<u32>(kNativeCaptureWords) + kNativeObjRawWords, 4, false};
        entries[DX12UavSlot::ResultWinner] =
            {ResultWinnerBuffer.Get(), resultWinnerElements, 4, false};
        entries[DX12UavSlot::IndirectArgs] =
            {IndirectArgsBuffer.Get(), static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true};
        entries[DX12UavSlot::DirectOutput] =
            {DirectOutputDummy.Get(), 0, 0, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 2};
        return CreateUavDescriptorTable(
            Context->GetDevice(), increment, destination, entries, kUavTableSize);
    };

    for (u32 slotIndex = 0; slotIndex < kCompositorFramesInFlight; ++slotIndex)
    {
        Gpu2D.OutputUavCpu[slotIndex] = {
            base.ptr + static_cast<SIZE_T>(slotIndex) * kUavTableSize * increment };
        const DX12Gpu2DOutput::Slot& slot = state->Slots[slotIndex];
        if (!buildCompositorTable(Gpu2D.OutputUavCpu[slotIndex],
                slot.StructuredInput.Get(), slot.Composed.Get(),
                slot.DirectTexture.Get()))
            return false;

        const DX12Gpu2DOutput::ComposeWorkSlot& work = state->WorkSlots[slotIndex];
        Gpu2D.WorkNativeUavCpu[slotIndex] = {
            workNativeBase.ptr + static_cast<SIZE_T>(slotIndex)
                * kUavTableSize * increment };
        if (!buildNativeTable(Gpu2D.WorkNativeUavCpu[slotIndex], work))
            return false;

        for (u32 outputIndex = 0; outputIndex < kCompositorFramesInFlight; ++outputIndex)
        {
            const u32 tableIndex = slotIndex * 4u + outputIndex;
            Gpu2D.WorkOutputUavCpu[tableIndex] = {
                workCompositorBase.ptr + static_cast<SIZE_T>(tableIndex)
                    * kUavTableSize * increment };
            const DX12Gpu2DOutput::Slot& output = state->Slots[outputIndex];
            if (!buildCompositorTable(Gpu2D.WorkOutputUavCpu[tableIndex],
                    work.StructuredInput.Get(), output.Composed.Get(),
                    output.DirectTexture.Get()))
                return false;
        }
        const u32 diagnosticTableIndex = slotIndex * 4u + 3u;
        Gpu2D.WorkOutputUavCpu[diagnosticTableIndex] = {
            workCompositorBase.ptr + static_cast<SIZE_T>(diagnosticTableIndex)
                * kUavTableSize * increment };
    }
    return true;
}

bool DX12Renderer3D::BuildWorkDiagnosticCompositorUavDescriptor(u32 workIndex)
{
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!state || workIndex >= state->WorkSlots.size())
        return false;
    const DX12Gpu2DOutput::ComposeWorkSlot& work = state->WorkSlots[workIndex];
    if (!work.DiagnosticComposed)
        return false;

    const u32 pixels = static_cast<u32>(ScreenWidth) * static_cast<u32>(ScreenHeight);
    const u32 resultWinnerElements = ScaleFactor == 1 ? pixels * 2u : 1u;
    const u32 tileElements = static_cast<u32>(TileSize) * static_cast<u32>(TileSize)
        * static_cast<u32>(MaxWorkTiles);
    const u32 binResultDwords = static_cast<u32>(
        (sizeof(BinResultHeader)
            + static_cast<u64>(TilesPerLine) * TileLines * CoarseBinStride * 4ull
            + static_cast<u64>(TilesPerLine) * TileLines * BinStride * 8ull) / 4ull);
    UavDescriptorEntry entries[kUavTableSize] = {};
    entries[DX12UavSlot::DispatchInput] =
        {work.StructuredInput.Get(), kCompositionInputDwords, 4, false};
    entries[DX12UavSlot::FinalFB] = {FinalFBBuffer.Get(), pixels, 4, false};
    entries[DX12UavSlot::TileColor] = {TileBuffers[0].Get(), tileElements, 4, false};
    entries[DX12UavSlot::TileDepth] = {TileBuffers[1].Get(), tileElements, 4, false};
    entries[DX12UavSlot::TileAttr] = {TileBuffers[2].Get(), tileElements, 4, false};
    entries[DX12UavSlot::BinResult] = {BinResultBuffer.Get(), binResultDwords, 4, true};
    entries[DX12UavSlot::WorkDesc] =
        {WorkDescBuffer.Get(), static_cast<u32>(MaxWorkTiles) * 2u, 8, false};
    entries[DX12UavSlot::XSpanSetup] =
        {XSpanSetupBuffer.Get(), static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false};
    entries[DX12UavSlot::DispatchOutput] =
        {work.DiagnosticComposed.Get(), pixels * 2u, 4, false};
    entries[DX12UavSlot::CaptureSidecar] =
        {Capture.GetSidecarBuffer(), 8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor), 4, false};
    entries[DX12UavSlot::BlendState] =
        {BlendStateBuffer.Get(), pixels + static_cast<u32>(kNativeCaptureWords) + kNativeObjRawWords, 4, false};
    entries[DX12UavSlot::ResultWinner] =
        {ResultWinnerBuffer.Get(), resultWinnerElements, 4, false};
    entries[DX12UavSlot::IndirectArgs] =
        {IndirectArgsBuffer.Get(), static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true};
    entries[DX12UavSlot::DirectOutput] =
        {DirectOutputDummy.Get(), 0, 0, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 2};
    return CreateUavDescriptorTable(
        Context->GetDevice(), Gpu2D.WorkOutputUav.GetIncrement(),
        Gpu2D.WorkOutputUavCpu[workIndex * 4u + 3u], entries, kUavTableSize);
}

bool DX12Renderer3D::BuildStaticSrvDescriptors()
{
    StaticSrvDescriptors.Reset();
    D3D12_GPU_DESCRIPTOR_HANDLE ignored{};
    if (!StaticSrvDescriptors.Allocate(kStaticSrvCount, StaticSrvCpu, ignored))
        return false;

    ID3D12Device* device = Context->GetDevice();
    const u32 increment = StaticSrvDescriptors.GetIncrement();
    auto handleAt = [&](u32 index) {
        return D3D12_CPU_DESCRIPTOR_HANDLE{
            StaticSrvCpu.ptr + static_cast<SIZE_T>(index) * increment };
    };

    for (u32 i = 0; i < 2; ++i)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        desc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(ClearBitmapTex[i].Get(), &desc, handleAt(i));
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC spanDesc{};
    spanDesc.Format = DXGI_FORMAT_UNKNOWN;
    spanDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    spanDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    spanDesc.Buffer.NumElements = 2048;
    spanDesc.Buffer.StructureByteStride = sizeof(RenderPolygon);
    device->CreateShaderResourceView(RenderPolygonBuffer.Get(), &spanDesc, handleAt(2));
    spanDesc.Buffer.NumElements = MaxYSpanSetups;
    spanDesc.Buffer.StructureByteStride = sizeof(SpanSetupY);
    device->CreateShaderResourceView(YSpanSetupBuffer.Get(), &spanDesc, handleAt(3));

    D3D12_SHADER_RESOURCE_VIEW_DESC indexDesc{};
    indexDesc.Format = DXGI_FORMAT_R16G16B16A16_UINT;
    indexDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    indexDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    indexDesc.Buffer.NumElements = static_cast<UINT>(MaxYSpanIndices);
    device->CreateShaderResourceView(SetupIndicesBuffer.Get(), &indexDesc, handleAt(4));
    DX12Perf::AddCounter(DX12Perf::Counter::DescriptorCreateCount, kStaticSrvCount);
    return true;
}

RendererOutput DX12Renderer3D::GetComposedOutput() const
{
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    if (state->Ring.GetPublishedSlot() < 0)
        return {};
    const DX12PresentedFrame& frame = state->Slots[state->Ring.GetPublishedSlot()].Frame;
    return RendererOutput::DX12Buffer(
        const_cast<DX12PresentedFrame*>(&frame), frame.Width, frame.Height,
        frame.Serial, frame.Epoch);
}

RendererOutputLease DX12Renderer3D::AcquireComposedOutputLease()
{
    const std::shared_ptr<DX12Gpu2DOutput> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    const auto lock = state->Ring.LockPublication();
    const int publishedSlot = state->Ring.GetPublishedSlot();
    if (publishedSlot < 0)
        return {};

    DX12Gpu2DOutput::Slot& slot = state->Slots[publishedSlot];
    auto* leaseCounter = state->Ring.AcquireLease(static_cast<u32>(publishedSlot));
    GPU2DNative::LogPresentedIdentity(
        "DX12", slot.Frame.Generation, slot.Frame.Serial,
        slot.Frame.Generation, slot.Frame.Epoch,
        static_cast<u32>(publishedSlot));
    return RendererOutputLease(
        RendererOutput::DX12Buffer(
            &slot.Frame, slot.Frame.Width, slot.Frame.Height,
            slot.Frame.Serial, slot.Frame.Epoch),
        leaseCounter,
        &RendererOutputRing::LeaseCounter::Release,
        state);
}

u32* DX12Renderer3D::GetLine(int line)
{
    if (GPU3D.AbortFrame || line < 0 || line >= 192)
    {
        std::memset(ScrolledLine, 0, sizeof(ScrolledLine));
        return ScrolledLine;
    }

    EnsureFrameReadback();

    u32* rawline = &ColorBuffer[static_cast<size_t>(line) * 256];

    const u16 xpos = GPU3D.RenderXPos;
    if (xpos == 0)
        return rawline;

    // Same X-scroll handling as SoftRenderer3D::GetLine(). The source line is
    // exactly 256 pixels here (the readback is already resolved to native
    // resolution), so the out-of-range half is transparent.
    int i = 0;
    if (xpos & 0x100)
    {
        int j = xpos;
        for (; j < 512; i++, j++)
            ScrolledLine[i] = 0;
        for (j = 0; i < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
    }
    else
    {
        int j = xpos;
        for (; j < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
        for (; i < 256; i++)
            ScrolledLine[i] = 0;
    }

    return ScrolledLine;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
