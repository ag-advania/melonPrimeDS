#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanOverlayRenderer requires the Vulkan build gate"
#endif

#include <QImage>
#include <QRect>

#include <volk.h>

#include "types.h"

#include "VulkanReference/VulkanSurfacePresenter.h"

class MelonPrimeVulkanOverlayRenderer
{
public:
    MelonPrimeVulkanOverlayRenderer() = default;
    ~MelonPrimeVulkanOverlayRenderer();

    MelonPrimeVulkanOverlayRenderer(const MelonPrimeVulkanOverlayRenderer&) = delete;
    MelonPrimeVulkanOverlayRenderer& operator=(const MelonPrimeVulkanOverlayRenderer&) = delete;

    bool init();
    void shutdown();

    bool ensureTexture(melonDS::u32 width, melonDS::u32 height);
    bool uploadRegion(const QImage& image, const QRect& rect);
    void setCompositeRect(melonDS::u32 x, melonDS::u32 y, melonDS::u32 width, melonDS::u32 height);
    void clearCompositeRequest();

    bool hasPendingComposite() const noexcept { return compositeRequested; }

    void record(const MelonDSAndroid::VulkanSurfacePresenter::VulkanDesktopOverlayTarget& target);
    static void RecordCallback(
        const MelonDSAndroid::VulkanSurfacePresenter::VulkanDesktopOverlayTarget& target,
        void* userData);

private:
    struct OverlayPushConstants
    {
        float surfaceSize[2]{};
        float drawOrigin[2]{};
        float drawSize[2]{};
    };

    bool ensurePipeline(VkRenderPass renderPass, VkFormat format);
    void destroyPipeline();
    bool uploadPendingRegion();
    bool createTexture(melonDS::u32 width, melonDS::u32 height);
    void destroyTexture();
    bool createTransferResources();
    void destroyTransferResources();

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    melonDS::u32 transferQueueFamilyIndex = UINT32_MAX;

    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    melonDS::u32 textureWidth = 0;
    melonDS::u32 textureHeight = 0;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* stagingMapped = nullptr;
    VkDeviceSize stagingCapacity = 0;

    VkCommandPool transferCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer transferCommandBuffer = VK_NULL_HANDLE;
    VkFence transferFence = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkRenderPass boundRenderPass = VK_NULL_HANDLE;
    VkFormat boundSwapchainFormat = VK_FORMAT_UNDEFINED;
    VkSampler sampler = VK_NULL_HANDLE;

    QImage pendingUploadImage;
    QRect pendingUploadRect;
    bool pendingUpload = false;

    melonDS::u32 compositeX = 0;
    melonDS::u32 compositeY = 0;
    melonDS::u32 compositeWidth = 0;
    melonDS::u32 compositeHeight = 0;
    bool compositeRequested = false;
    bool initialized = false;
};
