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

#ifndef VULKAN_CAPTURE_BRIDGE_H
#define VULKAN_CAPTURE_BRIDGE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanCommon.h"
#include "VulkanMemory.h"
#include "VulkanSync.h"

namespace melonDS
{

class VulkanDevice;

// Physical owner of native Display Capture on the Vulkan backend.
//
// The counterpart of DX12CaptureBridge, and the same split: CaptureProvenance-
// State decides *whether* a recorded block may still be served, this decides
// *how* it is fetched. By the time a read reaches here the answer is already
// yes.
//
// It owns the high-resolution sidecar the compositor writes and the readback
// buffer a demanded block lands in. It deliberately does not own the frame
// ring that issues the copy: that ring is shared with the demand-driven 3D
// resolve for GetLine(), and the two serialize against each other through its
// single command-buffer slot.
class VulkanCaptureBridge
{
public:
    // Fixed-size, created with the rest of the renderer's device objects.
    bool CreateReadback(const VulkanDevice& device, VkDeviceSize bytes);

    // Resolution-dependent: recreated whenever the internal scale changes.
    bool CreateSidecar(const VulkanDevice& device, VkDeviceSize bytes);

    void ReleaseSidecar() noexcept { Sidecar.Destroy(); }
    void ReleaseReadback() noexcept { Readback.Destroy(); }

    // Borrowed by the compositor's descriptor writes and barriers. The
    // compositor writes the sidecar; this class owns its lifetime.
    [[nodiscard]] VkBuffer GetSidecarHandle() const noexcept
    {
        return Sidecar.GetHandle();
    }
    [[nodiscard]] bool HasReadbackBuffer() const noexcept
    {
        return Readback.IsValid();
    }

    // Copies `len` capture blocks (clamped to three, the DS maximum for one
    // request) out of `source` at `sourceBase`, waits for that submission
    // alone, and writes them into `destination` at their DS block positions.
    //
    // `frames` is the shared demand-driven readback ring. The caller must
    // already have retired any other submission on it -- this class cannot
    // know what else is queued there.
    //
    // Returns false without touching `destination` if anything fails; a failed
    // capture read is a fallback, not a corruption.
    [[nodiscard]] bool ReadBlocks(
        const VulkanDevice& device,
        Vk::FrameRing& frames,
        VkBuffer source,
        VkDeviceSize sourceBase,
        u32 bank,
        u32 start,
        u32 len,
        u8* destination);

private:
    Vk::Buffer Sidecar;
    Vk::ReadbackBuffer Readback;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_CAPTURE_BRIDGE_H
