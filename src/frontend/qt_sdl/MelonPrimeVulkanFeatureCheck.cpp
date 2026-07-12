#include "MelonPrimeVulkanFeatureCheck.h"

#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <set>
#include <utility>
#include <vector>

#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "main.h"

namespace MelonPrime::Vulkan
{

namespace
{

struct Candidate
{
    VkPhysicalDevice device = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceFeatures features{};
    FeatureInfo info;
    bool swapchainAvailable = false;
    bool timelineExtensionAvailable = false;
    int score = std::numeric_limits<int>::min();
};

bool HasExtension(const std::vector<VkExtensionProperties>& extensions, const char* name)
{
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto& extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

std::vector<VkExtensionProperties> DeviceExtensions(
    QVulkanFunctions* functions,
    VkPhysicalDevice device)
{
    std::uint32_t count = 0;
    if (functions->vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS)
        return {};
    std::vector<VkExtensionProperties> extensions(count);
    if (count && functions->vkEnumerateDeviceExtensionProperties(
            device, nullptr, &count, extensions.data()) != VK_SUCCESS)
    {
        return {};
    }
    extensions.resize(count);
    return extensions;
}

bool FormatSupports(
    QVulkanFunctions* functions,
    VkPhysicalDevice device,
    VkFormat format,
    VkFormatFeatureFlags required)
{
    VkFormatProperties properties{};
    functions->vkGetPhysicalDeviceFormatProperties(device, format, &properties);
    return (properties.optimalTilingFeatures & required) == required;
}

VkFormat ChooseColorFormat(QVulkanFunctions* functions, VkPhysicalDevice device)
{
    constexpr VkFormatFeatureFlags required =
        VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
    for (VkFormat format : {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM})
    {
        if (FormatSupports(functions, device, format, required))
            return format;
    }
    return VK_FORMAT_UNDEFINED;
}

VkFormat ChooseDepthFormat(QVulkanFunctions* functions, VkPhysicalDevice device)
{
    for (VkFormat format : {VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT})
    {
        if (FormatSupports(functions, device, format,
                VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        {
            return format;
        }
    }
    return VK_FORMAT_UNDEFINED;
}

bool TimelineAvailable(
    QVulkanFunctions* functions,
    VkPhysicalDevice device,
    const VkPhysicalDeviceProperties& properties,
    bool extensionAvailable)
{
    if (VK_API_VERSION_MAJOR(properties.apiVersion) < 1 ||
        (VK_API_VERSION_MAJOR(properties.apiVersion) == 1 &&
         VK_API_VERSION_MINOR(properties.apiVersion) < 2 && !extensionAvailable))
    {
        return false;
    }

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    features2.pNext = &timeline;
    functions->vkGetPhysicalDeviceFeatures2(device, &features2);
    return timeline.timelineSemaphore == VK_TRUE;
}

Candidate InspectCandidate(
    QVulkanInstance& instance,
    QWindow* surfaceWindow,
    VkPhysicalDevice device)
{
    QVulkanFunctions* functions = instance.functions();
    Candidate candidate;
    candidate.device = device;
    functions->vkGetPhysicalDeviceProperties(device, &candidate.properties);
    functions->vkGetPhysicalDeviceFeatures(device, &candidate.features);

    const auto extensions = DeviceExtensions(functions, device);
    candidate.swapchainAvailable = HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    candidate.timelineExtensionAvailable = HasExtension(
        extensions, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    std::uint32_t queueCount = 0;
    functions->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, nullptr);
    std::vector<VkQueueFamilyProperties> queues(queueCount);
    functions->vkGetPhysicalDeviceQueueFamilyProperties(device, &queueCount, queues.data());

    std::uint32_t graphicsPresent = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t computePresent = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t graphics = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t compute = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t present = VK_QUEUE_FAMILY_IGNORED;
    for (std::uint32_t index = 0; index < queueCount; ++index)
    {
        if (!queues[index].queueCount)
            continue;
        const bool supportsPresent = instance.supportsPresent(device, index, surfaceWindow);
        const bool supportsGraphics = (queues[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool supportsCompute = (queues[index].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        if (supportsGraphics && graphics == VK_QUEUE_FAMILY_IGNORED)
            graphics = index;
        if (supportsCompute && compute == VK_QUEUE_FAMILY_IGNORED)
            compute = index;
        if (supportsPresent && present == VK_QUEUE_FAMILY_IGNORED)
            present = index;
        if (supportsGraphics && supportsPresent && graphicsPresent == VK_QUEUE_FAMILY_IGNORED)
            graphicsPresent = index;
        if (supportsCompute && supportsPresent && computePresent == VK_QUEUE_FAMILY_IGNORED)
            computePresent = index;
    }

    if (graphicsPresent != VK_QUEUE_FAMILY_IGNORED)
    {
        graphics = graphicsPresent;
        present = graphicsPresent;
    }
    if (computePresent != VK_QUEUE_FAMILY_IGNORED && compute == VK_QUEUE_FAMILY_IGNORED)
        compute = computePresent;

    candidate.info.graphicsQueueFamily = graphics;
    candidate.info.computeQueueFamily = compute;
    candidate.info.presentQueueFamily = present;
    candidate.info.presentationAvailable = present != VK_QUEUE_FAMILY_IGNORED;
    candidate.info.colorFormat = ChooseColorFormat(functions, device);
    candidate.info.depthStencilFormat = ChooseDepthFormat(functions, device);
    candidate.info.apiVersion = candidate.properties.apiVersion;
    candidate.info.vendorId = candidate.properties.vendorID;
    candidate.info.deviceId = candidate.properties.deviceID;
    candidate.info.driverVersion = candidate.properties.driverVersion;
    candidate.info.deviceName = candidate.properties.deviceName;
    candidate.info.driverName = "driverVersion=" + std::to_string(candidate.properties.driverVersion);
    candidate.info.maxComputeWorkGroupCountY = candidate.properties.limits.maxComputeWorkGroupCount[1];
    candidate.info.maxComputeWorkGroupInvocations =
        candidate.properties.limits.maxComputeWorkGroupInvocations;
    candidate.info.maxComputeSharedMemorySize =
        candidate.properties.limits.maxComputeSharedMemorySize;
    candidate.info.timelineSemaphoreAvailable = TimelineAvailable(
        functions, device, candidate.properties, candidate.timelineExtensionAvailable);

    const auto& limits = candidate.properties.limits;
    candidate.info.rasterRendererAvailable =
        graphics != VK_QUEUE_FAMILY_IGNORED &&
        candidate.info.presentationAvailable &&
        candidate.swapchainAvailable &&
        candidate.info.colorFormat != VK_FORMAT_UNDEFINED &&
        candidate.info.depthStencilFormat != VK_FORMAT_UNDEFINED &&
        limits.maxImageDimension2D >= 4096 &&
        limits.maxStorageBufferRange >= 64u * 1024u * 1024u &&
        limits.maxUniformBufferRange >= 16u * 1024u &&
        limits.maxPushConstantsSize >= 128 &&
        limits.maxPerStageDescriptorStorageBuffers >= 4;

    candidate.info.computeRendererAvailable =
        candidate.info.rasterRendererAvailable &&
        compute != VK_QUEUE_FAMILY_IGNORED &&
        limits.maxComputeWorkGroupCount[0] >= 65535 &&
        limits.maxComputeWorkGroupCount[1] >= 65535 &&
        limits.maxComputeWorkGroupInvocations >= 128 &&
        limits.maxComputeSharedMemorySize >= 16u * 1024u &&
        limits.maxPerStageDescriptorStorageBuffers >= 8;

    if (!candidate.info.rasterRendererAvailable)
    {
        candidate.info.unavailableReason =
            "missing graphics/present queue, swapchain, format, or minimum raster limits";
        return candidate;
    }

    candidate.score = 1000;
    if (candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        candidate.score += 200;
    else if (candidate.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        candidate.score += 100;
    if (graphics == compute && compute == present)
        candidate.score += 50;
    if (candidate.info.timelineSemaphoreAvailable)
        candidate.score += 25;
    if (candidate.info.depthStencilFormat == VK_FORMAT_D32_SFLOAT_S8_UINT)
        candidate.score += 10;
    return candidate;
}

} // namespace

DeviceContext::~DeviceContext()
{
    if (m_device && m_functions)
    {
        m_functions->vkDeviceWaitIdle(m_device);
        m_functions->vkDestroyDevice(m_device, nullptr);
        if (m_instance)
            m_instance->resetDeviceFunctions(m_device);
    }
}

std::shared_ptr<DeviceContext> CreateDeviceContext(QWindow* surfaceWindow, FeatureInfo& info)
{
    info = {};
    if (!surfaceWindow || !surfaceWindow->vulkanInstance())
    {
        info.unavailableReason = "Vulkan probe window has no QVulkanInstance";
        return {};
    }

    QVulkanInstance& instance = *surfaceWindow->vulkanInstance();
    info.instanceAvailable = instance.isValid();
    info.validationAvailable = instance.layers().contains("VK_LAYER_KHRONOS_validation");
    if (!info.instanceAvailable)
    {
        info.unavailableReason = "QVulkanInstance is invalid";
        return {};
    }
    if (QVulkanInstance::surfaceForWindow(surfaceWindow) == VK_NULL_HANDLE)
    {
        info.unavailableReason = "Qt failed to create a Vulkan surface for the probe window";
        return {};
    }

    QVulkanFunctions* functions = instance.functions();
    std::uint32_t deviceCount = 0;
    VkResult result = functions->vkEnumeratePhysicalDevices(
        instance.vkInstance(), &deviceCount, nullptr);
    if (result != VK_SUCCESS || !deviceCount)
    {
        info.unavailableReason = "vkEnumeratePhysicalDevices found no Vulkan device";
        return {};
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    result = functions->vkEnumeratePhysicalDevices(
        instance.vkInstance(), &deviceCount, devices.data());
    if (result != VK_SUCCESS)
    {
        info.unavailableReason = "vkEnumeratePhysicalDevices failed with VkResult " +
            std::to_string(static_cast<int>(result));
        return {};
    }

    Candidate selected;
    for (VkPhysicalDevice device : devices)
    {
        Candidate candidate = InspectCandidate(instance, surfaceWindow, device);
        if (candidate.score > selected.score)
            selected = std::move(candidate);
    }
    if (selected.device == VK_NULL_HANDLE || !selected.info.rasterRendererAvailable)
    {
        info.unavailableReason = selected.info.unavailableReason.empty()
            ? "no physical device meets the Vulkan raster baseline"
            : selected.info.unavailableReason;
        return {};
    }

    std::set<std::uint32_t> uniqueFamilies{
        selected.info.graphicsQueueFamily,
        selected.info.computeQueueFamily,
        selected.info.presentQueueFamily};
    uniqueFamilies.erase(VK_QUEUE_FAMILY_IGNORED);
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    queueInfos.reserve(uniqueFamilies.size());
    for (std::uint32_t family : uniqueFamilies)
    {
        VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    std::vector<const char*> enabledExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const bool timelineNeedsExtension = selected.info.timelineSemaphoreAvailable &&
        (VK_API_VERSION_MAJOR(selected.properties.apiVersion) < 1 ||
         (VK_API_VERSION_MAJOR(selected.properties.apiVersion) == 1 &&
          VK_API_VERSION_MINOR(selected.properties.apiVersion) < 2));
    if (timelineNeedsExtension)
        enabledExtensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);

    VkPhysicalDeviceTimelineSemaphoreFeatures timeline{
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    timeline.timelineSemaphore = selected.info.timelineSemaphoreAvailable ? VK_TRUE : VK_FALSE;
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pNext = selected.info.timelineSemaphoreAvailable ? &timeline : nullptr;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(enabledExtensions.size());
    createInfo.ppEnabledExtensionNames = enabledExtensions.data();

    VkDevice device = VK_NULL_HANDLE;
    result = functions->vkCreateDevice(selected.device, &createInfo, nullptr, &device);
    if (result != VK_SUCCESS)
    {
        info = selected.info;
        info.unavailableReason = "vkCreateDevice failed with VkResult " +
            std::to_string(static_cast<int>(result));
        return {};
    }

    auto context = std::shared_ptr<DeviceContext>(new DeviceContext());
    context->m_info = selected.info;
    context->m_info.instanceAvailable = true;
    context->m_info.unavailableReason.clear();
    context->m_instance = &instance;
    context->m_physicalDevice = selected.device;
    context->m_device = device;
    context->m_functions = instance.deviceFunctions(device);
    context->m_functions->vkGetDeviceQueue(
        device, selected.info.graphicsQueueFamily, 0, &context->m_graphicsQueue);
    if (selected.info.computeQueueFamily != VK_QUEUE_FAMILY_IGNORED)
    {
        context->m_functions->vkGetDeviceQueue(
            device, selected.info.computeQueueFamily, 0, &context->m_computeQueue);
    }
    context->m_functions->vkGetDeviceQueue(
        device, selected.info.presentQueueFamily, 0, &context->m_presentQueue);
    info = context->m_info;
    return context;
}

void LogFeatureInfo(const FeatureInfo& info)
{
    if (!info.instanceAvailable || !info.rasterRendererAvailable)
    {
        melonDS::Platform::Log(melonDS::Platform::LogLevel::Error,
            "[MelonPrime] Vulkan probe unavailable: %s\n",
            info.unavailableReason.empty() ? "unknown reason" : info.unavailableReason.c_str());
        return;
    }

    melonDS::Platform::Log(melonDS::Platform::LogLevel::Info,
        "[MelonPrime] Vulkan device: name=%s vendor=0x%04x device=0x%04x "
        "api=%u.%u.%u driver=%u queues(g=%u,c=%u,p=%u) formats(color=%d,depth=%d) "
        "timeline=%s raster=%s compute=%s computeLimits(y=%u,invocations=%u,shared=%u)\n",
        info.deviceName.c_str(), info.vendorId, info.deviceId,
        VK_API_VERSION_MAJOR(info.apiVersion), VK_API_VERSION_MINOR(info.apiVersion),
        VK_API_VERSION_PATCH(info.apiVersion), info.driverVersion,
        info.graphicsQueueFamily, info.computeQueueFamily, info.presentQueueFamily,
        static_cast<int>(info.colorFormat), static_cast<int>(info.depthStencilFormat),
        info.timelineSemaphoreAvailable ? "yes" : "no",
        info.rasterRendererAvailable ? "yes" : "no",
        info.computeRendererAvailable ? "yes" : "no",
        info.maxComputeWorkGroupCountY, info.maxComputeWorkGroupInvocations,
        info.maxComputeSharedMemorySize);
}

#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
int RunProbeHarness(const QString& outputPath, int iterations)
{
    FeatureInfo info;
    int completed = 0;
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (iterations <= 0)
        iterations = 1;

    if (host.ensureCreated())
    {
        for (int index = 0; index < iterations; ++index)
        {
            QWindow window;
            window.setSurfaceType(QSurface::VulkanSurface);
            window.setVulkanInstance(&host.instance());
            window.resize(1, 1);
            window.create();
            auto context = CreateDeviceContext(&window, info);
            context.reset();
            window.destroy();
            if (!info.rasterRendererAvailable)
                break;
            ++completed;
        }
    }
    else
    {
        info.unavailableReason = host.unavailableReason();
    }

    LogFeatureInfo(info);
    QJsonObject object{
        {"schema_version", 1},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"instance_available", info.instanceAvailable},
        {"presentation_available", info.presentationAvailable},
        {"raster_available", info.rasterRendererAvailable},
        {"compute_available", info.computeRendererAvailable},
        {"timeline_available", info.timelineSemaphoreAvailable},
        {"validation_enabled", host.validationEnabled()},
        {"api_version", static_cast<qint64>(info.apiVersion)},
        {"vendor_id", static_cast<qint64>(info.vendorId)},
        {"device_id", static_cast<qint64>(info.deviceId)},
        {"driver_version", static_cast<qint64>(info.driverVersion)},
        {"device_name", QString::fromStdString(info.deviceName)},
        {"driver_name", QString::fromStdString(info.driverName)},
        {"graphics_queue_family", static_cast<qint64>(info.graphicsQueueFamily)},
        {"compute_queue_family", static_cast<qint64>(info.computeQueueFamily)},
        {"present_queue_family", static_cast<qint64>(info.presentQueueFamily)},
        {"color_format", static_cast<int>(info.colorFormat)},
        {"depth_stencil_format", static_cast<int>(info.depthStencilFormat)},
        {"max_compute_work_group_count_y", static_cast<qint64>(info.maxComputeWorkGroupCountY)},
        {"max_compute_work_group_invocations", static_cast<qint64>(info.maxComputeWorkGroupInvocations)},
        {"max_compute_shared_memory_size", static_cast<qint64>(info.maxComputeSharedMemorySize)},
        {"unavailable_reason", QString::fromStdString(info.unavailableReason)},
    };
    QFile output(outputPath);
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return 2;
    output.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    output.close();
    return completed == iterations ? 0 : 1;
}
#endif

} // namespace MelonPrime::Vulkan
