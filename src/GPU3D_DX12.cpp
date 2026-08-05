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

#include <cstdio>
#include <cstring>

#include "GPU.h"
#include "GPU3D_DX12_shaders.h"
#include "Platform.h"

namespace melonDS
{

namespace
{

// Both the upload ring and the descriptor heap are sized for the worst frame
// the renderer can produce, so neither ever has to grow at runtime.
constexpr u64 kUploadRingBytes = 32ull * 1024 * 1024;
constexpr u32 kDescriptorCount = 4096;

constexpr u32 kSrvTableSize = 2;
constexpr u32 kUavTableSize = 2;

constexpr u32 DivRoundUp(u32 value, u32 divisor) noexcept
{
    return (value + divisor - 1) / divisor;
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

    // The constructor cannot fail; Init() does the device-side work.
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
    if (!Uploads.Init(*Context, kUploadRingBytes))
        return false;
    if (!Descriptors.Init(device, kDescriptorCount, true))
        return false;
    if (!CreateRootSignature())
        return false;

    TextureHeap.Init(Context, &Commands, &Uploads);

    for (int i = 0; i < 2; i++)
    {
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

    Texcache.Reset();
    TextureHeap.CollectGarbage();
    TextureHeap.Shutdown();

    ReleasePipelines();
    ReleaseScaleDependentResources();

    ReadbackBuffer.Reset();
    ResolveBuffer.Reset();
    ClearBitmapTex[0].Reset();
    ClearBitmapTex[1].Reset();
    RootSignature.Reset();

    Descriptors.Shutdown();
    Uploads.Shutdown();
    Commands.Shutdown();

    FrameInFlight = false;
    FrameReadbackValid = false;
}

void DX12Renderer3D::Reset()
{
    Commands.WaitIdle();
    Texcache.Reset();
    TextureHeap.CollectGarbage();
    ClearBitmapDirty = 0x3;
    FrameInFlight = false;
    FrameReadbackValid = false;
    ColorBuffer.fill(0);
}

bool DX12Renderer3D::CreateRootSignature()
{
    const auto& entry = DX12::LoadEntryPoints();
    if (!entry.D3D12SerializeRootSignature)
        return false;

    D3D12_DESCRIPTOR_RANGE srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = kSrvTableSize;
    srvRange.BaseShaderRegister = 0;
    srvRange.RegisterSpace = 0;
    srvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = kUavTableSize;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[4]{};

    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = DispatchUniformDwords;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[1].Descriptor.ShaderRegister = 1;
    params[1].Descriptor.RegisterSpace = 0;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &srvRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &uavRange;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = 4;
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

void DX12Renderer3D::ReleasePipelines()
{
    PipelineClearPlane.Reset();
    for (auto& pso : PipelineFinalPass)
        pso.Reset();
    PipelineResolve.Reset();
}

void DX12Renderer3D::ReleaseScaleDependentResources()
{
    ResultBuffer.Reset();
    FinalFBBuffer.Reset();
}

bool DX12Renderer3D::CreateScaleDependentResources()
{
    ReleaseScaleDependentResources();

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

    FinalFBBuffer = Context->CreateBuffer(
        pixels * 4ull,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 3D framebuffer");
    if (!FinalFBBuffer)
        return false;

    return true;
}

void DX12Renderer3D::SetRenderSettings(int scale, bool betterPolygons, bool hiresCoordinates)
{
    BetterPolygons = betterPolygons;

    if (scale == ScaleFactor)
    {
        // Like the OpenGL compute renderer, the high-resolution-coordinates
        // toggle must not tear down GPU resources: MelonPrimeDS applies it live
        // during a match.
        HiresCoordinates = hiresCoordinates;
        return;
    }

    Commands.WaitIdle();

    ScaleFactor = scale;
    ScreenWidth = 256 * ScaleFactor;
    ScreenHeight = 192 * ScaleFactor;
    HiresCoordinates = hiresCoordinates;

    ReleasePipelines();
    ShaderStepIdx = 0;

    if (!CreateScaleDependentResources())
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "DX12: failed to allocate render targets for %dx internal resolution\n",
            ScaleFactor);
    }

    FrameInFlight = false;
    FrameReadbackValid = false;
}

bool DX12Renderer3D::BuildPipeline(
    DX12::ComPtr<ID3D12PipelineState>& pipeline,
    const std::string& body,
    const std::vector<std::string>& defines,
    const char* debugName)
{
    pipeline.Reset();

    if (!Context || !RootSignature)
        return false;

    std::string source;
    source.reserve(body.size() + DX12Shaders::Common.size() + 512);
    source += "#define ScreenWidth ";
    source += std::to_string(ScreenWidth);
    source += "u\n#define ScreenHeight ";
    source += std::to_string(ScreenHeight);
    source += "u\n#define ScaleFactor ";
    source += std::to_string(ScaleFactor);
    source += "u\n";
    for (const std::string& define : defines)
    {
        source += "#define ";
        source += define;
        source += " 1\n";
    }
    source += DX12Shaders::Common;
    source += body;

    auto blob = Context->CompileShader(source, "main", "cs_5_1", {}, debugName);
    if (!blob)
        return false;

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = RootSignature.Get();
    desc.CS.pShaderBytecode = blob->GetBufferPointer();
    desc.CS.BytecodeLength = blob->GetBufferSize();
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

    if (step == ShaderStep_ClearPlane)
    {
        BuildPipeline(PipelineClearPlane, DX12Shaders::ClearPlane, {}, "DX12ClearPlane");
        return;
    }

    if (step >= ShaderStep_FinalPass0 && step <= ShaderStep_FinalPassLast)
    {
        const int variant = step - ShaderStep_FinalPass0;
        std::vector<std::string> defines;
        if (variant & 0x1) defines.emplace_back("EdgeMarking");
        if (variant & 0x2) defines.emplace_back("Fog");
        if (variant & 0x4) defines.emplace_back("AntiAliasing");

        char name[48];
        std::snprintf(name, sizeof(name), "DX12FinalPass%d", variant);
        BuildPipeline(PipelineFinalPass[variant], DX12Shaders::FinalPass, defines, name);
        return;
    }

    if (step == ShaderStep_Resolve)
    {
        BuildPipeline(PipelineResolve, DX12Shaders::Resolve, {}, "DX12Resolve");
        return;
    }
}

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

        u64 offset = 0;
        void* mapped = Uploads.Allocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT, offset);
        if (!mapped)
            continue;

        std::memcpy(mapped, ClearBitmap[slot].get(), static_cast<size_t>(totalBytes));

        D3D12_RESOURCE_BARRIER toCopy{};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = ClearBitmapTex[slot].Get();
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

        D3D12_RESOURCE_BARRIER toRead = toCopy;
        toRead.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toRead.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        if (!ClearBitmapTexInCopyDest[slot])
            list->ResourceBarrier(1, &toCopy);

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = ClearBitmapTex[slot].Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = 0;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = Uploads.GetBuffer();
        src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Offset = offset;
        src.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R32_UINT;
        src.PlacedFootprint.Footprint.Width = 256;
        src.PlacedFootprint.Footprint.Height = 256;
        src.PlacedFootprint.Footprint.Depth = 1;
        src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(rowPitch);

        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        list->ResourceBarrier(1, &toRead);
        ClearBitmapTexInCopyDest[slot] = false;
    }

    ClearBitmapDirty = 0;
}

void DX12Renderer3D::UploadMetaUniform(ID3D12GraphicsCommandList* list)
{
    MetaUniform meta{};
    meta.DispCnt = GPU3D.RenderDispCnt;
    meta.NumPolygons = GPU3D.RenderNumPolygons;
    meta.NumVariants = 0;
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

    u64 offset = 0;
    void* mapped = Uploads.Allocate(
        sizeof(MetaUniform), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, offset);
    if (!mapped)
        return;

    std::memcpy(mapped, &meta, sizeof(meta));
    list->SetComputeRootConstantBufferView(1, Uploads.GetBuffer()->GetGPUVirtualAddress() + offset);
}

void DX12Renderer3D::SetDispatchConstants(ID3D12GraphicsCommandList* list, const DispatchUniform& constants)
{
    list->SetComputeRoot32BitConstants(0, DispatchUniformDwords, &constants, 0);
}

void DX12Renderer3D::InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

bool DX12Renderer3D::BindUavTable(
    ID3D12GraphicsCommandList* list,
    std::initializer_list<ViewEntry> entries)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!Descriptors.Allocate(kUavTableSize, cpu, gpu))
        return false;

    // Every slot is written even when a shader binds fewer resources: the last
    // entry is repeated so no descriptor in the table is ever left undefined,
    // which the debug layer reports as an error.
    ViewEntry slots[kUavTableSize]{};
    u32 count = 0;
    for (const ViewEntry& entry : entries)
    {
        if (count >= kUavTableSize) break;
        slots[count++] = entry;
    }
    if (count == 0)
        return false;
    for (u32 i = count; i < kUavTableSize; i++)
        slots[i] = slots[count - 1];

    ID3D12Device* device = Context->GetDevice();
    const u32 increment = Descriptors.GetIncrement();

    for (u32 i = 0; i < kUavTableSize; i++)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC desc{};
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        desc.Buffer.FirstElement = 0;
        desc.Buffer.NumElements = slots[i].Elements;
        desc.Buffer.StructureByteStride = 4;
        desc.Buffer.CounterOffsetInBytes = 0;
        desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

        D3D12_CPU_DESCRIPTOR_HANDLE handle{ cpu.ptr + static_cast<SIZE_T>(i) * increment };
        device->CreateUnorderedAccessView(slots[i].Resource, nullptr, &desc, handle);
    }

    list->SetComputeRootDescriptorTable(3, gpu);
    return true;
}

bool DX12Renderer3D::BindSrvTable(
    ID3D12GraphicsCommandList* list,
    std::initializer_list<ViewEntry> entries)
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    if (!Descriptors.Allocate(kSrvTableSize, cpu, gpu))
        return false;

    ViewEntry slots[kSrvTableSize]{};
    u32 count = 0;
    for (const ViewEntry& entry : entries)
    {
        if (count >= kSrvTableSize) break;
        slots[count++] = entry;
    }
    if (count == 0)
        return false;
    for (u32 i = count; i < kSrvTableSize; i++)
        slots[i] = slots[count - 1];

    ID3D12Device* device = Context->GetDevice();
    const u32 increment = Descriptors.GetIncrement();

    for (u32 i = 0; i < kSrvTableSize; i++)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC desc{};
        desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (slots[i].Elements != 0)
        {
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            desc.Buffer.FirstElement = 0;
            desc.Buffer.NumElements = slots[i].Elements;
            desc.Buffer.StructureByteStride = 4;
            desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }
        else
        {
            desc.Format = DXGI_FORMAT_R32_UINT;
            desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            desc.Texture2D.MostDetailedMip = 0;
            desc.Texture2D.MipLevels = 1;
            desc.Texture2D.PlaneSlice = 0;
            desc.Texture2D.ResourceMinLODClamp = 0.0f;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE handle{ cpu.ptr + static_cast<SIZE_T>(i) * increment };
        device->CreateShaderResourceView(slots[i].Resource, &desc, handle);
    }

    list->SetComputeRootDescriptorTable(2, gpu);
    return true;
}

void DX12Renderer3D::RenderFrame()
{
    if (!Context || !RootSignature || !ResultBuffer || !FinalFBBuffer)
        return;
    if (ShaderStepIdx < ShaderStepCount)
        return; // pipelines are still being compiled

    u8 texcacheClearBitmapDirty = 0;
    Texcache.Update(texcacheClearBitmapDirty);
    ClearBitmapDirty |= texcacheClearBitmapDirty;

    ID3D12GraphicsCommandList* list = Commands.Begin();
    if (!list)
        return;

    // Begin() waited for the previous submission, so both the descriptor ring
    // and the upload ring are free to reuse and retired textures can go.
    Descriptors.Reset();
    Uploads.Reset();
    TextureHeap.CollectGarbage();

    ID3D12DescriptorHeap* heaps[] = { Descriptors.GetHeap() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(RootSignature.Get());

    UpdateClearBitmap();
    TextureHeap.FlushUploadBarriers();
    UploadMetaUniform(list);

    const u32 pixels = static_cast<u32>(ScreenWidth) * static_cast<u32>(ScreenHeight);
    const u32 resultElements = pixels * 3u * 2u;

    DispatchUniform constants{};
    SetDispatchConstants(list, constants);

    // 1. clear plane / clear bitmap
    if (PipelineClearPlane)
    {
        list->SetPipelineState(PipelineClearPlane.Get());
        if (BindUavTable(list, { ViewEntry{ ResultBuffer.Get(), resultElements } })
            && BindSrvTable(list, {
                ViewEntry{ ClearBitmapTex[0].Get(), 0 },
                ViewEntry{ ClearBitmapTex[1].Get(), 0 } }))
        {
            list->Dispatch(DivRoundUp(ScreenWidth, 8), DivRoundUp(ScreenHeight, 8), 1);
        }
        InsertUavBarrier(list, ResultBuffer.Get());
    }

    // 2. final pass: edge marking / fog / anti-aliasing resolve
    u32 finalPassVariant = 0;
    if (GPU3D.RenderDispCnt & (1 << 5)) finalPassVariant |= 0x1; // edge marking
    if (GPU3D.RenderDispCnt & (1 << 7)) finalPassVariant |= 0x2; // fog
    if (GPU3D.RenderDispCnt & (1 << 4)) finalPassVariant |= 0x4; // anti-aliasing

    if (PipelineFinalPass[finalPassVariant])
    {
        list->SetPipelineState(PipelineFinalPass[finalPassVariant].Get());
        if (BindUavTable(list, {
                ViewEntry{ ResultBuffer.Get(), resultElements },
                ViewEntry{ FinalFBBuffer.Get(), pixels } }))
        {
            list->Dispatch(DivRoundUp(ScreenWidth, 32), ScreenHeight, 1);
        }
        InsertUavBarrier(list, FinalFBBuffer.Get());
    }

    // 3. downscale to native resolution in the software compositor's format
    if (PipelineResolve)
    {
        // The resolve shader reads FinalFB as an SRV, so it has to leave the
        // UAV state it was written in.
        D3D12_RESOURCE_BARRIER toSrv{};
        toSrv.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSrv.Transition.pResource = FinalFBBuffer.Get();
        toSrv.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toSrv.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toSrv.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1, &toSrv);

        list->SetPipelineState(PipelineResolve.Get());
        if (BindUavTable(list, { ViewEntry{ ResolveBuffer.Get(), 256u * 192u } })
            && BindSrvTable(list, { ViewEntry{ FinalFBBuffer.Get(), pixels } }))
        {
            list->Dispatch(DivRoundUp(256, 8), DivRoundUp(192, 8), 1);
        }

        D3D12_RESOURCE_BARRIER back = toSrv;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1, &back);
    }

    // 4. copy to the readback heap the software compositor reads from
    {
        D3D12_RESOURCE_BARRIER toCopy{};
        toCopy.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toCopy.Transition.pResource = ResolveBuffer.Get();
        toCopy.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        toCopy.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        toCopy.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        list->ResourceBarrier(1, &toCopy);

        list->CopyBufferRegion(ReadbackBuffer.Get(), 0, ResolveBuffer.Get(), 0, 256ull * 192ull * 4ull);

        D3D12_RESOURCE_BARRIER back = toCopy;
        back.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        back.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        list->ResourceBarrier(1, &back);
    }

    if (Commands.Submit())
    {
        FrameInFlight = true;
        FrameReadbackValid = false;
    }
}

void DX12Renderer3D::EnsureFrameReadback()
{
    if (FrameReadbackValid || !FrameInFlight || !ReadbackBuffer)
        return;

    // Deliberately deferred to the first GetLine() of the frame instead of the
    // end of RenderFrame(): the GPU gets to overlap with whatever the emulation
    // thread does between the two.
    Commands.WaitIdle();

    D3D12_RANGE readRange{ 0, 256ull * 192ull * 4ull };
    void* mapped = nullptr;
    if (SUCCEEDED(ReadbackBuffer->Map(0, &readRange, &mapped)) && mapped)
    {
        std::memcpy(ColorBuffer.data(), mapped, ColorBuffer.size() * sizeof(u32));
        D3D12_RANGE noWrite{ 0, 0 };
        ReadbackBuffer->Unmap(0, &noWrite);
    }

    FrameInFlight = false;
    FrameReadbackValid = true;
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
