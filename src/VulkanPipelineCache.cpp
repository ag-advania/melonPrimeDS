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

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanPipelineCache.h"

#include <cstring>
#include <vector>

#include "Platform.h"
#include "VulkanDevice.h"

namespace melonDS
{

namespace
{

// On-disk pipeline cache framing. The Vulkan specification only allows
// pInitialData that came out of vkGetPipelineCacheData, so the payload is
// gated on an exact match of the device identity and the driver's own
// pipelineCacheUUID rather than handed to the driver and hoped for.
constexpr u32 PipelineCacheMagic = 0x4356504Du;     // 'MPVC'
constexpr u32 PipelineCacheVersion = 1;
constexpr const char* PipelineCacheFileName = "melonPrimeDS_vulkan_pipeline_cache.bin";

struct PipelineCacheFileHeader
{
    u32 Magic;
    u32 Version;
    u32 VendorId;
    u32 DeviceId;
    u32 DriverVersion;
    u32 PayloadBytes;
    u8 CacheUUID[VK_UUID_SIZE];
};

} // namespace

void VulkanPipelineCache::Create(const VulkanDevice& device)
{
    Handle = VK_NULL_HANDLE;
    LoadedFromDisk = false;

    const Vk::DeviceDispatch& fns = device.Fns();
    const VkPhysicalDeviceProperties& properties = device.GetProfile().Properties;

    std::vector<u8> payload;

    if (Platform::FileHandle* file = Platform::OpenLocalFile(PipelineCacheFileName, Platform::FileMode::Read))
    {
        PipelineCacheFileHeader header{};
        const bool headerRead =
            Platform::FileRead(&header, sizeof(header), 1, file) == 1
            && header.Magic == PipelineCacheMagic
            && header.Version == PipelineCacheVersion
            && header.VendorId == properties.vendorID
            && header.DeviceId == properties.deviceID
            && header.DriverVersion == properties.driverVersion
            && std::memcmp(header.CacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;

        if (headerRead && header.PayloadBytes > 0
            && Platform::FileLength(file) == sizeof(header) + header.PayloadBytes)
        {
            payload.resize(header.PayloadBytes);
            if (Platform::FileRead(payload.data(), header.PayloadBytes, 1, file) != 1)
                payload.clear();
        }
        Platform::CloseFile(file);
    }

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = payload.size();
    info.pInitialData = payload.empty() ? nullptr : payload.data();

    VkResult res = fns.CreatePipelineCache(device.GetHandle(), &info, nullptr, &Handle);
    LoadedFromDisk = res == VK_SUCCESS && !payload.empty();
    if (res != VK_SUCCESS && !payload.empty())
    {
        // A driver is allowed to reject data it does not recognise. Losing the
        // cache only costs compile time, so retry empty rather than fail.
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] the stored pipeline cache was rejected (%s); starting empty\n",
            Vk::FormatResult(res).c_str());
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        res = fns.CreatePipelineCache(device.GetHandle(), &info, nullptr, &Handle);
        LoadedFromDisk = false;
    }

    if (!MELONPRIME_VK_CHECK("vkCreatePipelineCache", res))
    {
        Handle = VK_NULL_HANDLE;
        // Pipeline creation accepts VK_NULL_HANDLE for the cache, so this is
        // recoverable; report it and continue uncached.
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] continuing without a pipeline cache\n");
    }
}

void VulkanPipelineCache::Save(const VulkanDevice& device) const
{
    if (Handle == VK_NULL_HANDLE || !device.IsValid())
        return;

    const Vk::DeviceDispatch& fns = device.Fns();

    size_t size = 0;
    if (fns.GetPipelineCacheData(device.GetHandle(), Handle, &size, nullptr) != VK_SUCCESS
        || size == 0)
        return;

    std::vector<u8> payload(size);
    if (fns.GetPipelineCacheData(device.GetHandle(), Handle, &size, payload.data()) != VK_SUCCESS)
        return;
    payload.resize(size);

    Platform::FileHandle* file =
        Platform::OpenLocalFile(PipelineCacheFileName, Platform::FileMode::Write);
    if (!file)
        return;

    const VkPhysicalDeviceProperties& properties = device.GetProfile().Properties;

    PipelineCacheFileHeader header{};
    header.Magic = PipelineCacheMagic;
    header.Version = PipelineCacheVersion;
    header.VendorId = properties.vendorID;
    header.DeviceId = properties.deviceID;
    header.DriverVersion = properties.driverVersion;
    header.PayloadBytes = static_cast<u32>(payload.size());
    std::memcpy(header.CacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE);

    Platform::FileWrite(&header, sizeof(header), 1, file);
    Platform::FileWrite(payload.data(), payload.size(), 1, file);
    Platform::CloseFile(file);
}

void VulkanPipelineCache::Destroy(const VulkanDevice& device) noexcept
{
    if (Handle != VK_NULL_HANDLE)
    {
        device.Fns().DestroyPipelineCache(device.GetHandle(), Handle, nullptr);
        Handle = VK_NULL_HANDLE;
    }
    LoadedFromDisk = false;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
