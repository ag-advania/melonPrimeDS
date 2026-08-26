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

#include "DX12CaptureBridge.h"

#include <algorithm>
#include <cstring>

#include "DX12Context.h"
#include "GPU.h"

namespace melonDS
{

namespace
{

// One DS capture block. The emulated hardware addresses capture in these
// units, so both the copy and the destination layout are expressed in them.
constexpr u64 kCaptureBlockBytes = CapturePhysicalBlockBytes;
constexpr u64 kCaptureBankBytes =
    kCaptureBlockBytes * CapturePhysicalBlocksPerBank;

void InsertUavBarrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = resource;
    list->ResourceBarrier(1, &barrier);
}

void TransitionBuffer(
    ID3D12GraphicsCommandList* list,
    ID3D12Resource* resource,
    D3D12_RESOURCE_STATES before,
    D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    list->ResourceBarrier(1, &barrier);
}

} // namespace

bool DX12CaptureBridge::CreateReadback(const DX12Context& context, u64 bytes)
{
    ReadbackBuffer = context.CreateBuffer(
        bytes,
        D3D12_HEAP_TYPE_READBACK,
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 native capture readback");
    return ReadbackBuffer.Get() != nullptr;
}

bool DX12CaptureBridge::CreateSidecar(const DX12Context& context, u64 bytes)
{
    SidecarBuffer = context.CreateBuffer(
        bytes,
        D3D12_HEAP_TYPE_DEFAULT,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        L"MelonPrime DX12 high-resolution capture sidecar");
    return SidecarBuffer.Get() != nullptr;
}

bool DX12CaptureBridge::ReadBlocks(
    DX12CommandContext& commands,
    ID3D12Resource* source,
    u64 sourceBase,
    u32 bank,
    u32 start,
    u32 len,
    u8* destination)
{
    if (!ReadbackBuffer || !source || !destination)
        return false;

    const u32 blockCount = len == 0u ? 1u : std::min<u32>(len, 3u);
    const u64 totalBytes = static_cast<u64>(blockCount) * kCaptureBlockBytes;

    ID3D12GraphicsCommandList* list = commands.Begin();
    if (!list)
        return false;

    // The compositor writes the source through a UAV, so the copy has to be
    // ordered after those writes and the resource moved to COPY_SOURCE for the
    // duration -- then put back, because the next frame keeps writing it.
    InsertUavBarrier(list, source);
    TransitionBuffer(
        list, source,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    for (u32 i = 0; i < blockCount; ++i)
    {
        list->CopyBufferRegion(
            ReadbackBuffer.Get(), static_cast<u64>(i) * kCaptureBlockBytes,
            source,
            sourceBase + static_cast<u64>(bank) * kCaptureBankBytes
                + static_cast<u64>((start + i) & (CapturePhysicalBlocksPerBank - 1u))
                    * kCaptureBlockBytes,
            kCaptureBlockBytes);
    }
    TransitionBuffer(
        list, source,
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    if (!commands.Submit())
        return false;
    // Waits on this submission alone. Not a queue-wide idle: a capture read is
    // demand-driven and must not stall unrelated frame work.
    if (!commands.WaitForSubmittedValue())
        return false;

    D3D12_RANGE readRange{0, static_cast<SIZE_T>(totalBytes)};
    void* mapped = nullptr;
    if (FAILED(ReadbackBuffer->Map(0, &readRange, &mapped)) || !mapped)
        return false;
    const u8* readbackSource = static_cast<const u8*>(mapped);
    for (u32 i = 0; i < blockCount; ++i)
    {
        // The copy packed the blocks contiguously; the destination wants them
        // at their DS block positions, which wrap within the bank.
        const u32 block = (start + i) & (CapturePhysicalBlocksPerBank - 1u);
        std::memcpy(
            destination + static_cast<std::size_t>(block) * kCaptureBlockBytes,
            readbackSource + static_cast<std::size_t>(i) * kCaptureBlockBytes,
            static_cast<std::size_t>(kCaptureBlockBytes));
    }
    D3D12_RANGE noWrite{0, 0};
    ReadbackBuffer->Unmap(0, &noWrite);
    return true;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
