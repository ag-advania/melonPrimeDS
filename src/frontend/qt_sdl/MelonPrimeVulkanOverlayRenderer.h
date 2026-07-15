#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanOverlayRenderer requires the Vulkan build gate"
#endif

#include <array>

#include <QImage>
#include <QRect>

#include <volk.h>

#include "types.h"
#include "VulkanR24Sync.h"

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
    void hideOverlay() noexcept;
    void invalidateOverlayTexture() noexcept;
    void bindPresenterTimeline(VkSemaphore semaphore) noexcept;
    void releaseCompletedUploadSlots(melonDS::u64 completedTimelineValue) noexcept;
    void markUploadSubmitted(melonDS::u32 slotIndex, melonDS::u64 completionTimelineValue) noexcept;
    void markLastUploadSubmitted(melonDS::u64 completionTimelineValue) noexcept;
    void collectRetiredResources(melonDS::u64 completedTimelineValue) noexcept;
    void clearCompositeRequest() noexcept { hideOverlay(); }

    bool hasPendingComposite() const noexcept { return compositeRequested; }
    bool hasValidOverlayTexture() const noexcept { return hasValidUploadedOverlay; }

    void recordTransfer(VkCommandBuffer commandBuffer);
    static void RecordTransferCallback(VkCommandBuffer commandBuffer, void* userData);
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

    bool ensurePipeline(VkRenderPass renderPass, VkFormat format, melonDS::u64 retireAfterTimelineValue);
    void retirePipeline(melonDS::u64 retireAfterTimelineValue) noexcept;
    bool ensureUploadSlotCapacity(melonDS::u32 slotIndex, VkDeviceSize required);
    void destroyUploadSlot(melonDS::u32 slotIndex);
    bool createMappedUploadSlot(melonDS::u32 slotIndex, VkDeviceSize capacity);
    bool acquireUploadSlotForStaging();
    bool stagePendingRegion();
    bool recordPendingTransfer(VkCommandBuffer commandBuffer);
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
    VkImageLayout textureLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool textureInitialized = false;
    bool hasValidUploadedOverlay = false;
    melonDS::u64 lastUploadedHudGeneration = 0;
    melonDS::u64 uploadFailureLogBudget = 0;

    struct OverlayUploadSlot
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;
        bool recorded = false;
        melonDS::u64 completionTimelineValue = 0;
    };

    static constexpr size_t kUploadSlotCount = 3;

    OverlayUploadSlot uploadSlots[kUploadSlotCount]{};
    melonDS::u32 activeUploadSlot = 0;
    melonDS::u32 nextUploadSlotIndex = 0;
    melonDS::u32 pendingSubmittedUploadSlot = UINT32_MAX;
    melonDS::u64 lastPresentTimelineValue = 0;
    melonDS::VulkanRetireQueue retiredPipelines;
    VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
    bool useTimelineSemaphores = false;
    VkDeviceSize pendingUploadBytes = 0;

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
