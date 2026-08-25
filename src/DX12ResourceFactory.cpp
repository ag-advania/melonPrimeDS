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

#include "DX12ResourceFactory.h"
#include "Platform.h"

namespace melonDS
{

DX12::ComPtr<ID3D12Resource> DX12ResourceFactory::CreateBuffer(
    u64 size,
    D3D12_HEAP_TYPE heapType,
    D3D12_RESOURCE_STATES initialState,
    D3D12_RESOURCE_FLAGS flags,
    const wchar_t* debugName) const
{
    DX12::ComPtr<ID3D12Resource> resource;
    if (!Device || size == 0)
        return resource;

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = heapType;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment = 0;
    desc.Width = size;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;

    const HRESULT hr = Device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        DX12::Fail("CreateCommittedResource(buffer)", hr);
        resource.Reset();
        return resource;
    }

    if (debugName) resource->SetName(debugName);
    return resource;
}

DX12::ComPtr<ID3D12Resource> DX12ResourceFactory::CreateTexture2D(
    DXGI_FORMAT format,
    u32 width,
    u32 height,
    u32 arraySize,
    D3D12_RESOURCE_FLAGS flags,
    D3D12_RESOURCE_STATES initialState,
    const wchar_t* debugName,
    HRESULT* outResult) const
{
    DX12::ComPtr<ID3D12Resource> resource;
    if (outResult)
        *outResult = E_FAIL;
    if (!Device || width == 0 || height == 0 || arraySize == 0)
    {
        if (outResult)
            *outResult = !Device ? E_FAIL : E_INVALIDARG;
        return resource;
    }

    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    heap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heap.CreationNodeMask = 1;
    heap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Alignment = 0;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = static_cast<UINT16>(arraySize);
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = flags;

    const HRESULT hr = Device->CreateCommittedResource(
        &heap,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        initialState,
        nullptr,
        IID_PPV_ARGS(resource.ReleaseAndGetAddressOf()));
    if (FAILED(hr))
    {
        if (outResult)
            *outResult = hr;
        DX12::Fail("CreateCommittedResource(texture2D)", hr);
        resource.Reset();
        return resource;
    }

    if (debugName) resource->SetName(debugName);
    if (outResult)
        *outResult = S_OK;
    return resource;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
