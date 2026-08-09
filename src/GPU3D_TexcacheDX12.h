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

#include <vector>

#include "GPU3D_Texcache.h"
#include "DX12Context.h"

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
    };

    void Init(DX12Context* context, DX12CommandContext* commands, DX12UploadRing* uploads);
    void Shutdown();

    u32 Create(u32 width, u32 height, u32 layers);
    void Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data);
    void Destroy(u32 handle);

    // Transitions every array that was written this frame into a shader-read
    // state. Called once, after the texcache finished updating.
    void FlushUploadBarriers();

    // Releases resources whose handle was freed while the GPU could still have
    // been referencing them. Call after the command context went idle.
    void CollectGarbage();

    [[nodiscard]] const Entry* Lookup(u32 handle) const noexcept
    {
        if (handle == 0 || handle > Entries.size()) return nullptr;
        const Entry& entry = Entries[handle - 1];
        return entry.InUse ? &entry : nullptr;
    }

private:
    DX12Context* Context = nullptr;
    DX12CommandContext* Commands = nullptr;
    DX12UploadRing* Uploads = nullptr;

    std::vector<Entry> Entries;
    std::vector<u32> FreeSlots;
    std::vector<u32> PendingBarriers;
    std::vector<DX12::ComPtr<ID3D12Resource>> Graveyard;
    // Oversized/overflow uploads stay alive until the next frame's Begin()
    // retires this command list. This avoids a synchronous mid-frame flush.
    std::vector<DX12::ComPtr<ID3D12Resource>> SpillUploads;
};

class TexcacheDX12Loader
{
public:
    explicit TexcacheDX12Loader(DX12TextureHeap* heap = nullptr) : Heap(heap) {}

    void SetHeap(DX12TextureHeap* heap) noexcept { Heap = heap; }

    u32 GenerateTexture(u32 width, u32 height, u32 layers)
    {
        return Heap ? Heap->Create(width, height, layers) : 0;
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
