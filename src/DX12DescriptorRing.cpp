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

#include "DX12DescriptorRing.h"
#include "Platform.h"

namespace melonDS
{

bool DX12DescriptorRing::Init(ID3D12Device* device, u32 descriptorCount, bool shaderVisible)
{
    Shutdown();

    if (!device || descriptorCount == 0)
        return false;

    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    desc.NumDescriptors = descriptorCount;
    desc.Flags = shaderVisible
        ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
        : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    desc.NodeMask = 0;

    const HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(Heap.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
        return DX12::Fail("CreateDescriptorHeap", hr);

    Increment = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    CpuStart = Heap->GetCPUDescriptorHandleForHeapStart();
    GpuStart = shaderVisible ? Heap->GetGPUDescriptorHandleForHeapStart() : D3D12_GPU_DESCRIPTOR_HANDLE{};
    Capacity = descriptorCount;
    Head = 0;
    ShaderVisible = shaderVisible;
    return true;
}

void DX12DescriptorRing::Shutdown()
{
    Heap.Reset();
    CpuStart = {};
    GpuStart = {};
    Increment = 0;
    Capacity = 0;
    Head = 0;
    ShaderVisible = false;
}

bool DX12DescriptorRing::Allocate(
    u32 count,
    D3D12_CPU_DESCRIPTOR_HANDLE& cpu,
    D3D12_GPU_DESCRIPTOR_HANDLE& gpu) noexcept
{
    if (!Heap || count == 0 || Head + count > Capacity)
        return false;

    cpu.ptr = CpuStart.ptr + static_cast<SIZE_T>(Head) * Increment;
    gpu.ptr = ShaderVisible ? (GpuStart.ptr + static_cast<UINT64>(Head) * Increment) : 0;
    Head += count;
    return true;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
