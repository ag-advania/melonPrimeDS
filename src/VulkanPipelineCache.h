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

#ifndef VULKAN_PIPELINE_CACHE_H
#define VULKAN_PIPELINE_CACHE_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanCommon.h"

namespace melonDS
{

class VulkanDevice;

// Owns the VkPipelineCache and its on-disk payload.
//
// Split out of VulkanRenderer3D for the same reason DX12PipelineRepository was
// split out of DX12Renderer3D: cache framing, device-identity validation and
// driver-rejection handling are their own change axis. The rasterizer builds
// pipelines; it only needs a cache handle to hand to
// vkCreateComputePipelines, and VK_NULL_HANDLE is a valid one.
//
// Pipeline *creation* deliberately stays with the rasterizer. Vulkan folds the
// resolution-dependent specialization constants in at pipeline creation, so
// that call belongs where the geometry state lives -- unlike DX12, where the
// root signature and PSO caching form a self-contained unit.
class VulkanPipelineCache
{
public:
    VulkanPipelineCache() = default;
    ~VulkanPipelineCache() = default;

    VulkanPipelineCache(const VulkanPipelineCache&) = delete;
    VulkanPipelineCache& operator=(const VulkanPipelineCache&) = delete;

    // Never fails the renderer. A missing, mismatched or driver-rejected
    // payload only costs compile time, and pipeline creation accepts
    // VK_NULL_HANDLE, so an outright creation failure is reported and
    // tolerated rather than propagated.
    void Create(const VulkanDevice& device);

    // Writes the driver's current cache blob, framed with the device identity
    // it is only valid against. No-op without a live cache.
    void Save(const VulkanDevice& device) const;

    // Destroys the cache object. The caller must have retired any in-flight
    // pipeline creation first.
    void Destroy(const VulkanDevice& device) noexcept;

    [[nodiscard]] VkPipelineCache GetHandle() const noexcept { return Handle; }

    // True when the cache was created from a stored payload rather than
    // empty. Startup diagnostics only.
    [[nodiscard]] bool WasLoadedFromDisk() const noexcept { return LoadedFromDisk; }

private:
    VkPipelineCache Handle = VK_NULL_HANDLE;
    bool LoadedFromDisk = false;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_PIPELINE_CACHE_H
