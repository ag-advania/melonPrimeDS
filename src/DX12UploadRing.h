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

#ifndef DX12_UPLOAD_RING_H
#define DX12_UPLOAD_RING_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12Common.h"

namespace melonDS
{

// Linear sub-allocator over a persistently mapped UPLOAD buffer. Texture and
// constant uploads bump-allocate from here; the renderer resets it once per
// frame after the GPU has retired the previous frame's list.
class DX12UploadRing
{
public:
    bool Init(ID3D12Device* device, u64 size);
    void Shutdown();

    void Reset() noexcept { Head = 0; }

    // Returns a mapped CPU pointer plus the matching buffer offset, aligned to
    // `alignment`. Returns nullptr when the remaining space is insufficient;
    // callers use a retained spill upload rather than waiting mid-frame.
    void* Allocate(u64 size, u64 alignment, u64& outOffset) noexcept;

    [[nodiscard]] ID3D12Resource* GetBuffer() const noexcept { return Buffer.Get(); }
    [[nodiscard]] u64 GetCapacity() const noexcept { return Capacity; }

private:
    DX12::ComPtr<ID3D12Resource> Buffer;
    u8* Mapped = nullptr;
    u64 Capacity = 0;
    u64 Head = 0;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_UPLOAD_RING_H
