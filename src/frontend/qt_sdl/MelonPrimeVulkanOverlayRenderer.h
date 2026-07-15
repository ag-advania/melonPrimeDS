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

using MelonDSAndroid::OverlayTransferRecord;

enum class OverlayUploadState : melonDS::u8
{
    Free,
    Staged,
    Recorded,
    Submitted,
};

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
    void notifyCompletedSubmissionSerial(melonDS::u64 completedSerial) noexcept;
    void notifySurfaceSubmission(
        melonDS::u64 uploadToken,
        bool submitted,
        melonDS::u64 timelineValue,
        melonDS::u64 submissionSerial) noexcept;
    static void NotifySurfaceSubmissionCallback(
        melonDS::u64 uploadToken,
        bool submitted,
        melonDS::u64 timelineValue,
        melonDS::u64 submissionSerial,
        void* userData);
    [[nodiscard]] OverlayTransferRecord consumeLastTransferRecord() noexcept;
    static bool QueryLastTransferRecordCallback(
        OverlayTransferRecord* outRecord,
        void* userData);
    void markUploadSubmitted(
        melonDS::u32 slotIndex,
        melonDS::u64 completionTimelineValue,
        melonDS::u64 submissionSerial) noexcept;
    void collectRetiredResources(melonDS::u64 completedTimelineValue) noexcept;
    void clearCompositeRequest() noexcept { hideOverlay(); }

    bool hasPendingComposite() const noexcept { return compositeRequested; }
    bool hasValidOverlayTexture() const noexcept { return committedTexture.valid; }

    OverlayTransferRecord recordTransfer(VkCommandBuffer commandBuffer);
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

    struct OverlayTextureState
    {
        VkImageLayout committedLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool committedInitialized = false;
        bool valid = false;
    };

    struct OverlayTextureTransition
    {
        VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        bool initializedAfter = false;
        bool validAfter = false;
    };

    struct OverlayTextureResourceBundle
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView imageView = VK_NULL_HANDLE;
        melonDS::u32 width = 0;
        melonDS::u32 height = 0;
        melonDS::u64 retireAfterTimelineValue = 0;
        melonDS::u64 retireAfterSubmissionSerial = 0;
    };

    struct OverlayUploadSlot
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize capacity = 0;

        OverlayUploadState state = OverlayUploadState::Free;
        melonDS::u64 uploadToken = 0;
        melonDS::u64 submitTimelineValue = 0;
        melonDS::u64 submitSerial = 0;
    };

    bool ensurePipeline(VkRenderPass renderPass, VkFormat format, melonDS::u64 retireAfterTimelineValue);
    void retirePipeline(melonDS::u64 retireAfterTimelineValue) noexcept;
    bool ensureUploadSlotCapacity(melonDS::u32 slotIndex, VkDeviceSize required);
    void destroyUploadSlot(melonDS::u32 slotIndex);
    bool createMappedUploadSlot(melonDS::u32 slotIndex, VkDeviceSize capacity);
    bool acquireUploadSlotForStaging();
    bool stagePendingRegion();
    OverlayTransferRecord recordPendingTransfer(VkCommandBuffer commandBuffer);
    bool createTexture(melonDS::u32 width, melonDS::u32 height);
    void destroyTexture();
    void retireCurrentTexture(melonDS::u64 retireAfterTimelineValue, melonDS::u64 retireAfterSubmissionSerial);
    void collectRetiredTextures(melonDS::u64 completedTimelineValue, melonDS::u64 completedSubmissionSerial);
    bool createTransferResources();
    void destroyTransferResources();
    void rollbackPendingTransfer(melonDS::u32 slotIndex) noexcept;
    [[nodiscard]] bool isUploadSlotCompleted(const OverlayUploadSlot& slot, melonDS::u64 completedTimelineValue) const noexcept;
    [[nodiscard]] bool isUploadSlotCompletedBySerial(
        const OverlayUploadSlot& slot,
        melonDS::u64 completedSubmissionSerial) const noexcept;

    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue transferQueue = VK_NULL_HANDLE;
    melonDS::u32 transferQueueFamilyIndex = UINT32_MAX;

    VkImage textureImage = VK_NULL_HANDLE;
    VkDeviceMemory textureMemory = VK_NULL_HANDLE;
    VkImageView textureView = VK_NULL_HANDLE;
    melonDS::u32 textureWidth = 0;
    melonDS::u32 textureHeight = 0;
    OverlayTextureState committedTexture{};
    OverlayTextureTransition pendingTextureTransition{};
    melonDS::u64 lastUploadedHudGeneration = 0;
    melonDS::u64 uploadFailureLogBudget = 0;

    static constexpr size_t kUploadSlotCount = 3;

    OverlayUploadSlot uploadSlots[kUploadSlotCount]{};
    melonDS::u32 activeUploadSlot = 0;
    melonDS::u32 nextUploadSlotIndex = 0;
    melonDS::u64 nextUploadToken = 1;
    melonDS::u64 lastPresentTimelineValue = 0;
    melonDS::u64 lastCompletedSubmissionSerial = 0;
    OverlayTransferRecord lastTransferRecord{};
    std::vector<OverlayTextureResourceBundle> retiredTextures;
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
