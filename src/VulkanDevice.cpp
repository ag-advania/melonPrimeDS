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

#include "VulkanDevice.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

namespace melonDS
{

namespace
{

// Same reason as in VulkanContext.cpp: the macro for this name only exists
// behind VK_ENABLE_BETA_EXTENSIONS, which the build does not define.
constexpr const char* PortabilitySubsetExtensionName = "VK_KHR_portability_subset";

// Single priority value shared by every queue. The backend creates exactly one
// queue per family, so relative priorities have nothing to arbitrate; 1.0 is
// the documented "as important as anything else" value.
constexpr float QueuePriority = 1.0f;

const char* DeviceTypeName(VkPhysicalDeviceType type) noexcept
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return "discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return "virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU:            return "CPU";
    default:                                     return "other";
    }
}

} // namespace


VulkanDevice::~VulkanDevice()
{
    Destroy();
}


const Vk::InstanceDispatch& VulkanDevice::InstanceFns() const noexcept
{
    return Context->Fns();
}


bool VulkanDevice::Create(VulkanContext& context, const char* requestedRendererName)
{
    Destroy();

    FailureReason.clear();
    Context = &context;

    if (!context.IsReady())
    {
        FailureReason = "the Vulkan instance is not ready";
        return false;
    }
    if (!context.HasSelectedDevice())
    {
        FailureReason = "no physical device has been selected";
        return false;
    }

    Profile = context.GetSelectedDevice();
    PhysicalDevice = Profile.Handle;

    const Vk::QueueFamilySelection& queues = Profile.Queues;

    // --- queue families -----------------------------------------------------
    if (queues.HasUniversalFamily())
    {
        MainQueueFamily = queues.UniversalFamily;
        // Present comes from the same family when one was found *with* a
        // surface. In the headless case PresentUnknown is true and the present
        // queue stays null until the presenter re-selects with a real surface.
        PresentQueueFamily = queues.PresentUnknown
            ? Vk::QueueFamilySelection::InvalidFamily
            : queues.UniversalFamily;
    }
    else
    {
        // Split configuration. Graphics and compute must both exist -- the
        // probe fails the device otherwise -- and the renderer records that
        // ownership transfers are required.
        if (queues.GraphicsFamily == Vk::QueueFamilySelection::InvalidFamily
            || queues.ComputeFamily == Vk::QueueFamilySelection::InvalidFamily)
        {
            FailureReason = "the selected device has no usable graphics+compute queue family";
            return false;
        }

        // The compute family carries the rasterizer, which is the bulk of the
        // frame; graphics work in this backend is limited to the present-time
        // blit, so the compute family is the "main" one.
        MainQueueFamily = queues.ComputeFamily;
        PresentQueueFamily = queues.PresentFamily;

        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] no universal queue family on %s; using compute family %u "
            "and present family %u, resources will need ownership transfers\n",
            Profile.DeviceName.c_str(), MainQueueFamily, PresentQueueFamily);
    }

    std::vector<u32> distinctFamilies;
    distinctFamilies.push_back(MainQueueFamily);
    if (PresentQueueFamily != Vk::QueueFamilySelection::InvalidFamily
        && PresentQueueFamily != MainQueueFamily)
    {
        distinctFamilies.push_back(PresentQueueFamily);
    }

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(distinctFamilies.size());
    for (u32 family : distinctFamilies)
    {
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &QueuePriority;
        queueInfos.push_back(info);
    }

    // --- extensions ---------------------------------------------------------
    EnabledExtensions.clear();

    std::vector<VkExtensionProperties> available;
    if (!Vk::FeatureProbe::EnumerateDeviceExtensions(context.Fns(), PhysicalDevice, available))
    {
        FailureReason = "vkEnumerateDeviceExtensionProperties failed on the selected device";
        return false;
    }

    // The swapchain extension is only requested when the context was created
    // for presentation; the headless settings-dialog device does not need it
    // and asking for it there would fail on a compute-only ICD.
    const bool needPresent = context.GetEnabledInstanceExtensions().size() > 0
        && Vk::ExtensionEnabled(context.GetEnabledInstanceExtensions(), "VK_KHR_surface");

    for (const char* name : Vk::FeatureProbe::GetRequiredDeviceExtensions(needPresent))
    {
        if (!Vk::FeatureProbe::HasExtension(available, name))
        {
            FailureReason = std::string("required device extension ") + name + " is missing";
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
            return false;
        }
        EnabledExtensions.push_back(name);
    }

    // Mandatory, not optional: the portability subset specification states that
    // an implementation exposing VK_KHR_portability_subset must have it enabled
    // by any device created from it. Skipping it is a spec violation that
    // validation flags immediately.
    if (Profile.RequiresPortabilitySubset)
        EnabledExtensions.push_back(PortabilitySubsetExtensionName);

    // --- features -----------------------------------------------------------
    // Deliberately empty.
    //
    // The compute rasterizer was ported from GPU3D_Compute, which uses only
    // core-guaranteed functionality: storage images are declared with an
    // explicit format (so shaderStorageImageWriteWithoutFormat is not needed),
    // the 64-bit depth interpolation is done with umulExtended on two 32-bit
    // halves rather than Int64 arithmetic (so shaderInt64 is not needed), and
    // no shader stage other than compute writes to a buffer or image (so
    // fragment/vertexPipelineStoresAndAtomics are not needed). Enabling
    // features the backend does not use would narrow the set of devices that
    // can create the device for no benefit.
    EnabledFeatures = VkPhysicalDeviceFeatures{};

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<u32>(EnabledExtensions.size());
    createInfo.ppEnabledExtensionNames =
        EnabledExtensions.empty() ? nullptr : EnabledExtensions.data();
    createInfo.pEnabledFeatures = &EnabledFeatures;
    // ppEnabledLayerNames is ignored for devices since Vulkan 1.0.13 -- device
    // layers are deprecated and the instance layer list already covers
    // validation -- so it is left null on purpose.

    const VkResult res =
        context.Fns().CreateDevice(PhysicalDevice, &createInfo, nullptr, &Device);
    if (res != VK_SUCCESS)
    {
        Device = VK_NULL_HANDLE;
        FailureReason = "vkCreateDevice failed: " + Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    std::string dispatchFailure;
    if (!Vk::LoadDeviceDispatch(context.Fns(), Device, EnabledExtensions, DeviceFns, dispatchFailure))
    {
        FailureReason = dispatchFailure;
        // The device exists but is unusable. Tear it down through the normal
        // path so the failure leaves nothing behind.
        Destroy();
        return false;
    }

    DeviceFns.GetDeviceQueue(Device, MainQueueFamily, 0, &MainQueue);
    if (MainQueue == VK_NULL_HANDLE)
    {
        FailureReason = "vkGetDeviceQueue returned no queue for the main family";
        Destroy();
        return false;
    }

    if (PresentQueueFamily != Vk::QueueFamilySelection::InvalidFamily)
    {
        if (PresentQueueFamily == MainQueueFamily)
        {
            // Same family, same queue index: the spec guarantees this is the
            // identical VkQueue, so no second retrieval is needed and, more
            // importantly, submissions to it are ordered against each other
            // with no cross-queue synchronization.
            PresentQueue = MainQueue;
        }
        else
        {
            DeviceFns.GetDeviceQueue(Device, PresentQueueFamily, 0, &PresentQueue);
            if (PresentQueue == VK_NULL_HANDLE)
            {
                FailureReason = "vkGetDeviceQueue returned no queue for the present family";
                Destroy();
                return false;
            }
        }
    }

    SetDebugName(VK_OBJECT_TYPE_DEVICE, Device, "melonPrimeDS device");
    SetDebugName(VK_OBJECT_TYPE_QUEUE, MainQueue, "melonPrimeDS main queue");

    LogStartupSummary(requestedRendererName);
    return true;
}


void VulkanDevice::Destroy()
{
    if (Device != VK_NULL_HANDLE)
    {
        // Permitted WaitIdle site: teardown. Destroying a device (and, through
        // the deferred destruction queue, every object still parented to it)
        // while the GPU may still be executing recorded commands is undefined
        // behaviour.
        if (DeviceFns.DeviceWaitIdle)
        {
            const VkResult res = DeviceFns.DeviceWaitIdle(Device);
            if (res != VK_SUCCESS)
            {
                // VK_ERROR_DEVICE_LOST is expected here after a TDR; the device
                // still has to be destroyed, so this is logged and not acted on.
                Platform::Log(Platform::LogLevel::Warn,
                    "[Vulkan] vkDeviceWaitIdle during teardown: %s\n",
                    Vk::FormatResult(res).c_str());
            }
        }

        if (DeviceFns.DestroyDevice)
            DeviceFns.DestroyDevice(Device, nullptr);
        Device = VK_NULL_HANDLE;
    }

    DeviceFns = Vk::DeviceDispatch{};
    MainQueue = VK_NULL_HANDLE;
    PresentQueue = VK_NULL_HANDLE;
    MainQueueFamily = Vk::QueueFamilySelection::InvalidFamily;
    PresentQueueFamily = Vk::QueueFamilySelection::InvalidFamily;
    EnabledExtensions.clear();
    EnabledFeatures = VkPhysicalDeviceFeatures{};
    PhysicalDevice = VK_NULL_HANDLE;
    Context = nullptr;
}


void VulkanDevice::LogStartupSummary(const char* requestedRendererName) const
{
    const char* requested = requestedRendererName ? requestedRendererName : "Vulkan";

    // requested/actual are logged together and in that order so a log excerpt
    // can never be read as a successful Vulkan start when a fallback happened.
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] renderer requested=%s actual=Vulkan\n", requested);

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] instance API %s, device API %s\n",
        Vk::FormatApiVersion(Context->GetInstanceVersion()).c_str(),
        Profile.ApiVersionText.c_str());

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] GPU %s (%s, %s) driver %s\n",
        Profile.DeviceName.c_str(),
        Profile.VendorName.c_str(),
        DeviceTypeName(Profile.Properties.deviceType),
        Profile.DriverVersionText.c_str());

    if (PresentQueueFamily == Vk::QueueFamilySelection::InvalidFamily)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: main family %u (present family not resolved yet)\n",
            MainQueueFamily);
    }
    else if (PresentQueueFamily == MainQueueFamily)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: family %u handles graphics, compute and present\n",
            MainQueueFamily);
    }
    else
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] queues: main family %u, present family %u (ownership transfers active)\n",
            MainQueueFamily, PresentQueueFamily);
    }

    if (EnabledExtensions.empty())
    {
        Platform::Log(Platform::LogLevel::Info, "[Vulkan] device extensions: none\n");
    }
    else
    {
        for (const char* name : EnabledExtensions)
            Platform::Log(Platform::LogLevel::Info, "[Vulkan] device extension: %s\n", name);
    }

    // The enabled feature set is empty by design; saying so explicitly stops
    // an empty line from reading as "the log is broken".
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] device features: none required (core 1.1 functionality only)\n");

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] device-local memory %.1f MiB, internal resolution up to %dx\n",
        static_cast<double>(Profile.DeviceLocalMemory) / (1024.0 * 1024.0),
        Profile.MaxScaleFactor);

    if (Profile.RequiresPortabilitySubset)
    {
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] portability implementation: VK_KHR_portability_subset enabled\n");
    }
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
