#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include "VulkanContext.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>

#include "Platform.h"
#include "VulkanDispatch.h"

namespace melonDS
{
namespace
{
constexpr std::array<const char*, 1> kRequiredDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

constexpr const char* kTimelineSemaphoreExtension = VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;
constexpr const char* kDescriptorIndexingExtension = VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME;
constexpr const char* kOptionalHostQueryResetExtension = VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME;
constexpr const char* kOptionalDebugUtilsExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr const char* kSurfaceExtension = "VK_KHR_surface";
constexpr const char* kWin32SurfaceExtension = "VK_KHR_win32_surface";
constexpr const char* kWaylandSurfaceExtension = "VK_KHR_wayland_surface";
constexpr const char* kXcbSurfaceExtension = "VK_KHR_xcb_surface";
constexpr const char* kXlibSurfaceExtension = "VK_KHR_xlib_surface";
constexpr const char* kMetalSurfaceExtension = "VK_EXT_metal_surface";
constexpr const char* kPortabilityEnumerationExtension = "VK_KHR_portability_enumeration";
constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";
std::atomic<bool> gForceDisableTimelineSemaphores{false};
std::atomic<bool> gForceDisableDynamicTextureIndexing{false};

bool hasExtension(const char* extensionName, const std::vector<VkExtensionProperties>& extensions)
{
    for (const VkExtensionProperties& extension : extensions)
    {
        if (std::strcmp(extensionName, extension.extensionName) == 0)
            return true;
    }

    return false;
}

bool hasLayer(const char* layerName, const std::vector<VkLayerProperties>& layers)
{
    for (const VkLayerProperties& layer : layers)
    {
        if (std::strcmp(layerName, layer.layerName) == 0)
            return true;
    }

    return false;
}

bool appendRequiredInstanceExtensions(
    const std::vector<VkExtensionProperties>& available,
    std::vector<const char*>& enabled,
    bool& enablePortabilityEnumeration)
{
    if (!hasExtension(kSurfaceExtension, available))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanContext: missing instance extension %s",
            kSurfaceExtension);
        return false;
    }
    enabled.push_back(kSurfaceExtension);

#if defined(_WIN32)
    if (!hasExtension(kWin32SurfaceExtension, available))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanContext: missing instance extension %s",
            kWin32SurfaceExtension);
        return false;
    }
    enabled.push_back(kWin32SurfaceExtension);
#elif defined(__APPLE__)
    if (!hasExtension(kMetalSurfaceExtension, available))
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanContext: missing instance extension %s",
            kMetalSurfaceExtension);
        return false;
    }
    enabled.push_back(kMetalSurfaceExtension);
    if (hasExtension(kPortabilityEnumerationExtension, available))
    {
        enabled.push_back(kPortabilityEnumerationExtension);
        enablePortabilityEnumeration = true;
    }
#else
    bool hasDesktopSurface = false;
    for (const char* extension : {
             kWaylandSurfaceExtension,
             kXcbSurfaceExtension,
             kXlibSurfaceExtension })
    {
        if (hasExtension(extension, available))
        {
            enabled.push_back(extension);
            hasDesktopSurface = true;
        }
    }
    if (!hasDesktopSurface)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanContext: no supported Linux presentation surface extension");
        return false;
    }
#endif
    return true;
}

bool isApiAtLeast(u32 apiVersion, u32 major, u32 minor)
{
    const u32 versionMajor = VK_API_VERSION_MAJOR(apiVersion);
    const u32 versionMinor = VK_API_VERSION_MINOR(apiVersion);
    return (versionMajor > major) || (versionMajor == major && versionMinor >= minor);
}

std::string toLower(std::string value)
{
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
    return value;
}

bool containsInsensitive(const std::string& haystackLower, const char* needle)
{
    return haystackLower.find(needle) != std::string::npos;
}

VulkanDeviceProfile makeDeviceProfile(const VkPhysicalDeviceProperties& deviceProperties)
{
    VulkanDeviceProfile profile{};
    profile.VendorId = deviceProperties.vendorID;
    profile.DeviceId = deviceProperties.deviceID;
    profile.DeviceName = deviceProperties.deviceName;

    const std::string deviceNameLower = toLower(profile.DeviceName);
    profile.IsAdreno = containsInsensitive(deviceNameLower, "adreno");
    profile.IsQualcomm =
        deviceProperties.vendorID == 0x5143u
        || containsInsensitive(deviceNameLower, "qualcomm")
        || profile.IsAdreno;
    profile.IsArmMali =
        deviceProperties.vendorID == 0x13B5u
        || containsInsensitive(deviceNameLower, "mali")
        || containsInsensitive(deviceNameLower, "arm");
    profile.IsPowerVR =
        containsInsensitive(deviceNameLower, "powervr")
        || containsInsensitive(deviceNameLower, "power vr")
        || containsInsensitive(deviceNameLower, "imgtec")
        || containsInsensitive(deviceNameLower, "imagination");
    profile.IsMaliG52Class =
        profile.IsArmMali
        && (containsInsensitive(deviceNameLower, "g52") || containsInsensitive(deviceNameLower, "mali-g52"));
    return profile;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
    void*)
{
    Platform::LogLevel logLevel = Platform::LogLevel::Warn;
    if ((messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0)
        logLevel = Platform::LogLevel::Error;

    const char* message = callbackData != nullptr && callbackData->pMessage != nullptr
        ? callbackData->pMessage
        : "unknown validation message";
    Platform::Log(logLevel, "VulkanValidation: %s", message);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeDebugUtilsMessengerCreateInfo()
{
    VkDebugUtilsMessengerCreateInfoEXT createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
        | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugUtilsMessengerCallback;
    return createInfo;
}
}

VulkanContext& VulkanContext::Get()
{
    static VulkanContext context;
    return context;
}

bool VulkanContext::Acquire()
{
    std::scoped_lock guard(ContextLock);
    if (ReferenceCount > 0)
    {
        ReferenceCount++;
        return true;
    }

    if (!initializeLocked())
        return false;

    ReferenceCount = 1;
    return true;
}

void VulkanContext::Release()
{
    std::scoped_lock guard(ContextLock);
    if (ReferenceCount == 0)
        return;

    ReferenceCount--;
    if (ReferenceCount == 0)
        shutdownLocked();
}

bool VulkanContext::IsReady() const
{
    std::scoped_lock guard(ContextLock);
    return Device != VK_NULL_HANDLE && Queue != VK_NULL_HANDLE;
}

bool VulkanContext::initializeLocked()
{
    ForceDisableTimelineSemaphores = gForceDisableTimelineSemaphores.load(std::memory_order_relaxed);
    ForceDisableDynamicTextureIndexing = gForceDisableDynamicTextureIndexing.load(std::memory_order_relaxed);

    if (!VulkanDispatch::Initialize())
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanContext: failed to initialize Vulkan dispatch");
        return false;
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "melonPrimeDS";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "melonDS";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    u32 extensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanContext: failed to enumerate instance extensions");
        return false;
    }

    std::vector<VkExtensionProperties> instanceExtensions(extensionCount);
    if (vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, instanceExtensions.data()) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanContext: failed to read instance extensions");
        return false;
    }

    std::vector<const char*> enabledInstanceExtensions;
    bool enablePortabilityEnumeration = false;
    if (!appendRequiredInstanceExtensions(
            instanceExtensions,
            enabledInstanceExtensions,
            enablePortabilityEnumeration))
        return false;
    std::vector<const char*> enabledInstanceLayers;
    VkDebugUtilsMessengerCreateInfoEXT debugMessengerCreateInfo{};
    bool enableValidationLayers = false;

#if defined(MELONDS_VULKAN_ENABLE_VALIDATION)
    u32 layerCount = 0;
    if (vkEnumerateInstanceLayerProperties(&layerCount, nullptr) == VK_SUCCESS && layerCount > 0)
    {
        std::vector<VkLayerProperties> instanceLayers(layerCount);
        if (vkEnumerateInstanceLayerProperties(&layerCount, instanceLayers.data()) == VK_SUCCESS)
        {
            const bool hasValidationLayer = hasLayer(kValidationLayerName, instanceLayers);
            const bool hasDebugUtils = hasExtension(kOptionalDebugUtilsExtension, instanceExtensions);
            if (hasValidationLayer && hasDebugUtils)
            {
                enabledInstanceExtensions.push_back(kOptionalDebugUtilsExtension);
                enabledInstanceLayers.push_back(kValidationLayerName);
                debugMessengerCreateInfo = makeDebugUtilsMessengerCreateInfo();
                enableValidationLayers = true;
                Platform::Log(Platform::LogLevel::Warn, "VulkanContext: enabling validation layer for debug build");
            }
            else if (!hasValidationLayer)
            {
                Platform::Log(Platform::LogLevel::Warn, "VulkanContext: debug build without %s", kValidationLayerName);
            }
            else
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: debug build without instance extension %s; validation callback disabled",
                    kOptionalDebugUtilsExtension
                );
            }
        }
    }
#endif

    VkInstanceCreateInfo instanceCreateInfo{};
    instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    if (enablePortabilityEnumeration)
        instanceCreateInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<u32>(enabledInstanceExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = enabledInstanceExtensions.data();
    instanceCreateInfo.enabledLayerCount = static_cast<u32>(enabledInstanceLayers.size());
    instanceCreateInfo.ppEnabledLayerNames = enabledInstanceLayers.data();
    if (enableValidationLayers)
        instanceCreateInfo.pNext = &debugMessengerCreateInfo;

    const VkResult createInstanceResult = vkCreateInstance(&instanceCreateInfo, nullptr, &Instance);
    if (createInstanceResult != VK_SUCCESS)
    {
        Platform::Log(
            Platform::LogLevel::Error,
            "VulkanContext: vkCreateInstance failed VkResult=%d",
            static_cast<int>(createInstanceResult));
        shutdownLocked();
        return false;
    }
    VulkanDispatch::LoadInstance(Instance);
    Platform::Log(
        Platform::LogLevel::Info,
        "VulkanContext: instance API=%u.%u validation=%d",
        VK_API_VERSION_MAJOR(appInfo.apiVersion),
        VK_API_VERSION_MINOR(appInfo.apiVersion),
        enableValidationLayers ? 1 : 0
    );

    if (enableValidationLayers)
    {
        const auto createDebugUtilsMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkCreateDebugUtilsMessengerEXT")
        );
        if (createDebugUtilsMessenger != nullptr)
        {
            const VkResult messengerResult = createDebugUtilsMessenger(
                Instance,
                &debugMessengerCreateInfo,
                nullptr,
                &DebugMessenger
            );
            if (messengerResult != VK_SUCCESS)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: failed to create debug utils messenger (%d)",
                    static_cast<int>(messengerResult)
                );
            }
        }
    }

    u32 physicalDeviceCount = 0;
    if (vkEnumeratePhysicalDevices(Instance, &physicalDeviceCount, nullptr) != VK_SUCCESS || physicalDeviceCount == 0)
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanContext: no physical devices found");
        shutdownLocked();
        return false;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    vkEnumeratePhysicalDevices(Instance, &physicalDeviceCount, physicalDevices.data());

    for (VkPhysicalDevice candidate : physicalDevices)
    {
        u32 deviceExtensionCount = 0;
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, nullptr) != VK_SUCCESS)
            continue;

        std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
        if (vkEnumerateDeviceExtensionProperties(candidate, nullptr, &deviceExtensionCount, deviceExtensions.data()) != VK_SUCCESS)
            continue;

        VkPhysicalDeviceProperties deviceProperties{};
        vkGetPhysicalDeviceProperties(candidate, &deviceProperties);
        const VulkanDeviceProfile candidateProfile = makeDeviceProfile(deviceProperties);
        // Device-level promoted features are only core when both the instance
        // and physical device API versions include the promotion.  Keep the
        // extension names enabled for our Vulkan 1.1 instance even when the
        // selected device itself advertises Vulkan 1.2 or newer.
        const bool apiAtLeast12 =
            isApiAtLeast(appInfo.apiVersion, 1, 2)
            && isApiAtLeast(deviceProperties.apiVersion, 1, 2);

        std::vector<const char*> requiredDeviceExtensions(
            kRequiredDeviceExtensions.begin(),
            kRequiredDeviceExtensions.end());

        bool hasRequiredExtensions = true;
        for (const char* requiredExtension : requiredDeviceExtensions)
        {
            if (!hasExtension(requiredExtension, deviceExtensions))
            {
                hasRequiredExtensions = false;
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing extension %s",
                    deviceProperties.deviceName,
                    requiredExtension
                );
                break;
            }
        }

        if (!hasRequiredExtensions)
            continue;

        std::vector<const char*> enabledDeviceExtensions(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end());
#if defined(__APPLE__)
        if (hasExtension(kPortabilitySubsetExtension, deviceExtensions))
            enabledDeviceExtensions.push_back(kPortabilitySubsetExtension);
#endif
        const bool hasHostQueryReset = hasExtension(kOptionalHostQueryResetExtension, deviceExtensions);
        if (hasHostQueryReset)
            enabledDeviceExtensions.push_back(kOptionalHostQueryResetExtension);

        u32 queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
        if (queueFamilyCount == 0)
            continue;

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

        int selectedQueueFamily = -1;
        for (u32 i = 0; i < queueFamilyCount; i++)
        {
            if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
            {
                selectedQueueFamily = static_cast<int>(i);
                break;
            }
        }

        if (selectedQueueFamily < 0)
        {
            for (u32 i = 0; i < queueFamilyCount; i++)
            {
                if ((queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
                {
                    selectedQueueFamily = static_cast<int>(i);
                    break;
                }
            }
        }

        if (selectedQueueFamily < 0)
            continue;

        const VkQueueFamilyProperties& selectedQueueFamilyProps = queueFamilies[static_cast<u32>(selectedQueueFamily)];
        const bool queueSupportsTimestamps = selectedQueueFamilyProps.timestampValidBits > 0;
        VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeaturesAvailable{};
        timelineFeaturesAvailable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeaturesAvailable{};
        descriptorIndexingFeaturesAvailable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        descriptorIndexingFeaturesAvailable.pNext = &timelineFeaturesAvailable;

        VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeaturesAvailable{};
        hostQueryResetFeaturesAvailable.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
        hostQueryResetFeaturesAvailable.pNext = &descriptorIndexingFeaturesAvailable;

        VkPhysicalDeviceFeatures2 deviceFeatures2{};
        deviceFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        deviceFeatures2.pNext = &hostQueryResetFeaturesAvailable;
        auto getPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            vkGetInstanceProcAddr(Instance, "vkGetPhysicalDeviceFeatures2"));
        if (getPhysicalDeviceFeatures2 == nullptr)
        {
            getPhysicalDeviceFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
                vkGetInstanceProcAddr(Instance, "vkGetPhysicalDeviceFeatures2KHR"));
        }

        if (getPhysicalDeviceFeatures2 != nullptr)
        {
            getPhysicalDeviceFeatures2(candidate, &deviceFeatures2);
        }
        else
        {
            // Fallback for loaders exposing only Vulkan 1.0 symbols.
            vkGetPhysicalDeviceFeatures(candidate, &deviceFeatures2.features);
        }

        if (deviceFeatures2.features.independentBlend != VK_TRUE)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanContext: device '%s' missing feature independentBlend",
                deviceProperties.deviceName
            );
            continue;
        }

        const bool timelineFeatureAvailable = timelineFeaturesAvailable.timelineSemaphore == VK_TRUE;
        const bool timelineExtensionAvailable = apiAtLeast12 || hasExtension(kTimelineSemaphoreExtension, deviceExtensions);
        const bool enableTimelineSemaphores =
            !ForceDisableTimelineSemaphores && timelineFeatureAvailable && timelineExtensionAvailable;
        if (!enableTimelineSemaphores)
        {
            if (ForceDisableTimelineSemaphores)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: forcing timeline semaphore fallback on '%s'",
                    deviceProperties.deviceName
                );
            }
            else if (!timelineFeatureAvailable)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing feature timelineSemaphore; using fence-based sync fallback",
                    deviceProperties.deviceName
                );
            }
            else
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing extension %s for timeline semaphores; using fence-based sync fallback",
                    deviceProperties.deviceName,
                    kTimelineSemaphoreExtension
                );
            }
        }

        const bool dynamicTextureIndexingFeatureAvailable =
            deviceFeatures2.features.shaderSampledImageArrayDynamicIndexing == VK_TRUE;
        // Mali-G52-class devices compile the compat-dynamic-uniform graphics_hw
        // shaders unreliably. Force the base single-descriptor path there so
        // graphics_hw remains available instead of falling over during init.
        const bool forceDynamicTextureIndexingOffForDevice = candidateProfile.IsMaliG52Class;
        const bool enableDynamicTextureIndexing =
            !ForceDisableDynamicTextureIndexing
            && !forceDynamicTextureIndexingOffForDevice
            && dynamicTextureIndexingFeatureAvailable;
        if (!enableDynamicTextureIndexing)
        {
            if (ForceDisableDynamicTextureIndexing)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: forcing dynamic-indexing fallback on '%s'",
                    deviceProperties.deviceName
                );
            }
            else if (forceDynamicTextureIndexingOffForDevice)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: forcing dynamic-indexing fallback on '%s' to keep graphics_hw stable on Mali-G52-class devices",
                    deviceProperties.deviceName
                );
            }
            else
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing feature shaderSampledImageArrayDynamicIndexing; using single-descriptor texture fallback",
                    deviceProperties.deviceName
                );
            }
        }

        const bool descriptorFeatureAvailable =
            descriptorIndexingFeaturesAvailable.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
        const bool descriptorIndexingExtensionAvailable =
            apiAtLeast12 || hasExtension(kDescriptorIndexingExtension, deviceExtensions);
        const bool enableDescriptorIndexing =
            enableDynamicTextureIndexing
            && descriptorFeatureAvailable
            && descriptorIndexingExtensionAvailable;

        if (!enableDescriptorIndexing)
        {
            if (!enableDynamicTextureIndexing)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' using single-descriptor texture fallback (dynamic indexing disabled)",
                    deviceProperties.deviceName
                );
            }
            else if (!descriptorFeatureAvailable)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing feature shaderSampledImageArrayNonUniformIndexing; using compatibility texture path",
                    deviceProperties.deviceName
                );
            }
            else if (!descriptorIndexingExtensionAvailable)
            {
                Platform::Log(
                    Platform::LogLevel::Warn,
                    "VulkanContext: device '%s' missing extension %s; using compatibility texture path",
                    deviceProperties.deviceName,
                    kDescriptorIndexingExtension
                );
            }
        }
        else if (!apiAtLeast12)
        {
            enabledDeviceExtensions.push_back(kDescriptorIndexingExtension);
        }

        VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeatures{};
        timelineFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
        timelineFeatures.timelineSemaphore = enableTimelineSemaphores ? VK_TRUE : VK_FALSE;

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexingFeatures{};
        descriptorIndexingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES;
        descriptorIndexingFeatures.pNext = nullptr;
        descriptorIndexingFeatures.shaderSampledImageArrayNonUniformIndexing = enableDescriptorIndexing ? VK_TRUE : VK_FALSE;

        VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures{};
        hostQueryResetFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES;
        hostQueryResetFeatures.pNext = nullptr;
        const bool enableHostQueryReset = hasHostQueryReset && hostQueryResetFeaturesAvailable.hostQueryReset == VK_TRUE;
        hostQueryResetFeatures.hostQueryReset = enableHostQueryReset ? VK_TRUE : VK_FALSE;

        void* featureChainHead = nullptr;
        if (enableTimelineSemaphores)
            featureChainHead = static_cast<void*>(&timelineFeatures);
        if (enableDescriptorIndexing)
        {
            descriptorIndexingFeatures.pNext = featureChainHead;
            featureChainHead = static_cast<void*>(&descriptorIndexingFeatures);
        }
        if (enableHostQueryReset)
        {
            hostQueryResetFeatures.pNext = featureChainHead;
            featureChainHead = static_cast<void*>(&hostQueryResetFeatures);
        }

        if (enableTimelineSemaphores && !apiAtLeast12)
            enabledDeviceExtensions.push_back(kTimelineSemaphoreExtension);

        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo queueCreateInfo{};
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = static_cast<u32>(selectedQueueFamily);
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceCreateInfo deviceCreateInfo{};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pNext = featureChainHead;
        VkPhysicalDeviceFeatures enabledDeviceFeatures{};
        enabledDeviceFeatures.independentBlend = VK_TRUE;
        deviceCreateInfo.pEnabledFeatures = &enabledDeviceFeatures;
        deviceCreateInfo.queueCreateInfoCount = 1;
        deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
        deviceCreateInfo.enabledExtensionCount = static_cast<u32>(enabledDeviceExtensions.size());
        deviceCreateInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();

        const VkResult createDeviceResult = vkCreateDevice(candidate, &deviceCreateInfo, nullptr, &Device);
        if (createDeviceResult != VK_SUCCESS)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanContext: vkCreateDevice failed for '%s' (%d)",
                deviceProperties.deviceName,
                static_cast<int>(createDeviceResult)
            );
            continue;
        }

        PhysicalDevice = candidate;
        VulkanDispatch::LoadDevice(Device);
        QueueFamilyIndex = static_cast<u32>(selectedQueueFamily);
        vkGetDeviceQueue(Device, QueueFamilyIndex, 0, &Queue);
        TimestampPeriod = deviceProperties.limits.timestampPeriod;
        TimestampQueriesSupported = queueSupportsTimestamps;
        TimelineSemaphoresSupported = enableTimelineSemaphores;
        DynamicTextureIndexingSupported = enableDynamicTextureIndexing;
        DeviceProfile = candidateProfile;
        // Keep Qualcomm/Adreno on the compatibility descriptor path unless we
        // have explicit proof that non-uniform indexing is stable there.
        const bool forceCompatTexturePath = DeviceProfile.IsQualcomm || DeviceProfile.IsAdreno;
        NonUniformTextureIndexingSupported = enableDescriptorIndexing && !forceCompatTexturePath;
        if (enableDescriptorIndexing && forceCompatTexturePath)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanContext: forcing compatibility texture path on '%s' (vendor=%#x device=%#x)",
                deviceProperties.deviceName,
                deviceProperties.vendorID,
                deviceProperties.deviceID
            );
        }
        if (enableDescriptorIndexing && NonUniformTextureIndexingSupported)
        {
            Platform::Log(
                Platform::LogLevel::Warn,
                "VulkanContext: enabling non-uniform texture path on '%s' (vendor=%#x device=%#x)",
                deviceProperties.deviceName,
                deviceProperties.vendorID,
                deviceProperties.deviceID
            );
        }
        Platform::Log(
            Platform::LogLevel::Warn,
            "VulkanContext: selected '%s' (api=%u.%u driver=%#x vendor=%#x device=%#x queueFamily=%u adreno=%d mali=%d powervr=%d g52=%d timeline=%d dynamicIndexing=%d nonUniformTextures=%d forceTimelineOff=%d forceDynamicOff=%d)",
            deviceProperties.deviceName,
            VK_API_VERSION_MAJOR(deviceProperties.apiVersion),
            VK_API_VERSION_MINOR(deviceProperties.apiVersion),
            deviceProperties.driverVersion,
            deviceProperties.vendorID,
            deviceProperties.deviceID,
            QueueFamilyIndex,
            DeviceProfile.IsAdreno ? 1 : 0,
            DeviceProfile.IsArmMali ? 1 : 0,
            DeviceProfile.IsPowerVR ? 1 : 0,
            DeviceProfile.IsMaliG52Class ? 1 : 0,
            TimelineSemaphoresSupported ? 1 : 0,
            DynamicTextureIndexingSupported ? 1 : 0,
            NonUniformTextureIndexingSupported ? 1 : 0,
            ForceDisableTimelineSemaphores ? 1 : 0,
            ForceDisableDynamicTextureIndexing ? 1 : 0
        );
        break;
    }

    if (Device == VK_NULL_HANDLE || Queue == VK_NULL_HANDLE)
    {
        Platform::Log(Platform::LogLevel::Error, "VulkanContext: failed to create logical device");
        shutdownLocked();
        return false;
    }

    WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphoresKHR>(
        vkGetDeviceProcAddr(Device, "vkWaitSemaphoresKHR")
    );
    if (WaitSemaphores == nullptr)
    {
        WaitSemaphores = reinterpret_cast<PFN_vkWaitSemaphoresKHR>(
            vkGetDeviceProcAddr(Device, "vkWaitSemaphores")
        );
    }

    GetSemaphoreCounterValueFn = reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(
        vkGetDeviceProcAddr(Device, "vkGetSemaphoreCounterValueKHR")
    );
    if (GetSemaphoreCounterValueFn == nullptr)
    {
        GetSemaphoreCounterValueFn = reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(
            vkGetDeviceProcAddr(Device, "vkGetSemaphoreCounterValue")
        );
    }

    ResetQueryPool = reinterpret_cast<PFN_vkResetQueryPoolEXT>(
        vkGetDeviceProcAddr(Device, "vkResetQueryPoolEXT")
    );
    if (ResetQueryPool == nullptr)
    {
        ResetQueryPool = reinterpret_cast<PFN_vkResetQueryPoolEXT>(
            vkGetDeviceProcAddr(Device, "vkResetQueryPool")
        );
    }
    if (ResetQueryPool == nullptr)
        TimestampQueriesSupported = false;

    return true;
}

void VulkanContext::shutdownLocked()
{
    if (DebugMessenger != VK_NULL_HANDLE && Instance != VK_NULL_HANDLE)
    {
        const auto destroyDebugUtilsMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(Instance, "vkDestroyDebugUtilsMessengerEXT")
        );
        if (destroyDebugUtilsMessenger != nullptr)
            destroyDebugUtilsMessenger(Instance, DebugMessenger, nullptr);
    }

    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
        vkDestroyDevice(Device, nullptr);
    }

    if (Instance != VK_NULL_HANDLE)
        vkDestroyInstance(Instance, nullptr);

    Instance = VK_NULL_HANDLE;
    DebugMessenger = VK_NULL_HANDLE;
    PhysicalDevice = VK_NULL_HANDLE;
    Device = VK_NULL_HANDLE;
    Queue = VK_NULL_HANDLE;
    QueueFamilyIndex = 0;
    WaitSemaphores = nullptr;
    GetSemaphoreCounterValueFn = nullptr;
    ResetQueryPool = nullptr;
    TimestampPeriod = 0.0f;
    TimestampQueriesSupported = false;
    TimelineSemaphoresSupported = false;
    DynamicTextureIndexingSupported = false;
    NonUniformTextureIndexingSupported = false;
    ForceDisableTimelineSemaphores = false;
    ForceDisableDynamicTextureIndexing = false;
    DeviceProfile = VulkanDeviceProfile{};
}

u32 VulkanContext::FindMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const
{
    std::scoped_lock guard(ContextLock);
    if (PhysicalDevice == VK_NULL_HANDLE)
        return UINT32_MAX;

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memoryProperties);

    for (u32 memoryType = 0; memoryType < memoryProperties.memoryTypeCount; memoryType++)
    {
        const bool typeMatches = (typeBits & (1u << memoryType)) != 0;
        const bool propertiesMatch = (memoryProperties.memoryTypes[memoryType].propertyFlags & properties) == properties;
        if (typeMatches && propertiesMatch)
            return memoryType;
    }

    return UINT32_MAX;
}

void VulkanContext::SetCompatibilityOverrides(bool disableTimelineSemaphores, bool disableDynamicTextureIndexing)
{
    gForceDisableTimelineSemaphores.store(disableTimelineSemaphores, std::memory_order_relaxed);
    gForceDisableDynamicTextureIndexing.store(disableDynamicTextureIndexing, std::memory_order_relaxed);
}

}

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
