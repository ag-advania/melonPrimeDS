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

#ifndef DX12_RESOURCE_FACTORY_H
#define DX12_RESOURCE_FACTORY_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Common.h"

namespace melonDS
{

// Committed-resource creation. A borrowed device pointer and nothing else:
// the factory does not extend the device's lifetime and must not outlive it.
//
// Separate from the device owner because "how a buffer or texture is
// described" changes with the renderer's resource layout, not with device
// selection.
class DX12ResourceFactory
{
public:
    explicit DX12ResourceFactory(ID3D12Device* device) noexcept : Device(device) {}

    // Both log and return an empty ComPtr on failure; callers must null-check.
    DX12::ComPtr<ID3D12Resource> CreateBuffer(
        u64 size,
        D3D12_HEAP_TYPE heapType,
        D3D12_RESOURCE_STATES initialState,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        const wchar_t* debugName = nullptr) const;

    DX12::ComPtr<ID3D12Resource> CreateTexture2D(
        DXGI_FORMAT format,
        u32 width,
        u32 height,
        u32 arraySize,
        D3D12_RESOURCE_FLAGS flags,
        D3D12_RESOURCE_STATES initialState,
        const wchar_t* debugName = nullptr,
        HRESULT* outResult = nullptr) const;

private:
    ID3D12Device* Device = nullptr;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_RESOURCE_FACTORY_H
