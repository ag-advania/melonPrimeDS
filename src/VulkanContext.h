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

#ifndef VULKAN_CONTEXT_H
#define VULKAN_CONTEXT_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <mutex>
#include <string>
#include <vector>

#include "VulkanCommon.h"
#include "VulkanFeatureProbe.h"
#include "VulkanLoader.h"

namespace melonDS
{

// Process-wide owner of the Vulkan loader, the VkInstance and the chosen
// VkPhysicalDevice, shaped like DX12Context: the settings-dialog feature check
// and the renderer both Acquire()/Release() the same instance instead of each
// creating their own.
//
// One instance per process is not just an optimization. Creating a second
// VkInstance while the first is alive re-initializes every installed layer and,
// on several drivers, resets global state the live instance depends on; and the
// renderer-switch path (Vulkan -> OpenGL -> Vulkan) would otherwise tear down
// and rebuild the instance on every toggle.
//
// Not thread-safe by construction: every mutating entry point takes Mutex.
class VulkanContext
{
public:
    static VulkanContext& Get();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    // Reference-counted. The first Acquire() opens the loader and creates the
    // instance; the last Release() destroys both. Returns false when Vulkan is
    // unavailable, in which case no reference is taken and
    // GetFailureReason() explains why.
    //
    // `needPresentation` false runs the headless path used by the settings
    // dialog: no surface extensions are requested and present support is
    // reported as "not checked" rather than assumed.
    bool Acquire(bool needPresentation = true);
    void Release();

    [[nodiscard]] bool IsReady() const noexcept { return Instance != VK_NULL_HANDLE; }
    [[nodiscard]] VkInstance GetInstance() const noexcept { return Instance; }
    [[nodiscard]] const Vk::InstanceDispatch& Fns() const noexcept { return InstanceFns; }
    [[nodiscard]] const Vk::Library& GetLibrary() const noexcept { return Loader; }
    [[nodiscard]] u32 GetInstanceVersion() const noexcept { return Loader.GetInstanceVersion(); }

    [[nodiscard]] const std::vector<const char*>& GetEnabledInstanceExtensions() const noexcept
    {
        return EnabledInstanceExtensions;
    }
    [[nodiscard]] const std::vector<const char*>& GetEnabledInstanceLayers() const noexcept
    {
        return EnabledInstanceLayers;
    }
    [[nodiscard]] bool IsValidationEnabled() const noexcept { return ValidationEnabled; }
    [[nodiscard]] bool IsDebugUtilsEnabled() const noexcept { return DebugUtilsEnabled; }

    // Enumerates and scores physical devices, keeping the highest-scoring
    // eligible one.
    //
    // `surface` may be VK_NULL_HANDLE for the headless probe. Once the
    // presenter owns a real surface it must call this again with it: present
    // support and the present queue family cannot be determined without one,
    // and re-selecting is cheap (no device has been created yet).
    //
    // Returns false when no device passes the probe; GetFailureReason() then
    // holds the first failed requirement of the best candidate.
    bool SelectPhysicalDevice(VkSurfaceKHR surface);

    [[nodiscard]] bool HasSelectedDevice() const noexcept
    {
        return SelectedDevice.Handle != VK_NULL_HANDLE;
    }
    [[nodiscard]] const Vk::DeviceProbeResult& GetSelectedDevice() const noexcept { return SelectedDevice; }
    [[nodiscard]] const Vk::ProbeReport& GetInstanceReport() const noexcept { return InstanceReport; }
    [[nodiscard]] const std::string& GetFailureReason() const noexcept { return FailureReason; }

    // Number of physical devices the last SelectPhysicalDevice() looked at.
    [[nodiscard]] u32 GetCandidateCount() const noexcept { return CandidateCount; }

private:
    VulkanContext() = default;
    ~VulkanContext();

    bool CreateInstance(bool needPresentation);
    void DestroyInstance();
    bool BuildInstanceExtensionList(bool needPresentation,
                                    const std::vector<VkExtensionProperties>& available);
    bool BuildInstanceLayerList();
    bool CreateDebugMessenger();
    void DestroyDebugMessenger();

    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT types,
        const VkDebugUtilsMessengerCallbackDataEXT* data,
        void* userData);

    mutable std::mutex Mutex;
    int RefCount = 0;

    Vk::Library Loader;
    VkInstance Instance = VK_NULL_HANDLE;
    Vk::InstanceDispatch InstanceFns{};
    VkDebugUtilsMessengerEXT DebugMessenger = VK_NULL_HANDLE;

    std::vector<const char*> EnabledInstanceExtensions;
    std::vector<const char*> EnabledInstanceLayers;
    bool ValidationEnabled = false;
    bool DebugUtilsEnabled = false;
    bool PortabilityEnumeration = false;
    bool PresentationRequested = true;

    Vk::ProbeReport InstanceReport;
    Vk::DeviceProbeResult SelectedDevice;
    u32 CandidateCount = 0;

    std::string FailureReason;
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // VULKAN_CONTEXT_H
