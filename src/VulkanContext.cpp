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

#include "VulkanContext.h"

#include <cstring>

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

namespace melonDS
{

namespace
{

constexpr const char* ValidationLayerName = "VK_LAYER_KHRONOS_validation";

// Extension names spelled out rather than taken from the VK_*_EXTENSION_NAME
// macros, because those macros live in the per-platform headers that only
// appear when the matching VK_USE_PLATFORM_* macro is defined -- and core
// deliberately does not define the Linux/macOS ones (see VulkanCommon.h). The
// names are part of the Vulkan registry and cannot change.
constexpr const char* SurfaceExtensionName = "VK_KHR_surface";
constexpr const char* XlibSurfaceExtensionName = "VK_KHR_xlib_surface";
constexpr const char* XcbSurfaceExtensionName = "VK_KHR_xcb_surface";
constexpr const char* WaylandSurfaceExtensionName = "VK_KHR_wayland_surface";
constexpr const char* Win32SurfaceExtensionName = "VK_KHR_win32_surface";
constexpr const char* MetalSurfaceExtensionName = "VK_EXT_metal_surface";
constexpr const char* PortabilityEnumerationExtensionName = "VK_KHR_portability_enumeration";
constexpr const char* DebugUtilsExtensionName = "VK_EXT_debug_utils";

bool ContainsExtension(const std::vector<VkExtensionProperties>& available, const char* name) noexcept
{
    return Vk::FeatureProbe::HasExtension(available, name);
}

bool ContainsLayer(const std::vector<VkLayerProperties>& available, const char* name) noexcept
{
    for (const VkLayerProperties& layer : available)
    {
        if (std::strncmp(layer.layerName, name, VK_MAX_EXTENSION_NAME_SIZE) == 0)
            return true;
    }
    return false;
}

void AppendUnique(std::vector<const char*>& list, const char* name)
{
    if (!name)
        return;
    for (const char* entry : list)
    {
        if (entry && std::strcmp(entry, name) == 0)
            return;
    }
    list.push_back(name);
}

} // namespace


VulkanContext& VulkanContext::Get()
{
#if defined(_WIN32)
    // The previous Windows backend deliberately kept its Vulkan context warm
    // for the process lifetime. Do the same here: a live renderer switch must
    // not unload vulkan-1.dll or destroy the instance while the retained
    // process-wide VkDevice still exists. Allocating the singleton prevents
    // C++ static-destruction order from tearing the loader down underneath the
    // driver; Windows reclaims all three after executable teardown.
    static auto* instance = new VulkanContext();
    return *instance;
#else
    // Function-local static: constructed on first use, destroyed at exit after
    // every renderer has already released its reference.
    static VulkanContext instance;
    return instance;
#endif
}

VulkanContext::~VulkanContext()
{
    // Nothing should still hold a reference at process exit, but tearing down
    // in the correct order is cheap insurance against a leaked Acquire().
    DestroyInstance();
}


bool VulkanContext::Acquire(bool needPresentation)
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (RefCount > 0)
    {
        // An existing instance built without surface extensions cannot serve a
        // caller that now needs to present. Rather than silently handing back
        // a crippled instance, this is reported: the frontend acquires with
        // presentation first, so it only happens if that ordering breaks.
        if (needPresentation && !PresentationRequested)
        {
            FailureReason = "the Vulkan instance was already created without surface support";
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
            return false;
        }

        RefCount++;
        return true;
    }

    FailureReason.clear();
    InstanceReport.Clear();

    if (!Loader.Open())
    {
        FailureReason = Loader.GetFailureReason();
        return false;
    }

    if (!CreateInstance(needPresentation))
    {
        // CreateInstance already filled FailureReason. Close the loader again
        // so a later retry (e.g. after the user installs a driver) starts from
        // a clean state instead of reusing a half-initialized one.
        DestroyInstance();
        return false;
    }

    PresentationRequested = needPresentation;
    RefCount = 1;
    return true;
}


void VulkanContext::Release()
{
    std::lock_guard<std::mutex> lock(Mutex);

    if (RefCount <= 0)
        return;

    RefCount--;
    if (RefCount > 0)
        return;

    DestroyInstance();
}


bool VulkanContext::BuildInstanceLayerList()
{
    EnabledInstanceLayers.clear();
    ValidationEnabled = false;

#if defined(MELONDS_VULKAN_ENABLE_VALIDATION)
    u32 count = 0;
    VkResult res = Loader.Global().EnumerateInstanceLayerProperties(&count, nullptr);
    if (res != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkEnumerateInstanceLayerProperties failed: %s; validation disabled\n",
            Vk::FormatResult(res).c_str());
        return true;
    }

    std::vector<VkLayerProperties> layers(count);
    if (count > 0)
    {
        res = Loader.Global().EnumerateInstanceLayerProperties(&count, layers.data());
        if (res != VK_SUCCESS)
        {
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] layer enumeration failed: %s; validation disabled\n",
                Vk::FormatResult(res).c_str());
            return true;
        }
    }

    if (ContainsLayer(layers, ValidationLayerName))
    {
        EnabledInstanceLayers.push_back(ValidationLayerName);
        ValidationEnabled = true;
        Platform::Log(Platform::LogLevel::Info, "[Vulkan] validation layer enabled\n");
    }
    else
    {
        // Absence is normal on a machine without the SDK. It is a developer
        // build feature, never a requirement, so this must not fail startup.
        Platform::Log(Platform::LogLevel::Info,
            "[Vulkan] %s is not installed; validation disabled\n", ValidationLayerName);
    }
#endif

    return true;
}


bool VulkanContext::BuildInstanceExtensionList(
    bool needPresentation, const std::vector<VkExtensionProperties>& available)
{
    EnabledInstanceExtensions.clear();
    DebugUtilsEnabled = false;
    PortabilityEnumeration = false;

    if (needPresentation)
    {
        AppendUnique(EnabledInstanceExtensions, SurfaceExtensionName);

        // Optional capability plumbing for present_id2 / present_wait2 /
        // present_timing. Its absence never disables Vulkan; the presenter
        // falls back to the legacy surface query and host pacing.
        if (ContainsExtension(available, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME))
            AppendUnique(EnabledInstanceExtensions, VK_KHR_GET_SURFACE_CAPABILITIES_2_EXTENSION_NAME);

#if defined(_WIN32)
        AppendUnique(EnabledInstanceExtensions, Win32SurfaceExtensionName);
#elif defined(__APPLE__)
        AppendUnique(EnabledInstanceExtensions, MetalSurfaceExtensionName);
#else
        // The Qt platform plugin decides at runtime whether the window is
        // X11/XCB or Wayland, so every WSI extension the runtime offers is
        // enabled and the surface adapter picks one. At least one must exist;
        // that requirement is checked by the caller through the required list.
        bool anyUnixSurface = false;
        if (ContainsExtension(available, XlibSurfaceExtensionName))
        {
            AppendUnique(EnabledInstanceExtensions, XlibSurfaceExtensionName);
            anyUnixSurface = true;
        }
        if (ContainsExtension(available, XcbSurfaceExtensionName))
        {
            AppendUnique(EnabledInstanceExtensions, XcbSurfaceExtensionName);
            anyUnixSurface = true;
        }
        if (ContainsExtension(available, WaylandSurfaceExtensionName))
        {
            AppendUnique(EnabledInstanceExtensions, WaylandSurfaceExtensionName);
            anyUnixSurface = true;
        }
        if (!anyUnixSurface)
        {
            FailureReason =
                "the Vulkan runtime exposes no window-system surface extension "
                "(VK_KHR_xlib_surface / VK_KHR_xcb_surface / VK_KHR_wayland_surface)";
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
            return false;
        }
#endif
    }

#if defined(__APPLE__)
    // MoltenVK is a portability implementation. Since the portability
    // enumeration extension exists, a conformant loader hides non-conformant
    // ICDs unless the instance opts in with both the extension and the
    // VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR flag, so without this
    // vkEnumeratePhysicalDevices returns zero devices on macOS.
    if (ContainsExtension(available, PortabilityEnumerationExtensionName))
    {
        AppendUnique(EnabledInstanceExtensions, PortabilityEnumerationExtensionName);
        PortabilityEnumeration = true;
    }
#endif

    // Optional everywhere: it powers object naming and the messenger, both of
    // which degrade to no-ops when absent.
    if (ContainsExtension(available, DebugUtilsExtensionName))
    {
#if defined(MELONDS_VULKAN_ENABLE_VALIDATION)
        AppendUnique(EnabledInstanceExtensions, DebugUtilsExtensionName);
        DebugUtilsEnabled = true;
#endif
    }

    return true;
}


bool VulkanContext::CreateInstance(bool needPresentation)
{
    const Vk::GlobalDispatch& global = Loader.Global();

    // Enumerate once here to drive the extension selection; the probe below
    // enumerates again to produce its own findings. Two enumerations at
    // startup cost microseconds and keep the probe the single reporter.
    std::vector<VkExtensionProperties> available;
    {
        u32 count = 0;
        VkResult res = global.EnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        if (res != VK_SUCCESS)
        {
            FailureReason = "vkEnumerateInstanceExtensionProperties failed: " + Vk::FormatResult(res);
            Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
            return false;
        }
        available.resize(count);
        if (count > 0)
        {
            res = global.EnumerateInstanceExtensionProperties(nullptr, &count, available.data());
            if (res != VK_SUCCESS)
            {
                FailureReason = "vkEnumerateInstanceExtensionProperties failed: " + Vk::FormatResult(res);
                Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
                return false;
            }
        }
    }

    if (!BuildInstanceExtensionList(needPresentation, available))
        return false;
    if (!BuildInstanceLayerList())
        return false;

    // The probe is the gate: it reports the loader, the instance version and
    // every extension in the enabled list, and refuses to continue if any of
    // them is missing.
    if (!Vk::FeatureProbe::CheckInstanceRequirements(Loader, EnabledInstanceExtensions, InstanceReport))
    {
        InstanceReport.Log("instance", Platform::LogLevel::Info);
        FailureReason = InstanceReport.FirstFailure();
        return false;
    }
    InstanceReport.Log("instance", Platform::LogLevel::Debug);

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "melonPrimeDS";
    appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
    appInfo.pEngineName = "melonDS";
    appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);

    // apiVersion is the highest version the application promises to use
    // correctly. Asking for exactly 1.1 -- not the loader's version -- pins the
    // backend to the baseline it was written and tested against: a driver that
    // later reports 1.4 will not change the semantics of anything here.
    appInfo.apiVersion = Vk::MinimumApiVersion;

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<u32>(EnabledInstanceExtensions.size());
    createInfo.ppEnabledExtensionNames =
        EnabledInstanceExtensions.empty() ? nullptr : EnabledInstanceExtensions.data();
    createInfo.enabledLayerCount = static_cast<u32>(EnabledInstanceLayers.size());
    createInfo.ppEnabledLayerNames =
        EnabledInstanceLayers.empty() ? nullptr : EnabledInstanceLayers.data();

    if (PortabilityEnumeration)
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

    // Chaining the messenger create-info into pNext is the only way to see
    // validation messages produced *by* vkCreateInstance and vkDestroyInstance
    // themselves, since no messenger object exists at those moments.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    if (DebugUtilsEnabled)
    {
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = &VulkanContext::DebugCallback;
        createInfo.pNext = &messengerInfo;
    }

    const VkResult res = global.CreateInstance(&createInfo, nullptr, &Instance);
    if (res != VK_SUCCESS)
    {
        Instance = VK_NULL_HANDLE;
        FailureReason = "vkCreateInstance failed: " + Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    std::string dispatchFailure;
    if (!Vk::LoadInstanceDispatch(global, Instance, EnabledInstanceExtensions, InstanceFns, dispatchFailure))
    {
        FailureReason = dispatchFailure;
        return false;
    }

    if (DebugUtilsEnabled && !CreateDebugMessenger())
    {
        // Non-fatal: the instance is perfectly usable, only the message stream
        // is missing. CreateDebugMessenger() already logged the reason.
        DebugUtilsEnabled = false;
    }

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] instance created (API %s requested, %zu extensions, %zu layers)\n",
        Vk::FormatApiVersion(appInfo.apiVersion).c_str(),
        EnabledInstanceExtensions.size(),
        EnabledInstanceLayers.size());

    for (const char* name : EnabledInstanceExtensions)
        Platform::Log(Platform::LogLevel::Debug, "[Vulkan]   instance extension: %s\n", name);

    return true;
}


void VulkanContext::DestroyInstance()
{
    // Strict reverse-creation order. The messenger is a child of the instance
    // and must not outlive it; the loader library must not be unloaded while
    // any instance-owned function pointer is still reachable.
    DestroyDebugMessenger();

    if (Instance != VK_NULL_HANDLE)
    {
        if (InstanceFns.DestroyInstance)
            InstanceFns.DestroyInstance(Instance, nullptr);
        Instance = VK_NULL_HANDLE;
    }

    InstanceFns = Vk::InstanceDispatch{};
    SelectedDevice = Vk::DeviceProbeResult{};
    CandidateCount = 0;
    EnabledInstanceExtensions.clear();
    EnabledInstanceLayers.clear();
    ValidationEnabled = false;
    DebugUtilsEnabled = false;
    PortabilityEnumeration = false;
    RefCount = 0;

    Loader.Close();
}


bool VulkanContext::CreateDebugMessenger()
{
    if (!InstanceFns.CreateDebugUtilsMessengerEXT)
        return false;

    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    // VERBOSE and INFO are omitted deliberately: the validation layer emits
    // thousands of INFO messages per frame for a compute workload like this
    // one, which would make the log useless and cost real frame time.
    info.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = &VulkanContext::DebugCallback;

    const VkResult res =
        InstanceFns.CreateDebugUtilsMessengerEXT(Instance, &info, nullptr, &DebugMessenger);
    if (res != VK_SUCCESS)
    {
        DebugMessenger = VK_NULL_HANDLE;
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] vkCreateDebugUtilsMessengerEXT failed: %s\n", Vk::FormatResult(res).c_str());
        return false;
    }
    return true;
}


void VulkanContext::DestroyDebugMessenger()
{
    if (DebugMessenger == VK_NULL_HANDLE)
        return;

    if (InstanceFns.DestroyDebugUtilsMessengerEXT && Instance != VK_NULL_HANDLE)
        InstanceFns.DestroyDebugUtilsMessengerEXT(Instance, DebugMessenger, nullptr);

    DebugMessenger = VK_NULL_HANDLE;
}


VKAPI_ATTR VkBool32 VKAPI_CALL VulkanContext::DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT types,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* userData)
{
    (void)userData;

    if (!data)
        return VK_FALSE;

    const char* kind = "general";
    if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT)
        kind = "validation";
    else if (types & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)
        kind = "performance";

    const Platform::LogLevel level =
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            ? Platform::LogLevel::Error
            : Platform::LogLevel::Warn;

    Platform::Log(level, "[Vulkan/%s] %s: %s\n",
        kind,
        data->pMessageIdName ? data->pMessageIdName : "<unnamed>",
        data->pMessage ? data->pMessage : "<no message>");

    // The spec requires applications to return VK_FALSE here. VK_TRUE is
    // reserved for layer development and aborts the offending call.
    return VK_FALSE;
}


bool VulkanContext::SelectPhysicalDevice(VkSurfaceKHR surface)
{
    std::lock_guard<std::mutex> lock(Mutex);

    SelectedDevice = Vk::DeviceProbeResult{};
    CandidateCount = 0;

    if (Instance == VK_NULL_HANDLE)
    {
        FailureReason = "SelectPhysicalDevice called before the instance was created";
        return false;
    }

    u32 count = 0;
    VkResult res = InstanceFns.EnumeratePhysicalDevices(Instance, &count, nullptr);
    if (res != VK_SUCCESS)
    {
        FailureReason = "vkEnumeratePhysicalDevices failed: " + Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    if (count == 0)
    {
        FailureReason = "the Vulkan runtime reports no physical devices";
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    res = InstanceFns.EnumeratePhysicalDevices(Instance, &count, devices.data());
    if (res != VK_SUCCESS)
    {
        FailureReason = "vkEnumeratePhysicalDevices failed: " + Vk::FormatResult(res);
        Platform::Log(Platform::LogLevel::Error, "[Vulkan] %s\n", FailureReason.c_str());
        return false;
    }

    CandidateCount = count;

    Vk::DeviceProbeResult best;
    Vk::DeviceProbeResult firstRejected;
    bool haveRejected = false;

    for (VkPhysicalDevice device : devices)
    {
        Vk::DeviceProbeResult probe = Vk::FeatureProbe::ProbeDevice(InstanceFns, device, surface);

        if (!probe.IsEligible())
        {
            // Every rejection is logged in full: the whole point of the probe
            // is that a user who cannot run Vulkan learns exactly why.
            probe.Report.Log(probe.DeviceName.c_str(), Platform::LogLevel::Debug);
            if (!haveRejected)
            {
                firstRejected = std::move(probe);
                haveRejected = true;
            }
            continue;
        }

        probe.Report.Log(probe.DeviceName.c_str(), Platform::LogLevel::Debug);

        if (best.Handle == VK_NULL_HANDLE || probe.Score > best.Score)
            best = std::move(probe);
    }

    if (best.Handle == VK_NULL_HANDLE)
    {
        FailureReason = haveRejected
            ? (firstRejected.DeviceName + ": " + firstRejected.Report.FirstFailure())
            : std::string("no physical device satisfied the Vulkan requirements");
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] no usable device among %u candidates -- %s\n", count, FailureReason.c_str());
        return false;
    }

    SelectedDevice = std::move(best);

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] selected %s (%s) -- score %d, up to %dx internal resolution\n",
        SelectedDevice.DeviceName.c_str(),
        SelectedDevice.VendorName.c_str(),
        SelectedDevice.Score,
        SelectedDevice.MaxScaleFactor);

    return true;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
