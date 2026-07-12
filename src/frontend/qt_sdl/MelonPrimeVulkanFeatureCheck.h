#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanFeatureCheck is owned by the Vulkan build gate"
#endif

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>

class QVulkanDeviceFunctions;
class QVulkanInstance;
class QWindow;
class QString;

namespace MelonPrime::Vulkan
{

struct FeatureInfo
{
    bool instanceAvailable = false;
    bool presentationAvailable = false;
    bool rasterRendererAvailable = false;
    bool computeRendererAvailable = false;
    bool timelineSemaphoreAvailable = false;
    bool validationAvailable = false;
    std::uint32_t apiVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t graphicsQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t computeQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t presentQueueFamily = VK_QUEUE_FAMILY_IGNORED;
    std::uint32_t maxComputeWorkGroupCountY = 0;
    std::uint32_t maxComputeWorkGroupInvocations = 0;
    std::uint32_t maxComputeSharedMemorySize = 0;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthStencilFormat = VK_FORMAT_UNDEFINED;
    std::string deviceName;
    std::string driverName;
    std::string unavailableReason;
};

class DeviceContext
{
public:
    ~DeviceContext();

    DeviceContext(const DeviceContext&) = delete;
    DeviceContext& operator=(const DeviceContext&) = delete;

    const FeatureInfo& featureInfo() const { return m_info; }
    VkPhysicalDevice physicalDevice() const { return m_physicalDevice; }
    VkDevice device() const { return m_device; }
    VkQueue graphicsQueue() const { return m_graphicsQueue; }
    VkQueue computeQueue() const { return m_computeQueue; }
    VkQueue presentQueue() const { return m_presentQueue; }
    QVulkanDeviceFunctions* functions() const { return m_functions; }

private:
    friend std::shared_ptr<DeviceContext> CreateDeviceContext(QWindow*, FeatureInfo&);
    DeviceContext() = default;

    FeatureInfo m_info;
    QVulkanInstance* m_instance = nullptr;
    QVulkanDeviceFunctions* m_functions = nullptr;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VkQueue m_computeQueue = VK_NULL_HANDLE;
    VkQueue m_presentQueue = VK_NULL_HANDLE;
};

std::shared_ptr<DeviceContext> CreateDeviceContext(QWindow* surfaceWindow, FeatureInfo& info);
void LogFeatureInfo(const FeatureInfo& info);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
int RunProbeHarness(const QString& outputPath, int iterations = 10);
#endif

} // namespace MelonPrime::Vulkan
