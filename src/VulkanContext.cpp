#include "VulkanContext.h"

#include "Platform.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace melonDS
{
namespace
{
std::atomic<bool> gNoTimeline{false};
std::atomic<bool> gNoDynamic{false};

constexpr const char* kSurfaceExtension = "VK_KHR_surface";
constexpr const char* kWin32SurfaceExtension = "VK_KHR_win32_surface";
constexpr const char* kXcbSurfaceExtension = "VK_KHR_xcb_surface";
constexpr const char* kXlibSurfaceExtension = "VK_KHR_xlib_surface";
constexpr const char* kWaylandSurfaceExtension = "VK_KHR_wayland_surface";
constexpr const char* kMetalSurfaceExtension = "VK_EXT_metal_surface";
constexpr const char* kPortabilityEnumerationExtension = "VK_KHR_portability_enumeration";
constexpr const char* kPortabilitySubsetExtension = "VK_KHR_portability_subset";

bool HasExtension(const char* name, const std::vector<VkExtensionProperties>& extensions)
{
    return std::any_of(
        extensions.begin(), extensions.end(),
        [name](const VkExtensionProperties& extension) {
            return std::strcmp(name, extension.extensionName) == 0;
        });
}

int DeviceScore(VkPhysicalDevice device)
{
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device, &properties);
    int score = 0;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        score += 1000;
    else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        score += 500;
    score += static_cast<int>(properties.limits.maxImageDimension2D / 1024u);
    return score;
}

VulkanPlatformRequirements SelectPlatformRequirements(
    const std::vector<VkExtensionProperties>& available)
{
    VulkanPlatformRequirements requirements{};
    if (!HasExtension(kSurfaceExtension, available))
        return requirements;
    requirements.InstanceExtensions.emplace_back(kSurfaceExtension);

#if defined(_WIN32)
    if (HasExtension(kWin32SurfaceExtension, available))
    {
        requirements.InstanceExtensions.emplace_back(kWin32SurfaceExtension);
        requirements.PreferredSurfaceBackend = VulkanDesktopSurfaceBackend::Win32;
        requirements.SurfaceExtensionsReady = true;
    }
#elif defined(__APPLE__)
    if (HasExtension(kMetalSurfaceExtension, available))
    {
        requirements.InstanceExtensions.emplace_back(kMetalSurfaceExtension);
        requirements.PreferredSurfaceBackend = VulkanDesktopSurfaceBackend::Metal;
        requirements.SurfaceExtensionsReady = true;
    }
    if (HasExtension(kPortabilityEnumerationExtension, available))
    {
        requirements.InstanceExtensions.emplace_back(kPortabilityEnumerationExtension);
        requirements.InstanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        requirements.PortabilityEnumerationEnabled = true;
    }
#elif defined(__unix__)
    const std::array<std::pair<const char*, VulkanDesktopSurfaceBackend>, 3> candidates = {{
        {kWaylandSurfaceExtension, VulkanDesktopSurfaceBackend::Wayland},
        {kXcbSurfaceExtension, VulkanDesktopSurfaceBackend::Xcb},
        {kXlibSurfaceExtension, VulkanDesktopSurfaceBackend::Xlib},
    }};
    for (const auto& [extension, backend] : candidates)
    {
        if (!HasExtension(extension, available))
            continue;
        requirements.InstanceExtensions.emplace_back(extension);
        if (requirements.PreferredSurfaceBackend == VulkanDesktopSurfaceBackend::None)
            requirements.PreferredSurfaceBackend = backend;
    }
    requirements.SurfaceExtensionsReady =
        requirements.PreferredSurfaceBackend != VulkanDesktopSurfaceBackend::None;
#else
    // Unknown desktop window systems may still use a platform-neutral headless
    // graphics context. A concrete surface backend is selected only above.
    requirements.SurfaceExtensionsReady = false;
#endif
    return requirements;
}

template <typename FunctionType>
FunctionType LoadDeviceFunction(VkDevice device, const char* extensionName, const char* coreName)
{
    auto function = reinterpret_cast<FunctionType>(vkGetDeviceProcAddr(device, extensionName));
    if (function == nullptr && coreName != nullptr)
        function = reinterpret_cast<FunctionType>(vkGetDeviceProcAddr(device, coreName));
    return function;
}
} // namespace

VulkanContext& VulkanContext::Get()
{
    static VulkanContext context;
    return context;
}

void VulkanContext::SetCompatibilityOverrides(
    bool disableTimelineSemaphores,
    bool disableDynamicTextureIndexing)
{
    gNoTimeline.store(disableTimelineSemaphores);
    gNoDynamic.store(disableDynamicTextureIndexing);
}

bool VulkanContext::Acquire()
{
    std::lock_guard<std::mutex> lock(ContextLock);
    if (ReferenceCount++)
        return Device != VK_NULL_HANDLE;
    if (!initializeLocked())
    {
        ReferenceCount = 0;
        return false;
    }
    return true;
}

void VulkanContext::Release()
{
    std::lock_guard<std::mutex> lock(ContextLock);
    if (ReferenceCount == 0)
        return;
    if (--ReferenceCount == 0)
        shutdownLocked();
}

bool VulkanContext::IsReady() const
{
    std::lock_guard<std::mutex> lock(ContextLock);
    return Device != VK_NULL_HANDLE;
}

bool VulkanContext::MarkDeviceLost(const char* reason)
{
    std::lock_guard<std::mutex> lock(DeviceLossLock);
    if (DeviceLostFlag)
        return false;
    DeviceLostFlag = true;
    DeviceLostReasonText = (reason != nullptr) ? reason : "unknown";
    Platform::Log(
        Platform::LogLevel::Error,
        "VulkanContext: device lost (%s); refusing further submits until the "
        "context is fully released and reacquired\n",
        DeviceLostReasonText.c_str());
    return true;
}

bool VulkanContext::IsDeviceLost() const
{
    std::lock_guard<std::mutex> lock(DeviceLossLock);
    return DeviceLostFlag;
}

std::string VulkanContext::DeviceLostReason() const
{
    std::lock_guard<std::mutex> lock(DeviceLossLock);
    return DeviceLostReasonText;
}

bool VulkanContext::initializeLocked()
{
    ForceDisableTimelineSemaphores = gNoTimeline.load();
    ForceDisableDynamicTextureIndexing = gNoDynamic.load();
    if (volkInitialize() != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[MelonPrime] VulkanContext: volkInitialize failed\n");
        return false;
    }

    u32 instanceExtensionCount = 0;
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &instanceExtensionCount, nullptr) != VK_SUCCESS)
    {
        return false;
    }
    std::vector<VkExtensionProperties> instanceExtensions(instanceExtensionCount);
    if (vkEnumerateInstanceExtensionProperties(
            nullptr, &instanceExtensionCount, instanceExtensions.data()) != VK_SUCCESS)
    {
        return false;
    }
    PlatformRequirements = SelectPlatformRequirements(instanceExtensions);
    if (!PlatformRequirements.SurfaceExtensionsReady)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[MelonPrime] VulkanContext: no supported desktop surface extension set\n");
        return false;
    }
    std::vector<const char*> enabledInstanceExtensions;
    enabledInstanceExtensions.reserve(PlatformRequirements.InstanceExtensions.size());
    for (const std::string& extension : PlatformRequirements.InstanceExtensions)
        enabledInstanceExtensions.push_back(extension.c_str());

    VkApplicationInfo applicationInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    applicationInfo.pApplicationName = "melonPrimeDS";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "melonPrimeDS";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.flags = PlatformRequirements.InstanceFlags;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<u32>(enabledInstanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = enabledInstanceExtensions.data();
    if (vkCreateInstance(&instanceInfo, nullptr, &Instance) != VK_SUCCESS)
    {
        Platform::Log(Platform::LogLevel::Error,
            "[MelonPrime] VulkanContext: vkCreateInstance failed\n");
        return false;
    }
    volkLoadInstance(Instance);

    const auto failAfterInstance = [this](const char* stage) {
        Platform::Log(Platform::LogLevel::Error,
            "[MelonPrime] VulkanContext: %s failed\n", stage);
        shutdownLocked();
        return false;
    };

    u32 deviceCount = 0;
    if (vkEnumeratePhysicalDevices(Instance, &deviceCount, nullptr) != VK_SUCCESS
        || deviceCount == 0)
    {
        return failAfterInstance("physical device enumeration");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    if (vkEnumeratePhysicalDevices(Instance, &deviceCount, devices.data()) != VK_SUCCESS)
        return failAfterInstance("physical device enumeration");
    PhysicalDevice = *std::max_element(
        devices.begin(), devices.end(),
        [](VkPhysicalDevice left, VkPhysicalDevice right) {
            return DeviceScore(left) < DeviceScore(right);
        });

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(PhysicalDevice, &properties);
    TimestampPeriod = properties.limits.timestampPeriod;
    TimestampQueriesSupported = properties.limits.timestampComputeAndGraphics != 0;
    DeviceProfile.VendorId = properties.vendorID;
    DeviceProfile.DeviceId = properties.deviceID;
    DeviceProfile.DeviceName = properties.deviceName;
    DeviceProfile.IsNvidia = properties.vendorID == 0x10DE;
    DeviceProfile.IsAmd = properties.vendorID == 0x1002 || properties.vendorID == 0x1022;
    DeviceProfile.IsIntel = properties.vendorID == 0x8086;
    DeviceProfile.IsQualcomm = properties.vendorID == 0x5143;
    DeviceProfile.IsAdreno = DeviceProfile.IsQualcomm;
    DeviceProfile.IsArmMali = properties.vendorID == 0x13B5;
    DeviceProfile.IsPowerVR = properties.vendorID == 0x1010;

    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(PhysicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(
        PhysicalDevice, &queueFamilyCount, queueFamilies.data());
    GraphicsQueueFamilyIndex = UINT32_MAX;
    CreatedQueueFamilies.clear();
    for (u32 index = 0; index < queueFamilyCount; ++index)
    {
        if (queueFamilies[index].queueCount == 0)
            continue;
        CreatedQueueFamilies.push_back(index);
        if (GraphicsQueueFamilyIndex == UINT32_MAX
            && (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            GraphicsQueueFamilyIndex = index;
        }
    }
    if (GraphicsQueueFamilyIndex == UINT32_MAX)
        return failAfterInstance("graphics queue selection");

    u32 deviceExtensionCount = 0;
    if (vkEnumerateDeviceExtensionProperties(
            PhysicalDevice, nullptr, &deviceExtensionCount, nullptr) != VK_SUCCESS)
    {
        return failAfterInstance("device extension enumeration");
    }
    std::vector<VkExtensionProperties> deviceExtensions(deviceExtensionCount);
    if (vkEnumerateDeviceExtensionProperties(
            PhysicalDevice, nullptr, &deviceExtensionCount, deviceExtensions.data()) != VK_SUCCESS)
    {
        return failAfterInstance("device extension enumeration");
    }
    if (!HasExtension(VK_KHR_SWAPCHAIN_EXTENSION_NAME, deviceExtensions))
        return failAfterInstance("VK_KHR_swapchain requirement");

    const bool timelineExtensionAvailable =
        HasExtension(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME, deviceExtensions)
        && !ForceDisableTimelineSemaphores;
    const bool descriptorIndexingExtensionAvailable =
        HasExtension(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME, deviceExtensions)
        && !ForceDisableDynamicTextureIndexing;
    const bool hostQueryResetExtensionAvailable =
        HasExtension(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME, deviceExtensions);

    VkPhysicalDeviceFeatures2 availableFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    VkPhysicalDeviceTimelineSemaphoreFeatures availableTimeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceDescriptorIndexingFeatures availableIndexing{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    VkPhysicalDeviceHostQueryResetFeatures availableHostQueryReset{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
    void** availableNext = &availableFeatures.pNext;
    if (timelineExtensionAvailable)
    {
        *availableNext = &availableTimeline;
        availableNext = &availableTimeline.pNext;
    }
    if (descriptorIndexingExtensionAvailable)
    {
        *availableNext = &availableIndexing;
        availableNext = &availableIndexing.pNext;
    }
    if (hostQueryResetExtensionAvailable)
    {
        *availableNext = &availableHostQueryReset;
        availableNext = &availableHostQueryReset.pNext;
    }
    vkGetPhysicalDeviceFeatures2(PhysicalDevice, &availableFeatures);

    const bool enableTimeline = timelineExtensionAvailable
        && availableTimeline.timelineSemaphore == VK_TRUE;
    const bool enableRuntimeDescriptorArray = descriptorIndexingExtensionAvailable
        && availableIndexing.runtimeDescriptorArray == VK_TRUE
        && availableIndexing.descriptorBindingPartiallyBound == VK_TRUE;
    const bool enableNonUniformIndexing = descriptorIndexingExtensionAvailable
        && availableIndexing.shaderSampledImageArrayNonUniformIndexing == VK_TRUE;
    const bool enableDescriptorIndexing =
        enableRuntimeDescriptorArray || enableNonUniformIndexing;
    const bool enableHostQueryReset = hostQueryResetExtensionAvailable
        && availableHostQueryReset.hostQueryReset == VK_TRUE;

    std::vector<const char*> enabledDeviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    if (enableTimeline)
        enabledDeviceExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    if (enableDescriptorIndexing)
        enabledDeviceExtensions.push_back(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    if (enableHostQueryReset)
        enabledDeviceExtensions.push_back(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);
    if (HasExtension(kPortabilitySubsetExtension, deviceExtensions))
        enabledDeviceExtensions.push_back(kPortabilitySubsetExtension);

    VkPhysicalDeviceTimelineSemaphoreFeatures enabledTimeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    enabledTimeline.timelineSemaphore = enableTimeline ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceDescriptorIndexingFeatures enabledIndexing{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES};
    enabledIndexing.runtimeDescriptorArray =
        enableRuntimeDescriptorArray ? VK_TRUE : VK_FALSE;
    enabledIndexing.descriptorBindingPartiallyBound =
        enableRuntimeDescriptorArray ? VK_TRUE : VK_FALSE;
    enabledIndexing.shaderSampledImageArrayNonUniformIndexing =
        enableNonUniformIndexing ? VK_TRUE : VK_FALSE;
    VkPhysicalDeviceHostQueryResetFeatures enabledHostQueryReset{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES};
    enabledHostQueryReset.hostQueryReset = enableHostQueryReset ? VK_TRUE : VK_FALSE;
    void* enabledFeatureChain = nullptr;
    void** enabledNext = &enabledFeatureChain;
    if (enableTimeline)
    {
        *enabledNext = &enabledTimeline;
        enabledNext = &enabledTimeline.pNext;
    }
    if (enableDescriptorIndexing)
    {
        *enabledNext = &enabledIndexing;
        enabledNext = &enabledIndexing.pNext;
    }
    if (enableHostQueryReset)
        *enabledNext = &enabledHostQueryReset;

    float queuePriority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(CreatedQueueFamilies.size());
    for (u32 family : CreatedQueueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &queuePriority;
        queueInfos.push_back(queueInfo);
    }

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = enabledFeatureChain;
    deviceInfo.queueCreateInfoCount = static_cast<u32>(queueInfos.size());
    deviceInfo.pQueueCreateInfos = queueInfos.data();
    deviceInfo.enabledExtensionCount = static_cast<u32>(enabledDeviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();
    deviceInfo.pEnabledFeatures = &availableFeatures.features;
    if (vkCreateDevice(PhysicalDevice, &deviceInfo, nullptr, &Device) != VK_SUCCESS)
        return failAfterInstance("vkCreateDevice");

    volkLoadDevice(Device);
    vkGetDeviceQueue(Device, GraphicsQueueFamilyIndex, 0, &GraphicsQueue);
    PresentQueue = GraphicsQueue;
    PresentQueueFamilyIndex = GraphicsQueueFamilyIndex;
    PresentQueueResolved = false;

    WaitSemaphores = LoadDeviceFunction<PFN_vkWaitSemaphoresKHR>(
        Device, "vkWaitSemaphoresKHR", "vkWaitSemaphores");
    GetSemaphoreCounterValueFn =
        LoadDeviceFunction<PFN_vkGetSemaphoreCounterValueKHR>(
            Device, "vkGetSemaphoreCounterValueKHR", "vkGetSemaphoreCounterValue");
    ResetQueryPool = LoadDeviceFunction<PFN_vkResetQueryPoolEXT>(
        Device, "vkResetQueryPoolEXT", "vkResetQueryPool");
    TimelineSemaphoresSupported = enableTimeline
        && WaitSemaphores != nullptr
        && GetSemaphoreCounterValueFn != nullptr;
    DynamicTextureIndexingSupported = enableRuntimeDescriptorArray;
    NonUniformTextureIndexingSupported = enableNonUniformIndexing;

    Platform::Log(
        Platform::LogLevel::Info,
        "[MelonPrime] VulkanContext ready: device=%s vendor=%04x graphicsFamily=%u "
        "createdFamilies=%zu timeline=%d indexing=%d portability=%d\n",
        properties.deviceName,
        properties.vendorID,
        GraphicsQueueFamilyIndex,
        CreatedQueueFamilies.size(),
        TimelineSemaphoresSupported ? 1 : 0,
        DynamicTextureIndexingSupported ? 1 : 0,
        PlatformRequirements.PortabilityEnumerationEnabled ? 1 : 0);
    return true;
}

bool VulkanContext::ResolvePresentQueue(VkSurfaceKHR surface)
{
    std::lock_guard<std::mutex> lock(ContextLock);
    if (Device == VK_NULL_HANDLE || PhysicalDevice == VK_NULL_HANDLE
        || surface == VK_NULL_HANDLE)
    {
        return false;
    }

    u32 selectedFamily = UINT32_MAX;
    for (u32 family : CreatedQueueFamilies)
    {
        VkBool32 supported = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(
                PhysicalDevice, family, surface, &supported) != VK_SUCCESS
            || supported != VK_TRUE)
        {
            continue;
        }
        selectedFamily = family;
        if (family == GraphicsQueueFamilyIndex)
            break;
    }
    if (selectedFamily == UINT32_MAX)
        return false;

    PresentQueueFamilyIndex = selectedFamily;
    vkGetDeviceQueue(Device, PresentQueueFamilyIndex, 0, &PresentQueue);
    PresentQueueResolved = PresentQueue != VK_NULL_HANDLE;
    if (PresentQueueResolved)
    {
        Platform::Log(
            Platform::LogLevel::Info,
            "[MelonPrime] VulkanContext present queue: graphicsFamily=%u "
            "presentFamily=%u separate=%d\n",
            GraphicsQueueFamilyIndex,
            PresentQueueFamilyIndex,
            PresentQueueFamilyIndex != GraphicsQueueFamilyIndex ? 1 : 0);
    }
    return PresentQueueResolved;
}

void VulkanContext::shutdownLocked()
{
    if (Device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(Device);
        vkDestroyDevice(Device, nullptr);
    }
    Device = VK_NULL_HANDLE;
    GraphicsQueue = VK_NULL_HANDLE;
    PresentQueue = VK_NULL_HANDLE;
    GraphicsQueueFamilyIndex = UINT32_MAX;
    PresentQueueFamilyIndex = UINT32_MAX;
    PresentQueueResolved = false;
    CreatedQueueFamilies.clear();
    PhysicalDevice = VK_NULL_HANDLE;
    if (Instance != VK_NULL_HANDLE)
        vkDestroyInstance(Instance, nullptr);
    Instance = VK_NULL_HANDLE;
    WaitSemaphores = nullptr;
    GetSemaphoreCounterValueFn = nullptr;
    ResetQueryPool = nullptr;
    TimestampPeriod = 0;
    TimestampQueriesSupported = false;
    TimelineSemaphoresSupported = false;
    DynamicTextureIndexingSupported = false;
    NonUniformTextureIndexingSupported = false;
    DeviceProfile = {};
    PlatformRequirements = {};

    {
        std::lock_guard<std::mutex> lossLock(DeviceLossLock);
        DeviceLostFlag = false;
        DeviceLostReasonText.clear();
    }
}

u32 VulkanContext::FindMemoryType(
    u32 typeBits,
    VkMemoryPropertyFlags properties) const
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memoryProperties);
    for (u32 index = 0; index < memoryProperties.memoryTypeCount; ++index)
    {
        if ((typeBits & (1u << index)) != 0
            && (memoryProperties.memoryTypes[index].propertyFlags & properties)
                == properties)
        {
            return index;
        }
    }
    return UINT32_MAX;
}

} // namespace melonDS
