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

#ifndef DX12_CAPTURE_BRIDGE_H
#define DX12_CAPTURE_BRIDGE_H

#if defined(MELONPRIME_DS) && defined(_WIN32) && defined(MELONPRIME_ENABLE_DX12)

#include "DX12CommandContext.h"
#include "DX12Common.h"

namespace melonDS
{

class DX12Context;

// Physical owner of native Display Capture on the DX12 backend.
//
// The audit's split for capture is semantic owner / physical owner:
// CaptureProvenanceState decides *whether* a recorded block may still be
// served, and this decides *how* it is fetched. Nothing here judges
// provenance; by the time a read reaches it, the answer is already yes.
//
// It owns two resources:
//
//   the high-resolution sidecar   written by the compositor, read back at
//                                 native resolution when a capture block is
//                                 demanded
//   the capture readback buffer   the READBACK-heap staging the copy lands in
//
// It deliberately does not own the command context that issues the copy. That
// context is shared with the demand-driven 3D resolve for GetLine(), and the
// two serialize against each other through it; a component owning it would
// make the rasterizer reach sideways into this one. The renderer facade owns
// it and passes it in.
class DX12CaptureBridge
{
public:
    // Fixed-size, created once with the rest of the renderer's device objects.
    bool CreateReadback(const DX12Context& context, u64 bytes);

    // Resolution-dependent: recreated whenever the internal scale changes.
    bool CreateSidecar(const DX12Context& context, u64 bytes);

    void ReleaseSidecar() noexcept { SidecarBuffer.Reset(); }
    void ReleaseReadback() noexcept { ReadbackBuffer.Reset(); }

    // Borrowed by the compositor's descriptor tables. The compositor writes
    // the sidecar; this class owns its lifetime and hands out the handle.
    [[nodiscard]] ID3D12Resource* GetSidecarBuffer() const noexcept
    {
        return SidecarBuffer.Get();
    }
    [[nodiscard]] bool HasReadbackBuffer() const noexcept
    {
        return ReadbackBuffer.Get() != nullptr;
    }

    // Copies `len` capture blocks (clamped to three, the DS maximum for one
    // request) out of `source` at `sourceBase`, waits for that copy alone, and
    // writes them into `destination` at their DS block positions.
    //
    // `commands` is the shared demand-driven readback context. The caller must
    // already have retired any other submission on it -- this class cannot
    // know what else is queued there.
    //
    // Returns false without touching `destination` if anything fails; a failed
    // capture read is a fallback, not a corruption.
    [[nodiscard]] bool ReadBlocks(
        DX12CommandContext& commands,
        ID3D12Resource* source,
        u64 sourceBase,
        u32 bank,
        u32 start,
        u32 len,
        u8* destination);

private:
    DX12::ComPtr<ID3D12Resource> SidecarBuffer;
    DX12::ComPtr<ID3D12Resource> ReadbackBuffer;
};

} // namespace melonDS

#endif // MELONPRIME_DS && _WIN32 && MELONPRIME_ENABLE_DX12
#endif // DX12_CAPTURE_BRIDGE_H
