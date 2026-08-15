// Physical-device capability probe for VK_GOOGLE_display_timing.
//
// Build on macOS without linking a Vulkan loader:
//   clang++ -std=c++20 -Wall -Wextra -Werror \
//     -I/usr/local/opt/vulkan-headers/include \
//     tools/testing/vulkan-google-display-timing-probe.cpp \
//     -o build-mac-vulkan/vulkan-google-display-timing-probe
//
// Pass the exact MoltenVK dylib under test as argv[1].

#include <vulkan/vulkan.h>

#include <dlfcn.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr const char* kPortabilitySubsetExtension =
    "VK_KHR_portability_subset";

template <typename T>
T ResolveInstance(PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                  VkInstance instance,
                  const char* name)
{
    return reinterpret_cast<T>(getInstanceProcAddr(instance, name));
}

template <typename T>
T ResolveDevice(PFN_vkGetDeviceProcAddr getDeviceProcAddr,
                VkDevice device,
                const char* name)
{
    return reinterpret_cast<T>(getDeviceProcAddr(device, name));
}

bool HasExtension(const std::vector<VkExtensionProperties>& extensions,
                  const char* name)
{
    return std::any_of(extensions.begin(), extensions.end(),
        [name](const VkExtensionProperties& extension) {
            return std::strcmp(extension.extensionName, name) == 0;
        });
}

bool EnumerateInstanceExtensions(
    PFN_vkEnumerateInstanceExtensionProperties enumerate,
    std::vector<VkExtensionProperties>& extensions)
{
    uint32_t count = 0;
    VkResult result = enumerate(nullptr, &count, nullptr);
    if (result != VK_SUCCESS)
        return false;

    extensions.resize(count);
    result = enumerate(nullptr, &count, extensions.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        return false;
    extensions.resize(count);
    return true;
}

bool EnumerateDeviceExtensions(
    PFN_vkEnumerateDeviceExtensionProperties enumerate,
    VkPhysicalDevice physicalDevice,
    std::vector<VkExtensionProperties>& extensions)
{
    uint32_t count = 0;
    VkResult result = enumerate(physicalDevice, nullptr, &count, nullptr);
    if (result != VK_SUCCESS)
        return false;

    extensions.resize(count);
    result = enumerate(physicalDevice, nullptr, &count, extensions.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
        return false;
    extensions.resize(count);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::fprintf(stderr, "usage: %s /absolute/path/to/libMoltenVK.dylib\n", argv[0]);
        return 64;
    }

    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!library)
    {
        std::fprintf(stderr, "failed to load MoltenVK: %s\n", dlerror());
        return 1;
    }

    const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        dlsym(library, "vkGetInstanceProcAddr"));
    if (!getInstanceProcAddr)
    {
        std::fprintf(stderr, "vkGetInstanceProcAddr: unavailable\n");
        dlclose(library);
        return 1;
    }

    const auto enumerateInstanceExtensions =
        ResolveInstance<PFN_vkEnumerateInstanceExtensionProperties>(
            getInstanceProcAddr, VK_NULL_HANDLE,
            "vkEnumerateInstanceExtensionProperties");
    const auto createInstance = ResolveInstance<PFN_vkCreateInstance>(
        getInstanceProcAddr, VK_NULL_HANDLE, "vkCreateInstance");
    if (!enumerateInstanceExtensions || !createInstance)
    {
        std::fprintf(stderr, "required global Vulkan entry points: unavailable\n");
        dlclose(library);
        return 1;
    }

    std::vector<VkExtensionProperties> instanceExtensions;
    if (!EnumerateInstanceExtensions(enumerateInstanceExtensions, instanceExtensions))
    {
        std::fprintf(stderr, "instance extension enumeration: failed\n");
        dlclose(library);
        return 1;
    }

    std::vector<const char*> enabledInstanceExtensions;
    VkInstanceCreateFlags instanceFlags = 0;
    if (HasExtension(instanceExtensions, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        enabledInstanceExtensions.push_back(
            VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        instanceFlags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "melonPrimeDS display timing probe";
    applicationInfo.applicationVersion = 1;
    applicationInfo.pEngineName = "none";
    applicationInfo.engineVersion = 1;
    applicationInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.flags = instanceFlags;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount =
        static_cast<uint32_t>(enabledInstanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = enabledInstanceExtensions.data();

    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = createInstance(&instanceInfo, nullptr, &instance);
    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateInstance: failed (%d)\n", result);
        dlclose(library);
        return 1;
    }

    const auto destroyInstance = ResolveInstance<PFN_vkDestroyInstance>(
        getInstanceProcAddr, instance, "vkDestroyInstance");
    const auto enumeratePhysicalDevices =
        ResolveInstance<PFN_vkEnumeratePhysicalDevices>(
            getInstanceProcAddr, instance, "vkEnumeratePhysicalDevices");
    const auto getPhysicalDeviceProperties =
        ResolveInstance<PFN_vkGetPhysicalDeviceProperties>(
            getInstanceProcAddr, instance, "vkGetPhysicalDeviceProperties");
    const auto enumerateDeviceExtensions =
        ResolveInstance<PFN_vkEnumerateDeviceExtensionProperties>(
            getInstanceProcAddr, instance,
            "vkEnumerateDeviceExtensionProperties");
    const auto getQueueFamilyProperties =
        ResolveInstance<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
            getInstanceProcAddr, instance,
            "vkGetPhysicalDeviceQueueFamilyProperties");
    const auto createDevice = ResolveInstance<PFN_vkCreateDevice>(
        getInstanceProcAddr, instance, "vkCreateDevice");
    const auto getDeviceProcAddr = ResolveInstance<PFN_vkGetDeviceProcAddr>(
        getInstanceProcAddr, instance, "vkGetDeviceProcAddr");

    if (!destroyInstance || !enumeratePhysicalDevices ||
        !getPhysicalDeviceProperties || !enumerateDeviceExtensions ||
        !getQueueFamilyProperties || !createDevice || !getDeviceProcAddr)
    {
        std::fprintf(stderr, "required instance Vulkan entry points: unavailable\n");
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    uint32_t physicalDeviceCount = 0;
    result = enumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr);
    if (result != VK_SUCCESS || physicalDeviceCount == 0)
    {
        std::fprintf(stderr, "physical device enumeration: failed (%d)\n", result);
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    result = enumeratePhysicalDevices(
        instance, &physicalDeviceCount, physicalDevices.data());
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        std::fprintf(stderr, "physical device enumeration: failed (%d)\n", result);
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    VkPhysicalDevice physicalDevice = physicalDevices.front();
    VkPhysicalDeviceProperties properties{};
    getPhysicalDeviceProperties(physicalDevice, &properties);

    std::vector<VkExtensionProperties> deviceExtensions;
    if (!EnumerateDeviceExtensions(
            enumerateDeviceExtensions, physicalDevice, deviceExtensions))
    {
        std::fprintf(stderr, "device extension enumeration: failed\n");
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    const bool googleDisplayTiming = HasExtension(
        deviceExtensions, VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);

    std::printf("MoltenVK dylib: %s\n", argv[1]);
    std::printf("Physical device: %s\n", properties.deviceName);
    std::printf("VK_GOOGLE_display_timing device extension: %s\n",
                googleDisplayTiming ? "yes" : "no");

    if (!googleDisplayTiming)
    {
        std::printf("vkGetPastPresentationTimingGOOGLE: unavailable\n");
        std::printf("vkGetRefreshCycleDurationGOOGLE: unavailable\n");
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 0;
    }

    uint32_t queueFamilyCount = 0;
    getQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    getQueueFamilyProperties(
        physicalDevice, &queueFamilyCount, queueFamilies.data());

    uint32_t queueFamily = UINT32_MAX;
    for (uint32_t index = 0; index < queueFamilyCount; ++index)
    {
        if (queueFamilies[index].queueCount > 0 &&
            (queueFamilies[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0)
        {
            queueFamily = index;
            break;
        }
    }
    if (queueFamily == UINT32_MAX)
    {
        std::fprintf(stderr, "graphics queue family: unavailable\n");
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    const float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> enabledDeviceExtensions = {
        VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME,
    };
    if (HasExtension(deviceExtensions, kPortabilitySubsetExtension))
        enabledDeviceExtensions.push_back(kPortabilitySubsetExtension);

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount =
        static_cast<uint32_t>(enabledDeviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    result = createDevice(physicalDevice, &deviceInfo, nullptr, &device);
    if (result != VK_SUCCESS)
    {
        std::fprintf(stderr, "vkCreateDevice with display timing: failed (%d)\n", result);
        destroyInstance(instance, nullptr);
        dlclose(library);
        return 1;
    }

    const auto destroyDevice = ResolveDevice<PFN_vkDestroyDevice>(
        getDeviceProcAddr, device, "vkDestroyDevice");
    const auto getPastPresentationTiming =
        ResolveDevice<PFN_vkGetPastPresentationTimingGOOGLE>(
            getDeviceProcAddr, device,
            "vkGetPastPresentationTimingGOOGLE");
    const auto getRefreshCycleDuration =
        ResolveDevice<PFN_vkGetRefreshCycleDurationGOOGLE>(
            getDeviceProcAddr, device,
            "vkGetRefreshCycleDurationGOOGLE");

    std::printf("vkGetPastPresentationTimingGOOGLE: %s\n",
                getPastPresentationTiming ? "resolved" : "unavailable");
    std::printf("vkGetRefreshCycleDurationGOOGLE: %s\n",
                getRefreshCycleDuration ? "resolved" : "unavailable");

    const bool complete = destroyDevice && getPastPresentationTiming &&
        getRefreshCycleDuration;
    if (destroyDevice)
        destroyDevice(device, nullptr);
    destroyInstance(instance, nullptr);
    dlclose(library);
    return complete ? 0 : 1;
}
