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

#include "DX12UploadRing.h"
#include "DX12ResourceFactory.h"
#include "Platform.h"

namespace melonDS
{

bool DX12UploadRing::Init(ID3D12Device* device, u64 size)
{
    Shutdown();

    Buffer = DX12ResourceFactory(device).CreateBuffer(
        size,
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_FLAG_NONE,
        L"MelonPrime DX12 upload ring");
    if (!Buffer)
        return false;

    D3D12_RANGE noRead{ 0, 0 };
    void* mapped = nullptr;
    const HRESULT hr = Buffer->Map(0, &noRead, &mapped);
    if (FAILED(hr))
    {
        Buffer.Reset();
        return DX12::Fail("Map(upload ring)", hr);
    }

    Mapped = static_cast<u8*>(mapped);
    Capacity = size;
    Head = 0;
    return true;
}

void DX12UploadRing::Shutdown()
{
    if (Buffer && Mapped)
    {
        D3D12_RANGE written{ 0, 0 };
        Buffer->Unmap(0, &written);
    }
    Buffer.Reset();
    Mapped = nullptr;
    Capacity = 0;
    Head = 0;
}

void* DX12UploadRing::Allocate(u64 size, u64 alignment, u64& outOffset) noexcept
{
    if (!Mapped || size == 0)
        return nullptr;

    if (alignment == 0) alignment = 1;
    const u64 aligned = (Head + alignment - 1) & ~(alignment - 1);
    if (aligned + size > Capacity)
        return nullptr;

    outOffset = aligned;
    Head = aligned + size;
    return Mapped + aligned;
}

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
