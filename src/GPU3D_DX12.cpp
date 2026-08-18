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
#include "DX12GpuTimestamp.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <utility>

#include "DX12PresentedFrame.h"
#include "DX12Perf.h"
#include "MelonPrimeStructuredComposition.h"
#include "GPU.h"
#include "GPU3D_RasterEdge.h"
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
constexpr u32 kStaticSrvCount = 5;
constexpr u32 kTextureSrvCount = 1;
constexpr u32 kUavTableSize = 14;
constexpr u32 kStructuredPixelCount = 256u * 192u;
constexpr u32 kCompositionInputDwords =
    (kStructuredPixelCount * 14u) + (192u * 2u) + (192u * 4u);
constexpr u32 kCompositorFramesInFlight = 3;

constexpr u32 kRootParamDispatchConstants = 0;
constexpr u32 kRootParamMetaCbv = 1;
constexpr u32 kRootParamStaticSrvTable = 2;
constexpr u32 kRootParamTextureSrvTable = 3;
constexpr u32 kRootParamUavTable = 4;

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

struct DX12Renderer3D::OutputState
{
    struct Slot
    {
        DX12CommandContext Commands;
        DX12DescriptorRing Descriptors;
        DX12::ComPtr<ID3D12Resource> StructuredStaging;
        DX12::ComPtr<ID3D12Resource> StructuredInput;
        DX12::ComPtr<ID3D12Resource> Composed;
        DX12::ComPtr<ID3D12Resource> DirectTexture;
        u32* StructuredMapped = nullptr;
        StructuredComposition::GenerationState UploadedContentGeneration{};
        bool StructuredUploadInitialized = false;
        bool DirectTextureInShaderResource = false;
        DX12PresentedFrame Frame;
        std::atomic<u32> PresenterRefs{0};
    };

    ~OutputState()
    {
        for (Slot& slot : Slots)
        {
            slot.Commands.WaitIdle();
            if (slot.StructuredStaging && slot.StructuredMapped)
            {
                D3D12_RANGE noWrite{0, 0};
                slot.StructuredStaging->Unmap(0, &noWrite);
                slot.StructuredMapped = nullptr;
            }
            slot.Descriptors.Shutdown();
            slot.Commands.Shutdown();
        }
        if (OwnsContextReference && Context)
            Context->Release();
    }

    bool Create(DX12Context& context, u32 width, u32 height, u64 resourceGeneration)
    {
        if (!context.Acquire())
            return false;
        Context = &context;
        OwnsContextReference = true;
        ResourceGeneration = resourceGeneration;

        ID3D12Device* device = context.GetDevice();
        const u64 inputBytes = static_cast<u64>(kCompositionInputDwords) * sizeof(u32);
        const u64 screenBytes = static_cast<u64>(width) * height * sizeof(u32);
        D3D12_FEATURE_DATA_FORMAT_SUPPORT formatSupport{};
        formatSupport.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        const bool directFormatSupported =
            SUCCEEDED(device->CheckFeatureSupport(
                D3D12_FEATURE_FORMAT_SUPPORT,
                &formatSupport,
                sizeof(formatSupport)))
            && (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D) != 0
            && (formatSupport.Support1 & D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE) != 0
            && (formatSupport.Support1
                    & D3D12_FORMAT_SUPPORT1_TYPED_UNORDERED_ACCESS_VIEW) != 0
            && (formatSupport.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0;
        DirectTextureEnabled = directFormatSupported;
        if (!DirectTextureEnabled)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "DX12: compositor direct texture disabled: RGBA8 lacks sampled or typed UAV support\n");
        }
        for (Slot& slot : Slots)
        {
            if (!slot.Commands.Init(device, context.GetQueue())
                || !slot.Descriptors.Init(device, 16, true))
                return false;
            slot.StructuredInput = context.CreateBuffer(
                inputBytes, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                L"MelonPrime DX12 structured input slot");
            slot.StructuredStaging = context.CreateBuffer(
                inputBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_FLAG_NONE,
                L"MelonPrime DX12 structured staging slot");
            slot.Composed = context.CreateBuffer(
                screenBytes * 2u, D3D12_HEAP_TYPE_DEFAULT,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                L"MelonPrime DX12 composed output slot");
            if (!slot.StructuredInput || !slot.StructuredStaging || !slot.Composed)
                return false;

            D3D12_RANGE noRead{0, 0};
            if (FAILED(slot.StructuredStaging->Map(
                    0, &noRead, reinterpret_cast<void**>(&slot.StructuredMapped)))
                || !slot.StructuredMapped)
                return false;

        }

        if (DirectTextureEnabled)
        {
            for (Slot& slot : Slots)
            {
                slot.DirectTexture = context.CreateTexture2D(
                    DXGI_FORMAT_R8G8B8A8_UNORM,
                    width,
                    height,
                    2,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    L"MelonPrime DX12 direct compositor output slot");
                if (!slot.DirectTexture)
                {
                    DirectTextureEnabled = false;
                    break;
                }
            }
        }
        if (!DirectTextureEnabled)
        {
            for (Slot& slot : Slots)
            {
                slot.DirectTexture.Reset();
                slot.DirectTextureInShaderResource = false;
            }
        }

        for (Slot& slot : Slots)
        {
            slot.Frame.Buffer = slot.Composed.Get();
            slot.Frame.DirectTexture = DirectTextureEnabled
                ? slot.DirectTexture.Get() : nullptr;
            slot.Frame.TopOffset = 0;
            slot.Frame.BottomOffset = screenBytes;
            slot.Frame.Width = width;
            slot.Frame.Height = height;
            slot.Frame.ResourceGeneration = ResourceGeneration;
        }
        return true;
    }

    DX12Context* Context = nullptr;
    bool OwnsContextReference = false;
    bool DirectTextureEnabled = false;
    std::array<Slot, kCompositorFramesInFlight> Slots;
    std::mutex Mutex;
    int PublishedSlot = -1;
    u32 NextSlot = 0;
    u64 NextSerial = 1;
    u64 ResourceGeneration = 0;
};

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
    if (!Context || !Context->IsReady())
        return false;

    ID3D12Device* device = Context->GetDevice();

    if (!Commands.Init(device, Context->GetQueue()))
        return false;
    if (!CaptureCommands.Init(device, Context->GetQueue()))
        return false;
    if (!Uploads.Init(*Context, kUploadRingBytes))
        return false;
    if (!Descriptors.Init(device, kDescriptorCount, true))
        return false;
    if (!StaticSrvDescriptors.Init(device, kStaticSrvCount, false))
        return false;
    if (!FrameUavDescriptors.Init(device, kUavTableSize, false))
        return false;
    if (!CompositorUavDescriptors.Init(
            device, kUavTableSize * kCompositorFramesInFlight, false))
        return false;
    if (!CaptureDescriptors.Init(device, kUavTableSize, true))
        return false;
    if (!CreateRootSignature())
        return false;
    if (!CreateCommandSignature())
        return false;

    TextureHeap.Init(Context, &Commands, &Uploads);

    if (!CreateFixedResources())
        return false;

    ClearBitmapDirty = 0x3;

    Platform::Log(
        Platform::LogLevel::Info,
        "DX12: 3D renderer initialized on \"%s\"\n",
        Context->GetDeviceProfile().AdapterName.c_str());
    return true;
}

void DX12Renderer3D::Stop()
{
    Commands.WaitIdle();
    CaptureCommands.WaitIdle();

    Texcache.Reset();
    TextureHeap.CollectGarbage();
    TextureHeap.Shutdown();

    ReleasePipelines();
    ReleaseScaleDependentResources();

    const D3D12_RANGE noWrite{ 0, 0 };
    if (RenderPolygonStaging && RenderPolygonStagingPtr)
        RenderPolygonStaging->Unmap(0, &noWrite);
    if (YSpanSetupStaging && YSpanSetupStagingPtr)
        YSpanSetupStaging->Unmap(0, &noWrite);
    if (MetaUniformUpload && MetaUniformUploadPtr)
        MetaUniformUpload->Unmap(0, &noWrite);
    for (int slot = 0; slot < 2; ++slot)
    {
        if (ClearBitmapUpload[slot] && ClearBitmapUploadPtr[slot])
            ClearBitmapUpload[slot]->Unmap(0, &noWrite);
        ClearBitmapUploadPtr[slot] = nullptr;
        ClearBitmapUpload[slot].Reset();
    }
    RenderPolygonStagingPtr = nullptr;
    YSpanSetupStagingPtr = nullptr;
    MetaUniformUploadPtr = nullptr;
    RenderPolygonStaging.Reset();
    YSpanSetupStaging.Reset();
    MetaUniformUpload.Reset();
    RenderPolygonBuffer.Reset();
    YSpanSetupBuffer.Reset();

    ReadbackBuffer.Reset();
    ResolveBuffer.Reset();
    IndirectArgsBuffer.Reset();
    BinResultBuffer.Reset();
    ClearBitmapTex[0].Reset();
    ClearBitmapTex[1].Reset();
    DummyTexture.Reset();
    DirectOutputDummy.Reset();
    DispatchSignature.Reset();
    RootSignature.Reset();

    Descriptors.Shutdown();
    StaticSrvDescriptors.Shutdown();
    FrameUavDescriptors.Shutdown();
    CompositorUavDescriptors.Shutdown();
    CaptureDescriptors.Shutdown();
    Uploads.Shutdown();
    Commands.Shutdown();
    CaptureCommands.Shutdown();

    FrameInFlight = false;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
    FinalFBHasValidFrame = false;
    ComposedOutputValid = false;
    ComposedGeneration = 0;
}

void DX12Renderer3D::Reset()
{
    Commands.WaitIdle();
    CaptureCommands.WaitIdle();
    Texcache.Reset();
    TextureHeap.CollectGarbage();
    ClearBitmapDirty = 0x3;
    FrameInFlight = false;
    FrameReadbackValid = false;
    NativeReadbackSubmitted = false;
    FinalFBHasValidFrame = false;
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    ColorBuffer.fill(0);
    if (ComposedOutput)
    {
        std::lock_guard<std::mutex> lock(ComposedOutput->Mutex);
        ComposedOutput->PublishedSlot = -1;
        for (OutputState::Slot& slot : ComposedOutput->Slots)
        {
            slot.UploadedContentGeneration = {};
            slot.StructuredUploadInitialized = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Device objects
// ---------------------------------------------------------------------------

bool DX12Renderer3D::CreateRootSignature()
{
    const auto& entry = DX12::LoadEntryPoints();
    if (!entry.D3D12SerializeRootSignature)
        return false;

    D3D12_DESCRIPTOR_RANGE staticSrvRange{};
    staticSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    staticSrvRange.NumDescriptors = kStaticSrvCount;
    staticSrvRange.BaseShaderRegister = 0;
    staticSrvRange.RegisterSpace = 0;
    staticSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE textureSrvRange{};
    textureSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureSrvRange.NumDescriptors = kTextureSrvCount;
    textureSrvRange.BaseShaderRegister = 5;
    textureSrvRange.RegisterSpace = 0;
    textureSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = kUavTableSize;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[5]{};

    params[kRootParamDispatchConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[kRootParamDispatchConstants].Constants.ShaderRegister = 0;
    params[kRootParamDispatchConstants].Constants.RegisterSpace = 0;
    params[kRootParamDispatchConstants].Constants.Num32BitValues = DispatchUniformDwords;
    params[kRootParamDispatchConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootParamMetaCbv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[kRootParamMetaCbv].Descriptor.ShaderRegister = 1;
    params[kRootParamMetaCbv].Descriptor.RegisterSpace = 0;
    params[kRootParamMetaCbv].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootParamStaticSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootParamStaticSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootParamStaticSrvTable].DescriptorTable.pDescriptorRanges = &staticSrvRange;
    params[kRootParamStaticSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootParamTextureSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootParamTextureSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootParamTextureSrvTable].DescriptorTable.pDescriptorRanges = &textureSrvRange;
    params[kRootParamTextureSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[kRootParamUavTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[kRootParamUavTable].DescriptorTable.NumDescriptorRanges = 1;
    params[kRootParamUavTable].DescriptorTable.pDescriptorRanges = &uavRange;
    params[kRootParamUavTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 5;
    desc.pParameters = params;
    desc.NumStaticSamplers = 0;
    desc.pStaticSamplers = nullptr;
    desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

    DX12::ComPtr<ID3DBlob> blob;
    DX12::ComPtr<ID3DBlob> errors;
    HRESULT hr = entry.D3D12SerializeRootSignature(
        &desc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        blob.ReleaseAndGetAddressOf(),
        errors.ReleaseAndGetAddressOf());
    if (FAILED(hr))
    {
        if (errors)
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: root signature serialization failed: %s\n",
                static_cast<const char*>(errors->GetBufferPointer()));
        }
        return DX12::Fail("D3D12SerializeRootSignature", hr);
    }

    hr = Context->GetDevice()->CreateRootSignature(
        0,
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(RootSignature.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateRootSignature", hr);

    return true;
}

bool DX12Renderer3D::CreateCommandSignature()
{
    D3D12_INDIRECT_ARGUMENT_DESC arg{};
    arg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    D3D12_COMMAND_SIGNATURE_DESC desc{};
    desc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs = &arg;
    desc.NodeMask = 0;

    // No root-argument changes in the indirect stream, so the signature does
    // not need the root signature.
    const HRESULT hr = Context->GetDevice()->CreateCommandSignature(
        &desc, nullptr, IID_PPV_ARGS(DispatchSignature.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateCommandSignature", hr);

    return true;
}

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

        ClearBitmapUpload[i] = Context->CreateBuffer(
            clearBitmapBytes,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            i == 0
                ? L"MelonPrime DX12 clear bitmap color upload"
                : L"MelonPrime DX12 clear bitmap depth upload");
        if (!ClearBitmapUpload[i]
            || FAILED(ClearBitmapUpload[i]->Map(
                0, &noRead, reinterpret_cast<void**>(&ClearBitmapUploadPtr[i])))
            || !ClearBitmapUploadPtr[i])
            return false;
    }

    MetaUniformUpload = Context->CreateBuffer(
        AlignUp(sizeof(MetaUniform), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 frame uniform upload");
    if (!MetaUniformUpload
        || FAILED(MetaUniformUpload->Map(
            0, &noRead, reinterpret_cast<void**>(&MetaUniformUploadPtr)))
        || !MetaUniformUploadPtr)
        return false;

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
    RenderPolygonStaging = Context->CreateBuffer(
        sizeof(RenderPolygon) * 2048ull,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 polygon staging");
    YSpanSetupBuffer = Context->CreateBuffer(
        sizeof(SpanSetupY) * static_cast<u64>(MaxYSpanSetups),
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 y-spans");
    YSpanSetupStaging = Context->CreateBuffer(
        sizeof(SpanSetupY) * static_cast<u64>(MaxYSpanSetups),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 y-span staging");
    if (!RenderPolygonBuffer || !RenderPolygonStaging || !YSpanSetupBuffer || !YSpanSetupStaging)
        return false;

    void* mapped = nullptr;
    if (FAILED(RenderPolygonStaging->Map(0, &noRead, &mapped)))
        return false;
    RenderPolygonStagingPtr = static_cast<u8*>(mapped);
    if (FAILED(YSpanSetupStaging->Map(0, &noRead, &mapped)))
        return false;
    YSpanSetupStagingPtr = static_cast<u8*>(mapped);
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
    PipelineCompositor.Reset();
    PipelineCorrectCoverage.Reset();
}

void DX12Renderer3D::ReleaseScaleDependentResources()
{
    if (SetupIndicesStaging && SetupIndicesStagingPtr)
    {
        D3D12_RANGE written{ 0, 0 };
        SetupIndicesStaging->Unmap(0, &written);
        SetupIndicesStagingPtr = nullptr;
    }

    ResultBuffer.Reset();
    ResultWinnerBuffer.Reset();
    FinalFBBuffer.Reset();
    CaptureSidecarBuffer.Reset();
    ComposedOutput.reset();
    TileBuffers[0].Reset();
    TileBuffers[1].Reset();
    TileBuffers[2].Reset();
    WorkDescBuffer.Reset();
    BlendStateBuffer.Reset();
    XSpanSetupBuffer.Reset();
    SetupIndicesBuffer.Reset();
    SetupIndicesStaging.Reset();
    // Sized from the tile counts, so it is scale-dependent too. Clearing it
    // here is what makes RenderFrame()'s null check catch a partially failed
    // reallocation instead of running against a stale buffer.
    BinResultBuffer.Reset();
    FrameUavDescriptors.Reset();
    CompositorUavDescriptors.Reset();
    FrameUavCpu = {};
    CompositorUavCpu.fill({});
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    FinalFBHasValidFrame = false;
}

bool DX12Renderer3D::CreateScaleDependentResources()
{
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

    CaptureSidecarBuffer = Context->CreateBuffer(
        8ull * 256ull * 256ull * static_cast<u64>(ScaleFactor) * static_cast<u64>(ScaleFactor) * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 high-resolution capture sidecar");
    if (!CaptureSidecarBuffer)
        return false;

    ComposedOutput = std::make_shared<OutputState>();
    if (!ComposedOutput->Create(
            *Context, static_cast<u32>(ScreenWidth), static_cast<u32>(ScreenHeight),
            NextOutputResourceGeneration++))
        return false;

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
        pixels * 4ull,
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
    SetupIndicesStaging = Context->CreateBuffer(
        sizeof(SetupIndices) * static_cast<u64>(MaxYSpanIndices),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 span index staging");
    if (!SetupIndicesBuffer || !SetupIndicesStaging)
        return false;

    D3D12_RANGE noRead{ 0, 0 };
    void* mapped = nullptr;
    if (FAILED(SetupIndicesStaging->Map(0, &noRead, &mapped)))
        return false;
    SetupIndicesStagingPtr = static_cast<u8*>(mapped);

    return BuildStaticSrvDescriptors()
        && BuildFrameUavDescriptors()
        && BuildCompositorUavDescriptors();
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

    Commands.WaitIdle();
    CaptureCommands.WaitIdle();

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
    static_assert(ShaderStepCount == static_cast<int>(DX12ShaderBlobs::VariantCount));
    pipeline.Reset();

    if (!Context || !RootSignature)
        return false;

    const u32 geometryBucket = TileSize == 8 ? 0u : (TileSize == 16 ? 1u : 2u);
    const DX12ShaderBlobs::Blob blob = DX12ShaderBlobs::Get(
        geometryBucket, static_cast<u32>(shaderVariant));
    if (!blob.Data || blob.Size == 0)
        return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = RootSignature.Get();
    desc.CS.pShaderBytecode = blob.Data;
    desc.CS.BytecodeLength = blob.Size;
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    const HRESULT hr = Context->GetDevice()->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(pipeline.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateComputePipelineState", hr);

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
        build(PipelineCompositor, DX12Shaders::Compositor,
            { "Compositor" }, "DX12Compositor");
        return;
    }

    if (step == ShaderStep_CorrectCoverage)
    {
        build(PipelineCorrectCoverage, DX12Shaders::CorrectCoverage,
            { "CorrectCoverage" }, "DX12CorrectCoverage");
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

    ID3D12GraphicsCommandList* list = Commands.GetList();
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

        if (!ClearBitmapUpload[slot] || !ClearBitmapUploadPtr[slot])
            continue;

        std::memcpy(
            ClearBitmapUploadPtr[slot], ClearBitmap[slot].get(), static_cast<size_t>(totalBytes));

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
        src.pResource = ClearBitmapUpload[slot].Get();
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

    if (!MetaUniformUpload || !MetaUniformUploadPtr)
        return false;

    std::memcpy(MetaUniformUploadPtr, &meta, sizeof(meta));
    list->SetComputeRootConstantBufferView(
        kRootParamMetaCbv, MetaUniformUpload->GetGPUVirtualAddress());
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
    if (!Descriptors.Allocate(kUavTableSize, cpu, gpu))
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
    if (!Descriptors.Allocate(kStaticSrvCount, cpu, gpu))
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
    return Commands.WaitQueueIdle();
}

bool DX12Renderer3D::BindSrvTable(ID3D12GraphicsCommandList* list, ID3D12Resource* texture)
{
    DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
    if (!texture)
        texture = DummyTexture.Get();

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
    if (!Descriptors.Allocate(kTextureSrvCount, cpu, gpu))
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
        return 1;
    }

    const u64 capacity = static_cast<u64>(MaxWorkTiles);
    u32 first = 0;
    u32 count = 0;
    u32 batchCount = 0;
    u64 batchTiles = 0;

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
        PolygonBatches[batchCount++] = { first, count };
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
    if (!Context || !RootSignature || !ResultBuffer || !ResultWinnerBuffer
        || !FinalFBBuffer || !BinResultBuffer || !IndirectArgsBuffer)
    {
        SetRuntimeFailure("required frame resources are unavailable");
        return;
    }
    if (ShaderStepIdx < ShaderStepCount)
        return; // pipelines are still being compiled

    DX12Perf::SetScale(static_cast<u32>(ScaleFactor));
    DX12Perf::AddCounter(DX12Perf::Counter::Frames);

    u8 texcacheClearBitmapDirty = 0;
    bool textureCacheChanged = false;
    {
        DX12Perf::ScopedCpuTimer texcacheTimer(DX12Perf::CpuMetric::TexcacheUpdate);
        textureCacheChanged = Texcache.Update(texcacheClearBitmapDirty);
    }
    ClearBitmapDirty |= texcacheClearBitmapDirty;
    if (!textureCacheChanged && GPU3D.RenderFrameIdentical
        && FinalFBHasValidFrame && ClearBitmapDirty == 0)
    {
        DX12Perf::AddCounter(DX12Perf::Counter::IdenticalFrames);
        DX12Perf::MaybeReport();
        return;
    }

    ID3D12GraphicsCommandList* list = nullptr;
    list = Commands.Begin(true);
    if (!list)
    {
        SetRuntimeFailure("could not begin a frame command list");
        return;
    }
    RecordDX12GpuMetric(
        Commands, GpuMetric::Raster, DX12Perf::Counter::RasterGpuTimeNs);
    Commands.WriteTimestamp(GpuMetricQueryIndex(GpuMetric::Raster, false));

    // Begin() waited for the previous submission, so both rings are free to
    // reuse and retired textures can go.
    Descriptors.Reset();
    Uploads.Reset();
    TextureHeap.CollectGarbage();
    TextureHeap.ResetUploadFailure();
    BoundSrvTexture = nullptr;
    BoundSrvTable = {};
    ResetFrameSrvCache();

    UpdateClearBitmap();

    // Polygon/span setup runs on the CPU exactly like the OpenGL compute
    // renderer; the texcache uploads it triggers are recorded into this same
    // list, which is why it has to happen while the list is open.
    int numYSpans = 0;
    int numSetupIndices = 0;
    u32 numPolygons = 0;
    u32 numVariants = 0;
    {
        DX12Perf::ScopedCpuTimer polygonTimer(DX12Perf::CpuMetric::BuildPolygons);
        numVariants = BuildPolygons(numYSpans, numSetupIndices, numPolygons);
    }
    if (TextureHeap.HadUploadFailure())
    {
        Commands.Submit();
        SetRuntimeFailure("could not allocate or map a texture spill upload");
        return;
    }
    DX12Perf::RecordGeometry(
        numPolygons, numVariants, static_cast<u32>(numYSpans), static_cast<u32>(numSetupIndices));
    TextureHeap.FlushUploadBarriers();

    // Texture-cache setup can use retained spill uploads if the main upload
    // ring fills, while continuing to record this same command list.
    list = Commands.GetList();
    if (!list)
    {
        SetRuntimeFailure("frame command list was lost during texture upload");
        return;
    }

    ID3D12DescriptorHeap* heaps[] = { Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(RootSignature.Get());

    if (numYSpans > 0)
    {
        const u64 spanBytes = sizeof(SpanSetupY) * static_cast<u64>(numYSpans)
            + sizeof(SetupIndices) * static_cast<u64>(numSetupIndices)
            + sizeof(RenderPolygon) * static_cast<u64>(numPolygons);
        {
            DX12Perf::ScopedCpuTimer copyTimer(DX12Perf::CpuMetric::SpanStagingCopy);
            std::memcpy(YSpanSetupStagingPtr, YSpanSetups.get(), sizeof(SpanSetupY) * numYSpans);
            std::memcpy(SetupIndicesStagingPtr, YSpanIndices.data(), sizeof(SetupIndices) * numSetupIndices);
            std::memcpy(RenderPolygonStagingPtr, RenderPolygons.get(), sizeof(RenderPolygon) * numPolygons);
        }
        DX12Perf::AddCounter(DX12Perf::Counter::SpanUploadBytes, spanBytes);

        TransitionBuffer(list, YSpanSetupBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        TransitionBuffer(list, SetupIndicesBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
        TransitionBuffer(list, RenderPolygonBuffer.Get(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);

        list->CopyBufferRegion(YSpanSetupBuffer.Get(), 0, YSpanSetupStaging.Get(), 0,
            sizeof(SpanSetupY) * numYSpans);
        list->CopyBufferRegion(SetupIndicesBuffer.Get(), 0, SetupIndicesStaging.Get(), 0,
            sizeof(SetupIndices) * numSetupIndices);
        list->CopyBufferRegion(RenderPolygonBuffer.Get(), 0, RenderPolygonStaging.Get(), 0,
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
        Commands.Submit();
        SetRuntimeFailure("could not upload the frame uniform block");
        return;
    }

    if (!BindFrameUavTable(list) || !BindStaticSrvTable(list) ||
        !BindSrvTable(list, nullptr))
    {
        Commands.Submit();
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
            DispatchSignature.Get(), 1, IndirectArgsBuffer.Get(),
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
                if (!BindSrvTable(list, texture ? texture->Resource.Get() : nullptr))
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
                    DispatchSignature.Get(), 1, IndirectArgsBuffer.Get(),
                    offsetof(BinResultHeader, VariantWorkCount) + i * 16, nullptr, 0);
            }
            if (!descriptorsValid)
            {
                Commands.Submit();
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
            list->SetPipelineState(PipelineCorrectCoverage.Get());
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
        Commands.WriteTimestamp(GpuMetricQueryIndex(GpuMetric::Raster, true));
        submitted = Commands.Submit();
    }
    if (submitted)
    {
        FrameInFlight = true;
        FrameReadbackValid = false;
        FinalFBHasValidFrame = true;
    }
    else
    {
        SetRuntimeFailure("frame command submission failed");
    }
    DX12Perf::MaybeReport();
}

bool DX12Renderer3D::RecordNativeResolveAndReadback()
{
    if (!CaptureCommands.GetList() || !CaptureDescriptors.GetHeap()
        || !RootSignature || !PipelineResolve || !FinalFBBuffer || !ResolveBuffer
        || !ReadbackBuffer || !FrameUavCpu.ptr)
        return false;

    // Retire only the previous lazy-capture submission before recycling its
    // command allocator and descriptor table. This is not a queue-wide idle.
    CaptureCommands.WaitIdle();

    ID3D12GraphicsCommandList* list = CaptureCommands.TryBegin();
    if (!list)
        return false;

    CaptureDescriptors.Reset();
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    {
        DX12Perf::ScopedCpuTimer descriptorTimer(DX12Perf::CpuMetric::DescriptorUpdate);
        if (!CaptureDescriptors.Allocate(kUavTableSize, cpu, gpu))
        {
            CaptureCommands.Submit();
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

    ID3D12DescriptorHeap* heaps[] = { CaptureDescriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(RootSignature.Get());
    SetDispatchConstants(list, MakeDispatchUniform());
    list->SetComputeRootDescriptorTable(kRootParamUavTable, gpu);

    // The main render and compositor use the same direct queue. This UAV
    // barrier makes FinalFB writes visible to the resolve in this later list.
    InsertUavBarrier(list, FinalFBBuffer.Get());
    list->SetPipelineState(PipelineResolve.Get());
    list->Dispatch(DivRoundUp(256, 8), DivRoundUp(192, 8), 1);
    InsertUavBarrier(list, ResolveBuffer.Get());
    TransitionBuffer(list, ResolveBuffer.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    list->CopyBufferRegion(ReadbackBuffer.Get(), 0, ResolveBuffer.Get(), 0,
        256ull * 192ull * 4ull);
    TransitionBuffer(list, ResolveBuffer.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    if (!CaptureCommands.Submit())
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
        CaptureCommands.WaitIdle();
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

bool DX12Renderer3D::ComposeStructuredOutput(
    const std::array<const u32*, 14>& planes,
    const std::array<const u32*, 2>& lineMeta,
    const u32* captureCommands,
    const StructuredComposition::ScreenRoutingView& screenRouting,
    u64 generation,
    const StructuredComposition::GenerationState& contentGeneration)
{
    if (RuntimeFailed || ShaderStepIdx < ShaderStepCount)
        return false;
    if (ComposedOutputValid && ComposedGeneration == generation)
        return true;
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!Context || !PipelineCaptureSidecar || !PipelineCompositor
        || !state || !FinalFBBuffer || !CaptureSidecarBuffer)
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
    {
        std::lock_guard<std::mutex> lock(state->Mutex);
        slotIndex = state->NextSlot;
        state->NextSlot = (state->NextSlot + 1u) % kCompositorFramesInFlight;
    }
    OutputState::Slot& slot = state->Slots[slotIndex];
    if (slot.PresenterRefs.load(std::memory_order_acquire) != 0)
    {
        // Presentation is still reading this slot. Reusing the last published
        // frame is preferable to blocking the emulation thread.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        return false;
    }
    ID3D12GraphicsCommandList* list = slot.Commands.TryBegin();
    if (!list)
    {
        // The GPU has not retired this ring slot after three frames. Keep the
        // previous output and let the emulator continue without a fence wait.
        DX12Perf::AddCounter(DX12Perf::Counter::CompositorDropCount);
        return false;
    }
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::CaptureSidecar,
        DX12Perf::Counter::CaptureSidecarGpuTimeNs);
    RecordDX12GpuMetric(
        slot.Commands, GpuMetric::StructuredCompositor,
        DX12Perf::Counter::StructuredCompositorGpuTimeNs);
    slot.Commands.WriteTimestamp(
        GpuMetricQueryIndex(GpuMetric::StructuredCompositor, false));

    constexpr u32 logicalUnitCount =
        StructuredComposition::kStructuredInputPlaneCount
        + StructuredComposition::kStructuredInputLineMetaCount + 1u;
    struct UploadRange
    {
        u64 Offset = 0;
        u64 Size = 0;
    };
    const u64 planeBytes = static_cast<u64>(kStructuredPixelCount) * sizeof(u32);
    const u64 lineMetaBytes = 192u * sizeof(u32);
    const u64 captureCommandBytes =
        192u * StructuredComposition::kCaptureCommandWords * sizeof(u32);
    std::array<u64, logicalUnitCount> unitOffsets{};
    std::array<u64, logicalUnitCount> unitSizes{};
    for (u32 unit = 0; unit < 14u; ++unit)
    {
        unitOffsets[unit] = static_cast<u64>(unit) * planeBytes;
        unitSizes[unit] = planeBytes;
    }
    unitOffsets[14u] = 14u * planeBytes;
    unitOffsets[15u] = unitOffsets[14u] + lineMetaBytes;
    unitOffsets[16u] = unitOffsets[15u] + lineMetaBytes;
    unitSizes[14u] = lineMetaBytes;
    unitSizes[15u] = lineMetaBytes;
    unitSizes[16u] = captureCommandBytes;

    std::array<bool, logicalUnitCount> dirty{};
    const bool fullUpload = !slot.StructuredUploadInitialized;
    for (u32 plane = 0; plane < 14u; ++plane)
    {
        dirty[plane] = fullUpload
            || contentGeneration.Plane[plane]
                != slot.UploadedContentGeneration.Plane[plane];
    }
    dirty[14u] = fullUpload
        || contentGeneration.LineMeta[0] != slot.UploadedContentGeneration.LineMeta[0];
    dirty[15u] = fullUpload
        || contentGeneration.LineMeta[1] != slot.UploadedContentGeneration.LineMeta[1];
    const bool captureClassificationDirty = fullUpload
        || contentGeneration.CaptureCommands
            != slot.UploadedContentGeneration.CaptureCommands
        || contentGeneration.Plane[3u]
            != slot.UploadedContentGeneration.Plane[3u]
        || contentGeneration.Plane[7u]
            != slot.UploadedContentGeneration.Plane[7u]
        || contentGeneration.Plane[13u]
            != slot.UploadedContentGeneration.Plane[13u];
    dirty[16u] = captureClassificationDirty;

    std::array<UploadRange, logicalUnitCount> ranges{};
    std::size_t rangeCount = 0;
    for (u32 unit = 0; unit < logicalUnitCount; ++unit)
    {
        if (!dirty[unit])
            continue;
        const u64 offset = unitOffsets[unit];
        const u64 size = unitSizes[unit];
        if (rangeCount != 0
            && ranges[rangeCount - 1].Offset + ranges[rangeCount - 1].Size == offset)
        {
            ranges[rangeCount - 1].Size += size;
        }
        else
        {
            ranges[rangeCount++] = {offset, size};
        }
    }
    const bool uploadRequired = rangeCount != 0;
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
    InsertUavBarrier(list, CaptureSidecarBuffer.Get());

    ID3D12DescriptorHeap* heaps[] = { slot.Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(RootSignature.Get());
    if (!BindCompositionUavTable(
            list, slot.Descriptors, CompositorUavCpu[slotIndex]))
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
            InsertUavBarrier(list, CaptureSidecarBuffer.Get());
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
        InsertUavBarrier(list, CaptureSidecarBuffer.Get());
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
    list->SetPipelineState(PipelineCompositor.Get());
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
        std::lock_guard<std::mutex> lock(state->Mutex);
        slot.Frame.Serial = state->NextSerial++;
        slot.Frame.Generation = generation;
        state->PublishedSlot = static_cast<int>(slotIndex);
        ComposedGeneration = generation;
        ComposedOutputValid = true;
    }
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

    const UavDescriptorEntry entries[kUavTableSize] = {
        { ResultBuffer.Get(),      pixels * 3u * 2u,               4, false },
        { FinalFBBuffer.Get(),     pixels,                         4, false },
        { TileBuffers[0].Get(),    tileElements,                   4, false },
        { TileBuffers[1].Get(),    tileElements,                   4, false },
        { TileBuffers[2].Get(),    tileElements,                   4, false },
        { BinResultBuffer.Get(),   binResultDwords,                4, true  },
        { WorkDescBuffer.Get(),    static_cast<u32>(MaxWorkTiles) * 2u, 8, false },
        { XSpanSetupBuffer.Get(),  static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false },
        { ResolveBuffer.Get(),     256u * 192u,                    4, false },
        { CaptureSidecarBuffer.Get(),
            8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor),
            4, false },
        { BlendStateBuffer.Get(), pixels, 4, false },
        { ResultWinnerBuffer.Get(), resultWinnerElements, 4, false },
        { IndirectArgsBuffer.Get(), static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true },
        { DirectOutputDummy.Get(), 0, 0, false, true,
            DXGI_FORMAT_R8G8B8A8_UNORM, 2 },
    };
    return CreateUavDescriptorTable(
        Context->GetDevice(), FrameUavDescriptors.GetIncrement(), FrameUavCpu,
        entries, kUavTableSize);
}

bool DX12Renderer3D::BuildCompositorUavDescriptors()
{
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!state)
        return false;

    CompositorUavDescriptors.Reset();
    D3D12_CPU_DESCRIPTOR_HANDLE base{};
    D3D12_GPU_DESCRIPTOR_HANDLE ignored{};
    if (!CompositorUavDescriptors.Allocate(
            kUavTableSize * kCompositorFramesInFlight, base, ignored))
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
    const u32 increment = CompositorUavDescriptors.GetIncrement();
    CompositorUavCpu.fill(D3D12_CPU_DESCRIPTOR_HANDLE{});

    for (u32 slotIndex = 0; slotIndex < kCompositorFramesInFlight; ++slotIndex)
    {
        CompositorUavCpu[slotIndex] = {
            base.ptr + static_cast<SIZE_T>(slotIndex) * kUavTableSize * increment };
        const OutputState::Slot& slot = state->Slots[slotIndex];
        const UavDescriptorEntry entries[kUavTableSize] = {
            { slot.StructuredInput.Get(), kCompositionInputDwords, 4, false },
            { FinalFBBuffer.Get(),         pixels,                   4, false },
            { TileBuffers[0].Get(),        tileElements,             4, false },
            { TileBuffers[1].Get(),        tileElements,             4, false },
            { TileBuffers[2].Get(),        tileElements,             4, false },
            { BinResultBuffer.Get(),       binResultDwords,           4, true  },
            { WorkDescBuffer.Get(),        static_cast<u32>(MaxWorkTiles) * 2u, 8, false },
            { XSpanSetupBuffer.Get(),      static_cast<u32>(MaxYSpanIndices), sizeof(SpanSetupX), false },
            { slot.Composed.Get(),         pixels * 2u,               4, false },
            { CaptureSidecarBuffer.Get(),
                8u * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor),
                4, false },
            { BlendStateBuffer.Get(),      pixels,                    4, false },
            { ResultWinnerBuffer.Get(),    resultWinnerElements,      4, false },
            { IndirectArgsBuffer.Get(),
                static_cast<u32>(sizeof(BinResultHeader) / sizeof(u32)), 4, true },
            { slot.DirectTexture ? slot.DirectTexture.Get() : DirectOutputDummy.Get(),
                0, 0, false, true, DXGI_FORMAT_R8G8B8A8_UNORM, 2 },
        };
        if (!CreateUavDescriptorTable(
                Context->GetDevice(), increment, CompositorUavCpu[slotIndex],
                entries, kUavTableSize))
        {
            return false;
        }
    }
    return true;
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
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    std::lock_guard<std::mutex> lock(state->Mutex);
    if (state->PublishedSlot < 0)
        return {};
    const DX12PresentedFrame& frame = state->Slots[state->PublishedSlot].Frame;
    return RendererOutput::DX12Buffer(
        const_cast<DX12PresentedFrame*>(&frame), frame.Width, frame.Height, frame.Serial);
}

RendererOutputLease DX12Renderer3D::AcquireComposedOutputLease()
{
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    std::lock_guard<std::mutex> lock(state->Mutex);
    if (state->PublishedSlot < 0)
        return {};

    OutputState::Slot& slot = state->Slots[state->PublishedSlot];
    slot.PresenterRefs.fetch_add(1, std::memory_order_relaxed);
    auto release = +[](void* opaque) {
        auto* leasedSlot = static_cast<OutputState::Slot*>(opaque);
        const u32 previous = leasedSlot->PresenterRefs.fetch_sub(1, std::memory_order_release);
        assert(previous > 0);
    };
    return RendererOutputLease(
        RendererOutput::DX12Buffer(
            &slot.Frame, slot.Frame.Width, slot.Frame.Height, slot.Frame.Serial),
        &slot,
        release,
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
