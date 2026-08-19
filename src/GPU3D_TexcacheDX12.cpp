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
#include "DX12Perf.h"

#include <algorithm>
#include <cstring>

#include "Platform.h"

namespace melonDS
{

namespace
{
constexpr u64 kRowPitchAlignment = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;      // 256
constexpr u64 kPlacementAlignment = D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT; // 512
constexpr u32 kInitialPendingUploadCapacity = 64;
constexpr u32 kInitialPendingCreateCapacity = 64;

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
    CreationFailed = false;
    UploadFailed = false;
    PendingUploadCount = 0;
    PendingCreateSlots.reserve(kInitialPendingCreateCapacity);
    PendingUploads.reserve(kInitialPendingUploadCapacity);
}

void DX12TextureHeap::Shutdown()
{
    Entries.clear();
    FreeSlots.clear();
    PendingCreateSlots.clear();
    PendingBarriers.clear();
    PendingUploads.clear();
    PendingUploadCount = 0;
    Graveyard.clear();
    SpillUploads.clear();
    Context = nullptr;
    Commands = nullptr;
    Uploads = nullptr;
    CreationFailed = false;
    UploadFailed = false;
}

u32 DX12TextureHeap::Reserve(u32 width, u32 height, u32 layers)
{
    if (!Context || width == 0 || height == 0 || layers == 0)
    {
        CreationFailed = true;
        return 0;
    }

    Entry entry;
    entry.Width = width;
    entry.Height = height;
    entry.Layers = layers;
    entry.State = D3D12_RESOURCE_STATE_COMMON;
    entry.InUse = true;
    entry.PendingCreate = true;

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

    PendingCreateSlots.push_back(slot);

    return slot + 1;
}

bool DX12TextureHeap::MaterializePendingCreates()
{
    if (CreationFailed)
        return false;
    if (!Context)
    {
        CreationFailed = true;
        return false;
    }

    for (u32 slot : PendingCreateSlots)
    {
        if (slot >= Entries.size())
            continue;

        Entry& entry = Entries[slot];
        if (!entry.InUse || !entry.PendingCreate)
            continue;

        DX12Perf::ScopedCpuTimer createTimer(
            DX12Perf::CpuMetric::TextureResourceCreate);
        auto resource = Context->CreateTexture2D(
            DXGI_FORMAT_R8G8B8A8_UINT,
            entry.Width,
            entry.Height,
            entry.Layers,
            D3D12_RESOURCE_FLAG_NONE,
            D3D12_RESOURCE_STATE_COPY_DEST,
            L"MelonPrime DX12 texcache array");
        if (!resource)
        {
            CreationFailed = true;
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: could not materialize texture array %ux%ux%u\n",
                entry.Width, entry.Height, entry.Layers);
            return false;
        }

        entry.Resource = std::move(resource);
        entry.State = D3D12_RESOURCE_STATE_COPY_DEST;
        entry.PhysicalReady = true;
        entry.PendingCreate = false;
        DX12Perf::AddCounter(DX12Perf::Counter::TextureMaterializeCount);
    }

    PendingCreateSlots.clear();

    return true;
}

DX12TextureHeap::PendingUpload* DX12TextureHeap::AcquirePendingUpload(
    u32 handle, u32 width, u32 height, u32 layer, std::size_t words)
{
    if (PendingUploadCount == PendingUploads.size())
        PendingUploads.emplace_back();

    PendingUpload& pending = PendingUploads[PendingUploadCount];
    pending.Handle = handle;
    pending.Width = width;
    pending.Height = height;
    pending.Layer = layer;
    const bool storageGrows = pending.Data.capacity() < words;
    {
        DX12Perf::ScopedCpuTimer growTimer(
            DX12Perf::CpuMetric::TexturePendingStorageGrow, storageGrows);
        pending.Data.resize(words);
    }
    if (storageGrows)
    {
        DX12Perf::AddCounter(DX12Perf::Counter::TexturePendingStorageGrowCount);
        DX12Perf::AddCounter(
            DX12Perf::Counter::TexturePendingStorageGrowBytes,
            pending.Data.capacity() * sizeof(u32));
    }
    pending.Committed = false;
    PendingUploadCount++;
    return &pending;
}

TextureDecodeTarget DX12TextureHeap::BeginTextureUpload(
    u32 handle, u32 width, u32 height, u32 layer)
{
    if (handle == 0 || handle > Entries.size())
    {
        UploadFailed = true;
        return {};
    }

    const Entry& entry = Entries[handle - 1];
    if (!entry.InUse || layer >= entry.Layers || width != entry.Width || height != entry.Height)
    {
        UploadFailed = true;
        return {};
    }

    const std::size_t words = static_cast<std::size_t>(width) * height;
    PendingUpload* pending = AcquirePendingUpload(handle, width, height, layer, words);
    return {
        pending->Data.data(), pending->Data.size() * sizeof(u32), PendingUploadCount};
}

void DX12TextureHeap::CommitTextureUpload(u32 token) noexcept
{
    if (token == 0 || token > PendingUploadCount)
    {
        UploadFailed = true;
        return;
    }

    PendingUpload& pending = PendingUploads[token - 1];
    if (pending.Committed)
    {
        UploadFailed = true;
        return;
    }
    pending.Committed = true;
    DX12Perf::AddCounter(
        DX12Perf::Counter::TexturePendingUploadBytes,
        pending.Data.size() * sizeof(u32));
    DX12Perf::AddCounter(DX12Perf::Counter::TexturePendingUploadCount);
}

void DX12TextureHeap::CancelTextureUpload(u32 token) noexcept
{
    if (token == 0 || token > PendingUploadCount)
        return;

    const u32 index = token - 1;
    const u32 last = PendingUploadCount - 1;
    if (index != last)
        std::swap(PendingUploads[index], PendingUploads[last]);
    PendingUploads[last].Committed = false;
    PendingUploadCount = last;
}

void DX12TextureHeap::Upload(u32 handle, u32 width, u32 height, u32 layer, const void* data)
{
    if (!Commands || !data || handle == 0 || handle > Entries.size())
    {
        UploadFailed = true;
        return;
    }

    const Entry& entry = Entries[handle - 1];
    if (!entry.InUse || layer >= entry.Layers || width != entry.Width || height != entry.Height)
    {
        UploadFailed = true;
        return;
    }

    if (!Commands->IsRecording())
    {
        const std::size_t words = static_cast<std::size_t>(width) * height;
        PendingUpload* pending = AcquirePendingUpload(handle, width, height, layer, words);
        DX12Perf::ScopedCpuTimer copyTimer(DX12Perf::CpuMetric::TexturePendingCpuCopy);
        const std::size_t bytes = pending->Data.size() * sizeof(u32);
        std::memcpy(pending->Data.data(), data, bytes);
        pending->Committed = true;
        DX12Perf::AddCounter(DX12Perf::Counter::TexturePendingUploadBytes, bytes);
        DX12Perf::AddCounter(DX12Perf::Counter::TexturePendingUploadCount);
        return;
    }

    if (!RecordUpload(handle, width, height, layer, data))
        UploadFailed = true;
}

void DX12TextureHeap::RecordPendingUploads()
{
    if (PendingUploadCount == 0 || !Commands || !Commands->IsRecording())
        return;

    for (u32 i = 0; i < PendingUploadCount; i++)
    {
        PendingUpload& pending = PendingUploads[i];
        if (pending.Committed && !RecordUpload(
            pending.Handle, pending.Width, pending.Height, pending.Layer,
            pending.Data.data()))
        {
            UploadFailed = true;
        }
        pending.Committed = false;
    }
    PendingUploadCount = 0;
}

bool DX12TextureHeap::RecordUpload(
    u32 handle, u32 width, u32 height, u32 layer, const void* data)
{
    if (!Commands || !Uploads || handle == 0 || handle > Entries.size())
        return false;

    Entry& entry = Entries[handle - 1];
    if (!entry.InUse || !entry.PhysicalReady || !entry.Resource || layer >= entry.Layers
        || width != entry.Width || height != entry.Height)
        return false;

    ID3D12GraphicsCommandList* list = Commands->GetList();
    if (!list || !Commands->IsRecording())
        return false;

    const u64 srcRowBytes = static_cast<u64>(width) * 4u;
    const u64 dstRowPitch = AlignUp(srcRowBytes, kRowPitchAlignment);
    const u64 totalBytes = dstRowPitch * height;
    DX12Perf::AddCounter(DX12Perf::Counter::TextureUploadBytes, totalBytes);

    u64 offset = 0;
    ID3D12Resource* uploadBuffer = Uploads->GetBuffer();
    DX12::ComPtr<ID3D12Resource> spillUpload;
    void* mapped = Uploads->Allocate(totalBytes, kPlacementAlignment, offset);
    if (!mapped)
    {
        DX12Perf::AddCounter(DX12Perf::Counter::UploadOverflowCount);
        // Keep recording on the current list. A dedicated upload resource is
        // retained until the next frame retires this submission, eliminating
        // the former Submit -> WaitIdle -> Begin pipeline bubble.
        spillUpload = Context->CreateBuffer(
            totalBytes,
            D3D12_HEAP_TYPE_UPLOAD,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_FLAG_NONE,
            L"MelonPrime DX12 texcache spill upload");
        if (!spillUpload)
        {
            UploadFailed = true;
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: could not allocate texture spill upload of %llu bytes\n",
                static_cast<unsigned long long>(totalBytes));
            return false;
        }
        D3D12_RANGE noRead{0, 0};
        if (FAILED(spillUpload->Map(0, &noRead, &mapped)) || !mapped)
        {
            UploadFailed = true;
            Platform::Log(
                Platform::LogLevel::Error,
                "DX12: could not map texture spill upload of %llu bytes\n",
                static_cast<unsigned long long>(totalBytes));
            return false;
        }
        offset = 0;
        uploadBuffer = spillUpload.Get();
        DX12Perf::AddCounter(DX12Perf::Counter::UploadSpillBytes, totalBytes);
    }

    const u8* src = static_cast<const u8*>(data);
    u8* dst = static_cast<u8*>(mapped);
    for (u32 y = 0; y < height; y++)
        std::memcpy(dst + y * dstRowPitch, src + y * srcRowBytes, static_cast<size_t>(srcRowBytes));
    if (spillUpload)
    {
        D3D12_RANGE written{0, static_cast<SIZE_T>(totalBytes)};
        spillUpload->Unmap(0, &written);
        SpillUploads.push_back(std::move(spillUpload));
    }

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
    srcLoc.pResource = uploadBuffer;
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
    return true;
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

    // A pending logical reservation has no GPU object to retire. A materialized
    // resource can be freed only through the existing graveyard path because
    // the previous submission may still reference it.
    if (entry.PhysicalReady && entry.Resource)
        Graveyard.push_back(std::move(entry.Resource));

    entry = Entry{};
    FreeSlots.push_back(handle - 1);

    PendingBarriers.erase(
        std::remove(PendingBarriers.begin(), PendingBarriers.end(), handle - 1),
        PendingBarriers.end());
    PendingCreateSlots.erase(
        std::remove(PendingCreateSlots.begin(), PendingCreateSlots.end(), handle - 1),
        PendingCreateSlots.end());
    u32 i = 0;
    while (i < PendingUploadCount)
    {
        if (PendingUploads[i].Handle != handle)
        {
            i++;
            continue;
        }

        PendingUploadCount--;
        if (i != PendingUploadCount)
            std::swap(PendingUploads[i], PendingUploads[PendingUploadCount]);
    }
}

void DX12TextureHeap::CollectGarbage()
{
    Graveyard.clear();
    SpillUploads.clear();
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
