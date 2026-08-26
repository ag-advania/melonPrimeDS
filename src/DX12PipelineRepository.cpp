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

#include "DX12PipelineRepository.h"

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "DX12Context.h"
#include "GPU2DNative.h"
#include "Platform.h"

namespace melonDS
{

namespace
{

constexpr const char* kPipelineLibraryFileName =
    "melonPrimeDS_dx12_pipeline_library.bin";
constexpr const char* kCachedPsoBlobFileName =
    "melonPrimeDS_dx12_cached_pso.bin";
constexpr u32 kPipelineLibraryMagic = 0x4D504C44u; // DLPM
constexpr u32 kPipelineLibraryVersion = 4u;
constexpr u32 kCachedPsoBlobMagic = 0x4F53504Du; // MPSO
constexpr u32 kCachedPsoBlobVersion = 1u;

struct PipelineLibraryFileHeader
{
    u32 Magic = kPipelineLibraryMagic;
    u32 Version = kPipelineLibraryVersion;
    u32 VendorId = 0;
    u32 DeviceId = 0;
    u64 AdapterLuid = 0;
    u64 DriverVersion = 0;
    u64 RootSignatureHash = 0;
    u64 ShaderBlobHash = 0;
    u32 NativeAbiVersion = GPU2DNative::PackedFrameAbiVersion;
    u32 VariantCount = 0;
    u32 BuildFlags = 0;
    u32 PayloadBytes = 0;
};

struct CachedPsoBlobFileHeader
{
    u32 Magic = kCachedPsoBlobMagic;
    u32 Version = kCachedPsoBlobVersion;
    u32 VendorId = 0;
    u32 DeviceId = 0;
    u64 AdapterLuid = 0;
    u64 DriverVersion = 0;
    u64 RootSignatureHash = 0;
    u64 ShaderBlobHash = 0;
    u32 NativeAbiVersion = GPU2DNative::PackedFrameAbiVersion;
    u32 VariantCount = 0;
    u32 BuildFlags = 0;
    u32 EntryCount = 0;
};

u64 HashBytes(u64 hash, const void* data, std::size_t size) noexcept
{
    constexpr u64 kFnvPrime = 1099511628211ull;
    const u8* bytes = static_cast<const u8*>(data);
    for (std::size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
    return hash;
}

constexpr u64 kFnvOffsetBasis = 1469598103934665603ull;

constexpr u32 PipelineLibraryBuildFlags() noexcept
{
    u32 flags = 0;
#if !defined(NDEBUG)
    flags |= 1u;
#endif
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
    flags |= 2u;
#endif
    return flags;
}

} // namespace

bool DX12PipelineRepository::CreateRootSignature(
    const DX12Context& context, u32 dispatchConstantDwords)
{
    namespace Layout = DX12RootSignatureLayout;

    const auto& entry = DX12::LoadEntryPoints();
    if (!entry.D3D12SerializeRootSignature)
        return false;

    D3D12_DESCRIPTOR_RANGE staticSrvRange{};
    staticSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    staticSrvRange.NumDescriptors = Layout::StaticSrvCount;
    staticSrvRange.BaseShaderRegister = 0;
    staticSrvRange.RegisterSpace = 0;
    staticSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE textureSrvRange{};
    textureSrvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    textureSrvRange.NumDescriptors = Layout::TextureSrvCount;
    textureSrvRange.BaseShaderRegister = 5;
    textureSrvRange.RegisterSpace = 0;
    textureSrvRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_DESCRIPTOR_RANGE uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = Layout::UavTableSize;
    uavRange.BaseShaderRegister = 0;
    uavRange.RegisterSpace = 0;
    uavRange.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER params[Layout::ParamCount]{};

    params[Layout::ParamDispatchConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[Layout::ParamDispatchConstants].Constants.ShaderRegister = 0;
    params[Layout::ParamDispatchConstants].Constants.RegisterSpace = 0;
    params[Layout::ParamDispatchConstants].Constants.Num32BitValues = dispatchConstantDwords;
    params[Layout::ParamDispatchConstants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[Layout::ParamMetaCbv].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
    params[Layout::ParamMetaCbv].Descriptor.ShaderRegister = 1;
    params[Layout::ParamMetaCbv].Descriptor.RegisterSpace = 0;
    params[Layout::ParamMetaCbv].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[Layout::ParamStaticSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[Layout::ParamStaticSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    params[Layout::ParamStaticSrvTable].DescriptorTable.pDescriptorRanges = &staticSrvRange;
    params[Layout::ParamStaticSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[Layout::ParamTextureSrvTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[Layout::ParamTextureSrvTable].DescriptorTable.NumDescriptorRanges = 1;
    params[Layout::ParamTextureSrvTable].DescriptorTable.pDescriptorRanges = &textureSrvRange;
    params[Layout::ParamTextureSrvTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    params[Layout::ParamUavTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[Layout::ParamUavTable].DescriptorTable.NumDescriptorRanges = 1;
    params[Layout::ParamUavTable].DescriptorTable.pDescriptorRanges = &uavRange;
    params[Layout::ParamUavTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_DESC desc{};
    desc.NumParameters = Layout::ParamCount;
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

    RootSignatureHash = HashBytes(
        kFnvOffsetBasis, blob->GetBufferPointer(), blob->GetBufferSize());

    hr = context.GetDevice()->CreateRootSignature(
        0,
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        IID_PPV_ARGS(RootSignature.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateRootSignature", hr);

    return true;
}

void DX12PipelineRepository::CreatePipelineLibrary(
    const DX12Context& context,
    ShaderBlobFn blobs,
    u32 bucketCount,
    u32 variantCount)
{
    PipelineLibrary.Reset();
    PipelineLibraryDirty = false;
    PipelineLibraryLoaded = false;
    BucketCount = bucketCount;
    VariantCount = variantCount;
    CachedPsoBlobs.assign(
        static_cast<std::size_t>(bucketCount) * variantCount, std::vector<u8>{});
    ShaderBlobHash = kFnvOffsetBasis;
    if (blobs)
    {
        for (u32 bucket = 0; bucket < bucketCount; ++bucket)
        {
            for (u32 variant = 0; variant < variantCount; ++variant)
            {
                const DX12ShaderBytecode blob = blobs(bucket, variant);
                ShaderBlobHash = HashBytes(ShaderBlobHash, blob.Data, blob.Size);
            }
        }
    }

    if (!context.GetDevice() || RootSignatureHash == 0u)
        return;
    LoadCachedPsoBlobs(context);
    const DX12Context::DeviceProfile& profile = context.GetDeviceProfile();
#if defined(MELONPRIME_DX12_ENABLE_DEBUG_LAYER)
    // Debug-layer library ingestion is not cancellable and has blocked inside
    // the NVIDIA driver. Per-PSO cached blobs remain active in this build.
    return;
#endif
    if (profile.VendorId == 0x10DEu)
    {
        // Current NVIDIA drivers can likewise block in CreatePipelineLibrary
        // with serialized data. Use the fail-soft per-PSO blob cache instead.
        return;
    }
    DX12::ComPtr<ID3D12Device1> device1;
    if (FAILED(context.GetDevice()->QueryInterface(
            IID_PPV_ARGS(device1.ReleaseAndGetAddressOf()))))
        return;

    std::vector<u8> payload;
    PipelineLibraryFileHeader header{};
    if (Platform::FileHandle* file = Platform::OpenLocalFile(
            kPipelineLibraryFileName, Platform::FileMode::Read))
    {
        const u64 fileBytes = Platform::FileLength(file);
        if (Platform::FileRead(&header, sizeof(header), 1, file) == 1
            && header.Magic == kPipelineLibraryMagic
            && header.Version == kPipelineLibraryVersion
            && header.VendorId == profile.VendorId
            && header.DeviceId == profile.DeviceId
            && header.AdapterLuid == profile.AdapterLuid
            && header.DriverVersion == profile.DriverVersion
            && header.RootSignatureHash == RootSignatureHash
            && header.ShaderBlobHash == ShaderBlobHash
            && header.NativeAbiVersion == GPU2DNative::PackedFrameAbiVersion
            && header.VariantCount == VariantCount
            && header.BuildFlags == PipelineLibraryBuildFlags()
            && fileBytes == sizeof(header) + header.PayloadBytes)
        {
            payload.resize(header.PayloadBytes);
            if (header.PayloadBytes != 0u
                && Platform::FileRead(payload.data(), header.PayloadBytes, 1, file) != 1)
                payload.clear();
        }
        Platform::CloseFile(file);
    }

    HRESULT hr = device1->CreatePipelineLibrary(
        payload.empty() ? nullptr : payload.data(), payload.size(),
        IID_PPV_ARGS(PipelineLibrary.ReleaseAndGetAddressOf()));
    PipelineLibraryLoaded = SUCCEEDED(hr) && !payload.empty();
    if (FAILED(hr) && !payload.empty())
    {
        // Driver or cache rejection is never a renderer failure. Start with
        // an empty library and let regular PSO creation repopulate it.
        hr = device1->CreatePipelineLibrary(
            nullptr, 0, IID_PPV_ARGS(PipelineLibrary.ReleaseAndGetAddressOf()));
    }
    if (FAILED(hr))
        PipelineLibrary.Reset();
}

void DX12PipelineRepository::LoadCachedPsoBlobs(const DX12Context& context)
{
    for (std::vector<u8>& blob : CachedPsoBlobs)
        blob.clear();
    CachedPsoBlobsDirty = false;

    Platform::FileHandle* file = Platform::OpenLocalFile(
        kCachedPsoBlobFileName, Platform::FileMode::Read);
    if (!file)
        return;
    CachedPsoBlobFileHeader header{};
    std::vector<u32> sizes(CachedPsoBlobs.size(), 0u);
    const std::size_t sizeTableBytes = sizes.size() * sizeof(u32);
    const DX12Context::DeviceProfile& profile = context.GetDeviceProfile();
    bool valid = Platform::FileRead(&header, sizeof(header), 1, file) == 1
        && header.Magic == kCachedPsoBlobMagic
        && header.Version == kCachedPsoBlobVersion
        && header.VendorId == profile.VendorId
        && header.DeviceId == profile.DeviceId
        && header.AdapterLuid == profile.AdapterLuid
        && header.DriverVersion == profile.DriverVersion
        && header.RootSignatureHash == RootSignatureHash
        && header.ShaderBlobHash == ShaderBlobHash
        && header.NativeAbiVersion == GPU2DNative::PackedFrameAbiVersion
        && header.VariantCount == VariantCount
        && header.BuildFlags == PipelineLibraryBuildFlags()
        && header.EntryCount == sizes.size()
        && sizeTableBytes != 0
        && Platform::FileRead(sizes.data(), sizeTableBytes, 1, file) == 1;
    u64 expectedBytes = sizeof(header) + sizeTableBytes;
    if (valid)
    {
        for (u32 size : sizes)
        {
            if (size > 64u * 1024u * 1024u)
            {
                valid = false;
                break;
            }
            expectedBytes += size;
        }
        valid = valid && expectedBytes == Platform::FileLength(file);
    }
    if (valid)
    {
        for (std::size_t i = 0; i < CachedPsoBlobs.size(); ++i)
        {
            CachedPsoBlobs[i].resize(sizes[i]);
            if (sizes[i] != 0u
                && Platform::FileRead(
                    CachedPsoBlobs[i].data(), sizes[i], 1, file) != 1)
            {
                valid = false;
                break;
            }
        }
    }
    Platform::CloseFile(file);
    if (!valid)
    {
        for (std::vector<u8>& blob : CachedPsoBlobs)
            blob.clear();
    }
}

void DX12PipelineRepository::SaveCachedPsoBlobs(const DX12Context& context) noexcept
{
    if (!CachedPsoBlobsDirty || CachedPsoBlobs.empty())
        return;
    const DX12Context::DeviceProfile& profile = context.GetDeviceProfile();
    CachedPsoBlobFileHeader header{};
    header.VendorId = profile.VendorId;
    header.DeviceId = profile.DeviceId;
    header.AdapterLuid = profile.AdapterLuid;
    header.DriverVersion = profile.DriverVersion;
    header.RootSignatureHash = RootSignatureHash;
    header.ShaderBlobHash = ShaderBlobHash;
    header.VariantCount = VariantCount;
    header.BuildFlags = PipelineLibraryBuildFlags();
    header.EntryCount = static_cast<u32>(CachedPsoBlobs.size());
    std::vector<u32> sizes(CachedPsoBlobs.size(), 0u);
    for (std::size_t i = 0; i < CachedPsoBlobs.size(); ++i)
    {
        if (CachedPsoBlobs[i].size() > UINT32_MAX)
            return;
        sizes[i] = static_cast<u32>(CachedPsoBlobs[i].size());
    }

    Platform::FileHandle* file = Platform::OpenLocalFile(
        kCachedPsoBlobFileName, Platform::FileMode::Write);
    if (!file)
        return;
    bool written = Platform::FileWrite(&header, sizeof(header), 1, file) == 1
        && Platform::FileWrite(
            sizes.data(), sizes.size() * sizeof(u32), 1, file) == 1;
    for (const std::vector<u8>& blob : CachedPsoBlobs)
    {
        if (written && !blob.empty())
            written = Platform::FileWrite(blob.data(), blob.size(), 1, file) == 1;
    }
    Platform::CloseFile(file);
    if (written)
        CachedPsoBlobsDirty = false;
}

void DX12PipelineRepository::SavePipelineLibrary(const DX12Context& context) noexcept
{
    if (!PipelineLibrary || !PipelineLibraryDirty)
        return;
    const SIZE_T payloadBytes = PipelineLibrary->GetSerializedSize();
    if (payloadBytes == 0u || payloadBytes > UINT32_MAX)
        return;
    std::vector<u8> payload(payloadBytes);
    if (FAILED(PipelineLibrary->Serialize(payload.data(), payload.size())))
        return;

    const DX12Context::DeviceProfile& profile = context.GetDeviceProfile();
    PipelineLibraryFileHeader header{};
    header.VendorId = profile.VendorId;
    header.DeviceId = profile.DeviceId;
    header.AdapterLuid = profile.AdapterLuid;
    header.DriverVersion = profile.DriverVersion;
    header.RootSignatureHash = RootSignatureHash;
    header.ShaderBlobHash = ShaderBlobHash;
    header.VariantCount = VariantCount;
    header.BuildFlags = PipelineLibraryBuildFlags();
    header.PayloadBytes = static_cast<u32>(payload.size());

    Platform::FileHandle* file = Platform::OpenLocalFile(
        kPipelineLibraryFileName, Platform::FileMode::Write);
    if (!file)
        return;
    const bool written = Platform::FileWrite(&header, sizeof(header), 1, file) == 1
        && Platform::FileWrite(payload.data(), payload.size(), 1, file) == 1;
    Platform::CloseFile(file);
    if (written)
        PipelineLibraryDirty = false;
}

void DX12PipelineRepository::Save(const DX12Context& context) noexcept
{
    SaveCachedPsoBlobs(context);
    SavePipelineLibrary(context);
}

void DX12PipelineRepository::Reset() noexcept
{
    DispatchSignature.Reset();
    PipelineLibrary.Reset();
    RootSignature.Reset();
    PipelineLibraryDirty = false;
    PipelineLibraryLoaded = false;
    CachedPsoBlobs.clear();
    CachedPsoBlobsDirty = false;
    RootSignatureHash = 0;
    ShaderBlobHash = 0;
    BucketCount = 0;
    VariantCount = 0;
}

bool DX12PipelineRepository::CreateCommandSignature(const DX12Context& context)
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
    const HRESULT hr = context.GetDevice()->CreateCommandSignature(
        &desc, nullptr, IID_PPV_ARGS(DispatchSignature.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateCommandSignature", hr);

    return true;
}

DX12PipelineBuildResult DX12PipelineRepository::BuildComputePipeline(
    const DX12Context& context,
    DX12::ComPtr<ID3D12PipelineState>& pipeline,
    u32 bucket,
    u32 variant,
    DX12ShaderBytecode bytecode)
{
    pipeline.Reset();

    if (!context.GetDevice() || !RootSignature)
        return DX12PipelineBuildResult::Failed;
    if (!bytecode.Data || bytecode.Size == 0)
        return DX12PipelineBuildResult::Failed;

    D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
    desc.pRootSignature = RootSignature.Get();
    desc.CS.pShaderBytecode = bytecode.Data;
    desc.CS.BytecodeLength = bytecode.Size;
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    wchar_t cacheKey[48]{};
    std::swprintf(cacheKey, std::size(cacheKey),
        L"MelonPrime_b%u_v%u", bucket, variant);
    if (PipelineLibrary
        && SUCCEEDED(PipelineLibrary->LoadComputePipeline(
            cacheKey, &desc,
            IID_PPV_ARGS(pipeline.ReleaseAndGetAddressOf()))))
    {
        return DX12PipelineBuildResult::LibraryHit;
    }
    pipeline.Reset();

    const std::size_t cachedBlobIndex =
        static_cast<std::size_t>(bucket) * VariantCount + variant;
    if (cachedBlobIndex >= CachedPsoBlobs.size())
        return DX12PipelineBuildResult::Failed;
    std::vector<u8>& cachedBlob = CachedPsoBlobs[cachedBlobIndex];
    desc.CachedPSO.pCachedBlob = cachedBlob.empty() ? nullptr : cachedBlob.data();
    desc.CachedPSO.CachedBlobSizeInBytes = cachedBlob.size();
    HRESULT hr = context.GetDevice()->CreateComputePipelineState(
        &desc, IID_PPV_ARGS(pipeline.ReleaseAndGetAddressOf()));
    bool cachedBlobUsed = SUCCEEDED(hr) && !cachedBlob.empty();
    if (FAILED(hr) && !cachedBlob.empty())
    {
        // Cached blobs are driver-private. Rejection is an ordinary cache miss,
        // never a renderer failure or a reason to retain the stale payload.
        cachedBlob.clear();
        CachedPsoBlobsDirty = true;
        desc.CachedPSO = {};
        hr = context.GetDevice()->CreateComputePipelineState(
            &desc, IID_PPV_ARGS(pipeline.ReleaseAndGetAddressOf()));
        cachedBlobUsed = false;
    }
    if (FAILED(hr))
    {
        DX12::Fail("CreateComputePipelineState", hr);
        return DX12PipelineBuildResult::Failed;
    }

    if (PipelineLibrary
        && SUCCEEDED(PipelineLibrary->StorePipeline(cacheKey, pipeline.Get())))
        PipelineLibraryDirty = true;

    DX12::ComPtr<ID3DBlob> createdBlob;
    if (SUCCEEDED(pipeline->GetCachedBlob(createdBlob.ReleaseAndGetAddressOf()))
        && createdBlob && createdBlob->GetBufferSize() <= UINT32_MAX)
    {
        const u8* begin = static_cast<const u8*>(createdBlob->GetBufferPointer());
        const std::size_t size = createdBlob->GetBufferSize();
        if (cachedBlob.size() != size
            || (size != 0u && std::memcmp(cachedBlob.data(), begin, size) != 0))
        {
            cachedBlob.assign(begin, begin + size);
            CachedPsoBlobsDirty = true;
        }
    }

    return cachedBlobUsed
        ? DX12PipelineBuildResult::CachedBlobHit
        : DX12PipelineBuildResult::Compiled;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
