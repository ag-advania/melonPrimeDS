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

#ifndef GPU3D_TEXCACHE_DX12_H
#define GPU3D_TEXCACHE_DX12_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include <memory>
#include <vector>

#include "GPU3D_Texcache.h"
#include "DX12Context.h"
#include "DX12Perf.h"

namespace melonDS
{

template <typename, typename>
class Texcache;

// Owns every decoded-texture array the texcache hands out. The cache itself
// only ever sees an opaque u32 handle, which keeps its batching comparisons
// (`rp->TexID != texid`) working exactly like the OpenGL backend's GLuint.
//
// Handles are 1-based so 0 stays the "no texture" sentinel the renderer already
// uses.
class DX12TextureHeap
{
public:
    struct Entry
    {
        DX12::ComPtr<ID3D12Resource> Resource;
        u32 Width = 0;
        u32 Height = 0;
        u32 Layers = 0;
        // Tracked so uploads and shader reads insert the right barrier without
        // transitioning every array every frame.
        D3D12_RESOURCE_STATES State = D3D12_RESOURCE_STATE_COMMON;
        bool InUse = false;
        bool PhysicalReady = false;
        bool PendingCreate = false;
    };

    void Init(DX12Context* context, DX12CommandContext* commands, DX12UploadRing* uploads);
    void Shutdown();

    // CPU-side cache misses reserve an opaque identity only. The resource is
    // created by MaterializePendingCreates() before Begin() waits for the
    // previous command-list slot, because creation does not touch frame-local
    // command, descriptor, or upload resources.
    u32 Reserve(u32 width, u32 height, u32 layers);
    TextureMaterializeResult MaterializePendingCreates();
    void ClearRetryableCreationFailure() noexcept
    {
        RetryableCreationFailure = false;
        MaterializeRetryAttempted = true;
    }
    void Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data);
    TextureDecodeTarget BeginTextureUpload(
        u32 handle, u32 width, u32 height, u32 layer);
    void CommitTextureUpload(u32 token) noexcept;
    void CancelTextureUpload(u32 token) noexcept;
    // Texture decoding and logical array allocation happen during the CPU
    // preparation phase, before the raster command allocator can be reused.
    // Uploads therefore queue their decoded bytes until the caller has opened
    // the new command list after the reuse fence retires.
    void RecordPendingUploads();
    void Destroy(u32 handle);

    // Transitions every array that was written this frame into a shader-read
    // state. Called once, after the texcache finished updating.
    void FlushUploadBarriers();

    // Releases resources whose handle was freed while the GPU could still have
    // been referencing them. Call after the command context went idle.
    void CollectGarbage();

    void ResetFailures() noexcept
    {
        CreationFailed = false;
        UploadFailed = false;
        RetryableCreationFailure = false;
        MaterializeRetryAttempted = false;
    }
    void ResetUploadFailure() noexcept { UploadFailed = false; }
    [[nodiscard]] bool HadCreationFailure() const noexcept { return CreationFailed; }
    [[nodiscard]] bool HadUploadFailure() const noexcept { return UploadFailed; }
    [[nodiscard]] bool HadFailure() const noexcept
    {
        return CreationFailed || UploadFailed;
    }

    [[nodiscard]] const Entry* Lookup(u32 handle) const noexcept
    {
        if (handle == 0 || handle > Entries.size()) return nullptr;
        const Entry& entry = Entries[handle - 1];
        return entry.InUse ? &entry : nullptr;
    }

private:
    struct PendingUpload
    {
        u32 Handle = 0;
        u32 Width = 0;
        u32 Height = 0;
        u32 Layer = 0;
        // Decoders overwrite every word. A nothrow high-watermark allocation
        // avoids value-initializing the entire upload before that overwrite.
        std::unique_ptr<u32[]> Data;
        std::size_t CapacityWords = 0;
        std::size_t UsedWords = 0;
        bool Committed = false;
    };

    bool EnsurePendingStorage(PendingUpload& pending, std::size_t words) noexcept;
    TextureMaterializeResult HandleMaterializeFailure(
        TextureMaterializeFailureReason reason) noexcept;
    bool RecordUpload(u32 handle, u32 width, u32 height, u32 layer, const void* data);
    PendingUpload* AcquirePendingUpload(
        u32 handle, u32 width, u32 height, u32 layer, std::size_t words) noexcept;

    DX12Context* Context = nullptr;
    DX12CommandContext* Commands = nullptr;
    DX12UploadRing* Uploads = nullptr;

    std::vector<Entry> Entries;
    std::vector<u32> FreeSlots;
    // Only newly reserved logical entries are visited by materialization.
    std::vector<u32> PendingCreateSlots;
    std::vector<u32> PendingBarriers;
    // The active prefix is drained by count; backing objects and decoded-word
    // high-watermarks stay allocated for reuse instead of churning each frame.
    std::vector<PendingUpload> PendingUploads;
    u32 PendingUploadCount = 0;
    std::vector<DX12::ComPtr<ID3D12Resource>> Graveyard;
    // Oversized/overflow uploads stay alive until the next frame's Begin()
    // retires this command list. This avoids a synchronous mid-frame flush.
    std::vector<DX12::ComPtr<ID3D12Resource>> SpillUploads;
    bool CreationFailed = false;
    bool UploadFailed = false;
    bool RetryableCreationFailure = false;
    bool MaterializeRetryAttempted = false;
};

class TexcacheDX12Loader
{
public:
    explicit TexcacheDX12Loader(DX12TextureHeap* heap = nullptr) : Heap(heap) {}

    void SetHeap(DX12TextureHeap* heap) noexcept { Heap = heap; }

    u32 GenerateTexture(u32 width, u32 height, u32 layers)
    {
        return Heap ? Heap->Reserve(width, height, layers) : 0;
    }

    DX12Perf::ScopedCpuTimer BeginTextureDecode() noexcept
    {
        return DX12Perf::ScopedCpuTimer(
            DX12Perf::CpuMetric::TextureDecode, Heap != nullptr);
    }

    TextureDecodeTarget BeginTextureUpload(
        u32 handle, u32 width, u32 height, u32 layer)
    {
        return Heap ? Heap->BeginTextureUpload(handle, width, height, layer) : TextureDecodeTarget{};
    }

    void CommitTextureUpload(u32 token) noexcept
    {
        if (Heap) Heap->CommitTextureUpload(token);
    }

    void CancelTextureUpload(u32 token) noexcept
    {
        if (Heap) Heap->CancelTextureUpload(token);
    }

    void UploadTexture(u32 handle, u32 width, u32 height, u32 layer, void* data)
    {
        if (Heap) Heap->Upload(handle, width, height, layer, data);
    }

    void DeleteTexture(u32 handle)
    {
        if (Heap) Heap->Destroy(handle);
    }

private:
    DX12TextureHeap* Heap;
};

using TexcacheDX12 = Texcache<TexcacheDX12Loader, u32>;

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // GPU3D_TEXCACHE_DX12_H
