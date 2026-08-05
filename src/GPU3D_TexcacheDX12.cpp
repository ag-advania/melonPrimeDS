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

#include "GPU3D_TexcacheDX12.h"

#include <algorithm>
#include <cstring>

#include "Platform.h"

namespace melonDS
{

namespace
{
constexpr u64 kRowPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;      // 256
constexpr u64 kPlacementAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // 512

constexpr u64 AlignUp(u64 value, u64 alignment) noexcept
{
    return (value + alignment - 1) & ~(alignment - 1);
}
}

void DX12TextureHeap::Init(DX12Context* context, DX12CommandContext* commands, DX12UploadRing* uploads)
{
    Context = context;
    Commands = commands;
    Uploads = uploads;
}

void DX12TextureHeap::Shutdown()
{
    Entries.clear();
    FreeSlots.clear();
    PendingBarriers.clear();
    Graveyard.clear();
    Context = nullptr;
    Commands = nullptr;
    Uploads = nullptr;
}

u32 DX12TextureHeap::Create(u32 width, u32 height, u32 layers)
{
    if (!Context)
        return 0;

    auto resource = Context->CreateTexture2D(
        DXGI_FORMAT_R8G8B8A8_UINT,
        width,
        height,
        layers,
        D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATE_COPY_DEST,
        L"MelonPrime DX12 texcache array");
    if (!resource)
        return 0;

    Entry entry;
    entry.Resource = resource;
    entry.Width = width;
    entry.Height = height;
    entry.Layers = layers;
    entry.State = D3D12_RESOURCE_STATE_COPY_DEST;
    entry.InUse = true;

    u32 slot;
    if (!FreeSlots.empty())
    {
        slot = FreeSlots.back();
        FreeSlots.pop_back();
        Entries[slot] = std::move(entry);
    }
    else
    {
        slot = static_cast<u32>(Entries.size());
        Entries.push_back(std::move(entry));
    }

    return slot + 1;
}

void DX12TextureHeap::Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data)
{
    if (!Commands || !Uploads || handle == 0 || handle > Entries.size())
        return;

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse || !entry.Resource || layer >= entry.Layers)
        return;

    ID3D12GraphicsCommandList* list = Commands->GetList();
    if (!list || !Commands->IsRecording())
        return;

    const u64 srcRowBytes = static_cast<u64>(width) * 4u;
    const u64 dstRowPitch = AlignUp(srcRowBytes, kRowPitchAlignment);
    const u64 totalBytes = dstRowPitch * height;

    u64 offset = 0;
    void* mapped = Uploads->Allocate(totalBytes, kPlacementAlignment, offset);
    if (!mapped)
    {
        // The ring filled up mid-frame. Submitting and waiting is expensive but
        // strictly better than dropping the texture and rendering garbage.
        if (!Commands->Flush())
            return;
        Uploads->Reset();
        // Every array the previous list transitioned is retired now; the state
        // tracking below still describes the resources correctly.
        PendingBarriers.clear();
        mapped = Uploads->Allocate(totalBytes, kPlacementAlignment, offset);
        if (!mapped)
        {
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: texture upload of %llu bytes exceeds the upload ring (%llu bytes)\n",
                static_cast<unsigned long long>(totalBytes),
                static_cast<unsigned long long>(Uploads->GetCapacity()));
            return;
        }
        list = Commands->GetList();
        if (!list) return;
    }

    const u8* src = static_cast<const u8*>(data);
    u8* dst = static_cast<u8*>(mapped);
    for (u32 y = 0; y < height; y++)
        std::memcpy(dst + y * dstRowPitch, src + y * srcRowBytes, static_cast<size_t>(srcRowBytes));

    if (entry.State != D3D12_RESOURCE_STATE_COPY_DEST)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = entry.Resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = entry.State;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1, &barrier);
        entry.State = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = entry.Resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = layer; // one mip level, so subresource == array slice

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = Uploads->GetBuffer();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint.Offset = offset;
    srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UINT;
    srcLoc.PlacedFootprint.Footprint.Width = width;
    srcLoc.PlacedFootprint.Footprint.Height = height;
    srcLoc.PlacedFootprint.Footprint.Depth = 1;
    srcLoc.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(dstRowPitch);

    list->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    const u32 slot = handle - 1;
    if (std::find(PendingBarriers.begin(), PendingBarriers.end(), slot) == PendingBarriers.end())
        PendingBarriers.push_back(slot);
}

void DX12TextureHeap::FlushUploadBarriers()
{
    if (PendingBarriers.empty() || !Commands || !Commands->IsRecording())
    {
        PendingBarriers.clear();
        return;
    }

    ID3D12GraphicsCommandList* list = Commands->GetList();
    if (!list)
    {
        PendingBarriers.clear();
        return;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(PendingBarriers.size());

    for (u32 slot : PendingBarriers)
    {
        if (slot >= Entries.size()) continue;
        Entry& entry = Entries[slot];
        if (!entry.InUse || !entry.Resource) continue;
        if (entry.State == D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) continue;

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = entry.Resource.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = entry.State;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers.push_back(barrier);

        entry.State = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }

    if (!barriers.empty())
        list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());

    PendingBarriers.clear();
}

void DX12TextureHeap::Destroy(u32 handle)
{
    if (handle == 0 || handle > Entries.size())
        return;

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse)
        return;

    // The cache can free a texture while the previous frame's command list is
    // still in flight, so ownership moves to the graveyard instead of being
    // dropped here.
    if (entry.Resource)
        Graveyard.push_back(std::move(entry.Resource));

    entry = Entry{};
    FreeSlots.push_back(handle - 1);

    PendingBarriers.erase(
        std::remove(PendingBarriers.begin(), PendingBarriers.end(), handle - 1),
        PendingBarriers.end());
}

void DX12TextureHeap::CollectGarbage()
{
    Graveyard.clear();
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
