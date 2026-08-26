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

#ifndef DX12_PIPELINE_REPOSITORY_H
#define DX12_PIPELINE_REPOSITORY_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <algorithm>
#include <cstddef>
#include <vector>

#include "DX12Common.h"

namespace melonDS
{

class DX12Context;

// The compute root-signature binding contract.
//
// Single source of truth for both the signature layout and the descriptor
// rings bound against it -- the same role VulkanDescriptors' binding table
// plays on the other backend. Sizing a ring from one constant while the
// signature declares another is a silent GPU-side corruption, so the two are
// not allowed to live in separate files.
namespace DX12RootSignatureLayout
{

inline constexpr u32 StaticSrvCount = 5;
inline constexpr u32 TextureSrvCount = 1;
inline constexpr u32 UavTableSize = 14;

inline constexpr u32 ParamDispatchConstants = 0;
inline constexpr u32 ParamMetaCbv = 1;
inline constexpr u32 ParamStaticSrvTable = 2;
inline constexpr u32 ParamTextureSrvTable = 3;
inline constexpr u32 ParamUavTable = 4;
inline constexpr u32 ParamCount = 5;

} // namespace DX12RootSignatureLayout

// Root constants, mirroring the HLSL DispatchUniform cbuffer.
//
// Part of the root-signature contract, so it lives with the layout above
// rather than inside one of the components that binds it: the rasterizer and
// the GPU2D compositor record independent command lists and both set these.
struct DX12DispatchUniform
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
inline constexpr u32 DX12DispatchUniformDwords = sizeof(DX12DispatchUniform) / 4;
static_assert(DX12DispatchUniformDwords <= 64, "DX12 root constants exceed the API limit");

// Command-list recording that is contract, not policy: which root parameter
// the constants go to, and the two barrier shapes every component records.
// Free functions because they carry no state -- the rasterizer, the compositor
// and the capture bridge all record these against their own lists.
inline void DX12SetDispatchConstants(
    ID3D12GraphicsCommandList* list, const DX12DispatchUniform& constants)
{
    list->SetComputeRoot32BitConstants(
        DX12RootSignatureLayout::ParamDispatchConstants,
        DX12DispatchUniformDwords, &constants, 0);
}

inline void DX12InsertUavBarrier(
    ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

inline void DX12InsertUavBarriers(
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

inline void DX12TransitionBuffer(
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

// Slot assignment inside that UAV table.
//
// One fourteen-entry table serves every compute dispatch in the backend, and
// the shaders are compiled against these register indices. Most slots mean the
// same thing to every shader; three do not:
//
//   DispatchInput (0)   the raster path binds the span result buffer here; the
//                       GPU2D paths bind their structured or native input
//   DispatchOutput (8)  the raster path binds the native resolve target; the
//                       compositor binds its composed output, and the
//                       diagnostic path its readback copy
//   DirectOutput (13)   the compositor's sampleable output texture, or a dummy
//                       when the direct path is unavailable
//
// That overloading is why the rasterizer and the GPU2D compositor cannot yet
// be split into components with separate resource ownership: they do not
// merely share buffers, they share register slots whose meaning depends on
// which shader is dispatched. Separating them means giving each its own table
// and recompiling the shaders against it -- a descriptor-strategy change, and
// deliberately not something to fold into a responsibility refactor.
//
// Until then, this is the contract. Every table build site assigns through
// these names so the fourteen entries cannot silently drift out of order: a
// mismatch is not a compile error, it is a shader reading the wrong buffer.
namespace DX12UavSlot
{

inline constexpr u32 DispatchInput = 0;
inline constexpr u32 FinalFB = 1;
inline constexpr u32 TileColor = 2;
inline constexpr u32 TileDepth = 3;
inline constexpr u32 TileAttr = 4;
inline constexpr u32 BinResult = 5;
inline constexpr u32 WorkDesc = 6;
inline constexpr u32 XSpanSetup = 7;
inline constexpr u32 DispatchOutput = 8;
inline constexpr u32 CaptureSidecar = 9;
inline constexpr u32 BlendState = 10;
inline constexpr u32 ResultWinner = 11;
inline constexpr u32 IndirectArgs = 12;
inline constexpr u32 DirectOutput = 13;

inline constexpr u32 Count = DX12RootSignatureLayout::UavTableSize;
static_assert(Count == DirectOutput + 1u,
    "every UAV table slot must be named");

} // namespace DX12UavSlot

// One compute shader's DXBC. Borrowed, never owned: the blobs are static
// program data in the renderer's translation unit.
struct DX12ShaderBytecode
{
    const void* Data = nullptr;
    std::size_t Size = 0;
};

// How a pipeline came to exist. The repository reports it; what to do with
// that -- startup telemetry, cache-hit counters -- is the caller's business.
enum class DX12PipelineBuildResult : int
{
    Failed,
    LibraryHit,    // served from the serialized ID3D12PipelineLibrary
    CachedBlobHit, // driver accepted the per-PSO cached blob
    Compiled,      // the driver built it from bytecode
};

// Owns the DX12 compute pipeline state and everything that persists it:
// the root signature and its hash, the indirect dispatch command signature,
// the ID3D12PipelineLibrary, the per-PSO cached blob fallback, and the two
// on-disk cache files with their validation headers.
//
// Split out of DX12Renderer3D because none of this changes for the same
// reason the rasterizer does. Cache file versioning, driver blob rejection
// handling, vendor workarounds and root-signature layout are their own change
// axis; the renderer only asks for a pipeline and is told where it came from.
//
// Deliberately does not know which shaders exist. The generated blob table is
// a ~190k-line .inc owned by GPU3D_DX12.cpp and must stay in exactly one
// translation unit, so the caller supplies bytecode through a lookup function.
class DX12PipelineRepository
{
public:
    // Bytecode lookup, indexed the way the generated table is.
    using ShaderBlobFn = DX12ShaderBytecode (*)(u32 bucket, u32 variant);

    DX12PipelineRepository() = default;
    ~DX12PipelineRepository() = default;

    DX12PipelineRepository(const DX12PipelineRepository&) = delete;
    DX12PipelineRepository& operator=(const DX12PipelineRepository&) = delete;

    // Call order at renderer init: CreateRootSignature -> CreatePipelineLibrary
    // -> CreateCommandSignature. The library load hashes the root-signature
    // blob, so it cannot run first.
    bool CreateRootSignature(const DX12Context& context, u32 dispatchConstantDwords);

    // Loads both on-disk caches when they match this adapter, driver, root
    // signature, shader set and build flags. Never fails the renderer: a
    // rejected or absent cache just means every pipeline is compiled.
    void CreatePipelineLibrary(
        const DX12Context& context,
        ShaderBlobFn blobs,
        u32 bucketCount,
        u32 variantCount);

    bool CreateCommandSignature(const DX12Context& context);

    // Serializes whatever changed. noexcept and fail-soft: losing a warm cache
    // costs startup time on the next run and nothing else.
    void Save(const DX12Context& context) noexcept;

    // Releases the GPU objects. The cached blob payloads are dropped with
    // them, so a later Save() cannot write a cache that no longer matches.
    void Reset() noexcept;

    [[nodiscard]] DX12PipelineBuildResult BuildComputePipeline(
        const DX12Context& context,
        DX12::ComPtr<ID3D12PipelineState>& pipeline,
        u32 bucket,
        u32 variant,
        DX12ShaderBytecode bytecode);

    [[nodiscard]] ID3D12RootSignature* GetRootSignature() const noexcept
    {
        return RootSignature.Get();
    }
    [[nodiscard]] ID3D12CommandSignature* GetDispatchSignature() const noexcept
    {
        return DispatchSignature.Get();
    }
    // True when the pipeline library was created from a stored payload rather
    // than empty. Startup diagnostics only.
    [[nodiscard]] bool WasLibraryLoadedFromCache() const noexcept
    {
        return PipelineLibraryLoaded;
    }

private:
    void LoadCachedPsoBlobs(const DX12Context& context);
    void SaveCachedPsoBlobs(const DX12Context& context) noexcept;
    void SavePipelineLibrary(const DX12Context& context) noexcept;

    DX12::ComPtr<ID3D12RootSignature> RootSignature;
    DX12::ComPtr<ID3D12CommandSignature> DispatchSignature;
    DX12::ComPtr<ID3D12PipelineLibrary> PipelineLibrary;
    u64 RootSignatureHash = 0;
    u64 ShaderBlobHash = 0;
    u32 BucketCount = 0;
    u32 VariantCount = 0;
    bool PipelineLibraryDirty = false;
    bool PipelineLibraryLoaded = false;
    // BucketCount * VariantCount entries, bucket-major. Sized once by
    // CreatePipelineLibrary(); the on-disk layout is a fixed-width size table
    // followed by the payloads, so the entry count is part of cache validity.
    std::vector<std::vector<u8>> CachedPsoBlobs;
    bool CachedPsoBlobsDirty = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_PIPELINE_REPOSITORY_H
