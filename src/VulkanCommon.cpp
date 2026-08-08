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

#include "VulkanCommon.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <cstdio>

namespace melonDS::Vk
{

const char* ResultToString(VkResult result) noexcept
{
    switch (result)
    {
    case VK_SUCCESS:                                            return "VK_SUCCESS";
    case VK_NOT_READY:                                          return "VK_NOT_READY";
    case VK_TIMEOUT:                                            return "VK_TIMEOUT";
    case VK_EVENT_SET:                                          return "VK_EVENT_SET";
    case VK_EVENT_RESET:                                        return "VK_EVENT_RESET";
    case VK_INCOMPLETE:                                         return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:                           return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:                         return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:                        return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:                                  return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:                            return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:                            return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:                        return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:                          return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:                          return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:                             return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:                         return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:                              return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:                                      return "VK_ERROR_UNKNOWN";

    // Promoted to core in 1.1 / 1.2; the enumerants exist unconditionally in
    // any header new enough to build this backend.
    case VK_ERROR_OUT_OF_POOL_MEMORY:                           return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:                      return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_FRAGMENTATION:                                return "VK_ERROR_FRAGMENTATION";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:               return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";

    // WSI. VK_SUBOPTIMAL_KHR and VK_ERROR_OUT_OF_DATE_KHR are the two the
    // presenter acts on rather than treats as fatal, so they must print
    // readably even though Check() rejects them.
    case VK_ERROR_SURFACE_LOST_KHR:                             return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:                     return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:                                     return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:                              return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:                     return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";

    case VK_ERROR_VALIDATION_FAILED_EXT:                        return "VK_ERROR_VALIDATION_FAILED_EXT";
    case VK_ERROR_INVALID_SHADER_NV:                            return "VK_ERROR_INVALID_SHADER_NV";

#ifdef VK_EXT_full_screen_exclusive
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:          return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
#endif
#ifdef VK_VERSION_1_3
    case VK_PIPELINE_COMPILE_REQUIRED:                          return "VK_PIPELINE_COMPILE_REQUIRED";
#endif
#ifdef VK_KHR_deferred_host_operations
    case VK_THREAD_IDLE_KHR:                                    return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR:                                    return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR:                             return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR:                         return "VK_OPERATION_NOT_DEFERRED_KHR";
#endif

    default:
        // Deliberately null rather than "Unknown": a layer or a vendor
        // extension can return a code this build's headers never heard of, and
        // FormatResult() prints the raw number so the user's log still carries
        // the information needed to look it up.
        return nullptr;
    }
}

std::string FormatResult(VkResult result)
{
    char buffer[64];

    const char* name = ResultToString(result);
    if (name)
        std::snprintf(buffer, sizeof(buffer), "%s (%d)", name, static_cast<int>(result));
    else
        std::snprintf(buffer, sizeof(buffer), "VkResult(%d)", static_cast<int>(result));

    return std::string(buffer);
}

bool Fail(const char* operation, VkResult result, const char* file, int line)
{
    Platform::Log(Platform::LogLevel::Error,
        "[Vulkan] %s failed: %s (%s:%d)\n",
        operation ? operation : "<unnamed operation>",
        FormatResult(result).c_str(),
        file ? file : "<unknown>",
        line);
    return false;
}

std::string FormatDriverVersion(u32 vendorId, u32 driverVersion)
{
    char buffer[64];

    // VkPhysicalDeviceProperties::driverVersion is explicitly vendor-defined:
    // the spec does not require the VK_MAKE_API_VERSION packing. Only the two
    // vendors below are documented to deviate, so everyone else is decoded
    // with the standard 10/10/12 layout.
    switch (vendorId)
    {
    case 0x10DE: // NVIDIA: 10 / 8 / 8 / 6 (major.minor.secondary.tertiary)
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u",
            (driverVersion >> 22) & 0x3FF,
            (driverVersion >> 14) & 0x0FF,
            (driverVersion >> 6)  & 0x0FF,
            driverVersion & 0x03F);
        break;

#if defined(_WIN32)
    case 0x8086: // Intel on Windows: 14 / 18, matching the DCH driver number
        std::snprintf(buffer, sizeof(buffer), "%u.%u",
            driverVersion >> 14,
            driverVersion & 0x3FFF);
        break;
#endif

    default:
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u",
            VK_API_VERSION_MAJOR(driverVersion),
            VK_API_VERSION_MINOR(driverVersion),
            VK_API_VERSION_PATCH(driverVersion));
        break;
    }

    return std::string(buffer);
}

std::string FormatApiVersion(u32 apiVersion)
{
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%u.%u.%u",
        VK_API_VERSION_MAJOR(apiVersion),
        VK_API_VERSION_MINOR(apiVersion),
        VK_API_VERSION_PATCH(apiVersion));
    return std::string(buffer);
}

std::string FormatVendor(u32 vendorId)
{
    // PCI SIG IDs. Vulkan also defines a small block of non-PCI VkVendorId
    // values (0x10000 and up) for software implementations.
    switch (vendorId)
    {
    case 0x1002: return "AMD";
    case 0x1010: return "ImgTec";
    case 0x106B: return "Apple";
    case 0x10DE: return "NVIDIA";
    case 0x13B5: return "ARM";
    case 0x14E4: return "Broadcom";
    case 0x5143: return "Qualcomm";
    case 0x8086: return "Intel";
    case 0x10005: return "Mesa";
    default: break;
    }

    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "vendor 0x%04X", vendorId);
    return std::string(buffer);
}

} // namespace melonDS::Vk

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
