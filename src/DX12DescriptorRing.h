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

#ifndef DX12_DESCRIPTOR_RING_H
#define DX12_DESCRIPTOR_RING_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Common.h"

namespace melonDS
{

// Simple linear allocator over one shader-visible CBV/SRV/UAV descriptor heap.
// The compute renderer rebuilds its descriptor tables every frame, so a bump
// allocator that resets per frame is both sufficient and the cheapest option.
class DX12DescriptorRing
{
public:
    bool Init(ID3D12Device* device, u32 descriptorCount, bool shaderVisible);
    void Shutdown();

    // Reset preserves the optional persistent prefix and rewinds only the
    // transient tail. The zero-argument form retains the original behavior for
    // renderer-owned rings that have no persistent descriptors.
    void Reset(u32 reservedPrefix = 0) noexcept
    {
        Head = reservedPrefix < Capacity ? reservedPrefix : Capacity;
    }

    // Allocates `count` contiguous descriptors. Returns false when the heap is
    // exhausted (the caller should treat that as a hard error, not a hint to
    // grow: the renderer sizes the heap for its worst frame).
    bool Allocate(u32 count, D3D12_CPU_DESCRIPTOR_HANDLE& cpu, D3D12_GPU_DESCRIPTOR_HANDLE& gpu) noexcept;

    [[nodiscard]] ID3D12DescriptorHeap* GetHeap() const noexcept { return Heap.Get(); }
    [[nodiscard]] u32 GetIncrement() const noexcept { return Increment; }
    [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetCpu(u32 index) const noexcept
    {
        return index < Capacity
            ? D3D12_CPU_DESCRIPTOR_HANDLE{
                CpuStart.ptr + static_cast<SIZE_T>(index) * Increment}
            : D3D12_CPU_DESCRIPTOR_HANDLE{};
    }
    [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetGpu(u32 index) const noexcept
    {
        return ShaderVisible && index < Capacity
            ? D3D12_GPU_DESCRIPTOR_HANDLE{
                GpuStart.ptr + static_cast<UINT64>(index) * Increment}
            : D3D12_GPU_DESCRIPTOR_HANDLE{};
    }

private:
    DX12::ComPtr<ID3D12DescriptorHeap> Heap;
    D3D12_CPU_DESCRIPTOR_HANDLE CpuStart{};
    D3D12_GPU_DESCRIPTOR_HANDLE GpuStart{};
    u32 Increment = 0;
    u32 Capacity = 0;
    u32 Head = 0;
    bool ShaderVisible = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_DESCRIPTOR_RING_H
