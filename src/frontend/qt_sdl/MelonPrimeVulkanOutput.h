#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#ifndef MELONPRIME_VULKAN_OUTPUT_H
#define MELONPRIME_VULKAN_OUTPUT_H

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vulkan/vulkan.h>

#include "MelonPrimeVulkanFrameQueue.h"
#include "types.h"
#include "VulkanPerfStats.h"

namespace melonDS
{
class VulkanRenderer3D;
}

namespace MelonPrime
{

// One emulated frame of the structured 2D composition contract, as published by
// melonDS::SoftRenderer::GetStructuredVulkanFrame(). See
// src/MelonPrimeStructuredComposition.h for the bit layout.
//
// Everything the compositor needs is here, and nothing else is allowed in: the
// composed image is a pure function of a single emulated frame. There is no
// screen-swap input because the producer already assigned engines to LCDs, and
// no previous-frame or scene-statistic input because the per-pixel control word
// already states where the 3D layer belongs.
struct StructuredCompositionFrame
{
    static constexpr std::size_t kScreenWidth = 256;
    static constexpr std::size_t kScreenHeight = 192;
    static constexpr std::size_t kPixelCount = kScreenWidth * kScreenHeight;
    static constexpr std::size_t kLineCount = kScreenHeight;

    // [screen][plane]; screen 0 is the top LCD, plane order is below/above/control.
    const u32* Plane[2][3]{};
    // [screen]; one metadata word per scanline.
    const u32* LineMeta[2]{};
    // False when the 3D frame was aborted, which is the DX12 compositor's
    // equivalent gate. The 3D layer then contributes nothing.
    bool Has3D = false;
    u64 Generation = 0;

    [[nodiscard]] bool IsComplete() const noexcept
    {
        for (std::size_t screen = 0; screen < 2u; ++screen)
        {
            if (LineMeta[screen] == nullptr)
                return false;
            for (std::size_t plane = 0; plane < 3u; ++plane)
            {
                if (Plane[screen][plane] == nullptr)
                    return false;
            }
        }
        return true;
    }
};

// Resolved GPU handles and scalars for one compositor dispatch.
struct VulkanCompositionInputs
{
    VkImage sourceImage{VK_NULL_HANDLE};
    VkImageView sourceImageView{VK_NULL_HANDLE};
    VkBuffer topPackedBuffer{VK_NULL_HANDLE};
    VkBuffer bottomPackedBuffer{VK_NULL_HANDLE};
    VkDeviceSize packedBufferSize{};
    u32 packedStride{};
    u32 scale{};
    u32 rendererWidth{};
    u32 rendererHeight{};
    bool has3D{};
};

class MelonPrimeVulkanOutput
{
public:
    MelonPrimeVulkanOutput();
    ~MelonPrimeVulkanOutput();

    MelonPrimeVulkanOutput(const MelonPrimeVulkanOutput&) = delete;
    MelonPrimeVulkanOutput& operator=(const MelonPrimeVulkanOutput&) = delete;

    bool init();
    void shutdown();
    [[nodiscard]] bool isInitialized() const { return initialized; }

    bool ensureFrameResources(VulkanFrame* frame, u32 width, u32 height);

    // Drops every cross-frame reference. The composed pixels never depend on
    // frame history, so this exists only for resource lifetime: a renderer
    // transition or a savestate can retire the images a queued frame points at.
    void releaseFrameReferences();

    // Copies this frame's structured planes into the frame's mapped packed
    // buffers. Nothing is retained between frames.
    bool prepareFrameForPresentation(
        VulkanFrame* frame,
        const StructuredCompositionFrame& structured);
    bool buildCompositionInputs(
        const VulkanFrame* frame,
        const melonDS::VulkanRenderer3D& renderer3D,
        int scale,
        bool has3D,
        VulkanCompositionInputs& outInputs) const;
    bool composeAndSubmitFrame(VulkanFrame* frame, const VulkanCompositionInputs& inputs);

    bool waitForFrame(const VulkanFrame* frame, u64 timeoutNs);
    [[nodiscard]] VkImage getFrameImage(const VulkanFrame* frame) const;
    [[nodiscard]] VkImageView getFrameImageView(const VulkanFrame* frame) const;

private:
    // Mirrors MelonPrimeVulkanCompositorShader.comp. Keep the order identical.
    struct CompositorPushConstants
    {
        u32 outputWidth;
        u32 outputHeight;
        u32 scale;
        u32 rendererWidth;
        u32 rendererHeight;
        u32 packedStride;
        u32 has3D;
    };

    struct FrameResource
    {
        // Composition output. Produced by the compute dispatch in
        // VK_IMAGE_LAYOUT_GENERAL, consumed by the surface presenter.
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkDeviceMemory imageMemory{VK_NULL_HANDLE};

        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence submitFence{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkQueryPool timestampQueryPool{VK_NULL_HANDLE};

        // Persistently mapped structured planes, written once per emulated
        // frame by the emulation thread and read by the compute dispatch.
        VkBuffer topPackedBuffer{VK_NULL_HANDLE};
        VkDeviceMemory topPackedMemory{VK_NULL_HANDLE};
        void* topPackedMapped{};
        VkBuffer bottomPackedBuffer{VK_NULL_HANDLE};
        VkDeviceMemory bottomPackedMemory{VK_NULL_HANDLE};
        void* bottomPackedMapped{};
        VkDeviceSize packedBufferSize{};

        u64 submissionValue{};
        u32 width{};
        u32 height{};
        bool hasContent{};
        bool hasPreparedInputs{};
        bool descriptorSetReady{};
        bool timestampPending{};
        VkImageView cachedRendererImageView{VK_NULL_HANDLE};
    };

    bool createSyncObjects();
    bool createCommandObjects();
    bool createCompositorResources();
    void destroyCompositorResources();
    bool createTimestampQueryPool(VkQueryPool& queryPool);
    void destroyTimestampQueryPool(VkQueryPool& queryPool);
    bool createFrameResource(VulkanFrame* frame, u32 width, u32 height);
    void destroyFrameResource(VulkanFrame* frame);
    void destroyFrameResources();
    u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;

    bool beginFrameCommand(FrameResource& resource, u64 waitTimeoutNs = UINT64_MAX);
    bool submitFrameCommand(VulkanFrame* frame, FrameResource& resource, bool signalTimeline);
    bool updateCompositorPackedBuffers(
        FrameResource& resource,
        const StructuredCompositionFrame& structured);
    bool dispatchCompositor(
        VulkanFrame* frame,
        FrameResource& resource,
        const VulkanCompositionInputs& inputs);
    void consumeFrameGpuTiming(FrameResource& resource);
    void logPerformanceIfNeeded();

private:
    bool initialized{};
    bool contextAcquired{};

    VkInstance instance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue queue{VK_NULL_HANDLE};
    u32 queueFamilyIndex{};

    VkCommandPool commandPool{VK_NULL_HANDLE};

    VkSemaphore timelineSemaphore{VK_NULL_HANDLE};
    u64 timelineValue{};
    bool useTimelineSemaphores{};

    PFN_vkWaitSemaphoresKHR waitSemaphores{};
    PFN_vkGetSemaphoreCounterValueKHR getSemaphoreCounterValue{};
    PFN_vkResetQueryPoolEXT resetQueryPool{};
    float timestampPeriodNs{};
    bool timestampQueriesSupported{};

    VkDescriptorSetLayout compositorDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool compositorDescriptorPool{VK_NULL_HANDLE};
    VkPipelineLayout compositorPipelineLayout{VK_NULL_HANDLE};
    VkPipeline compositorPipeline{VK_NULL_HANDLE};

    std::unordered_map<VulkanFrame*, FrameResource> resources;
    std::mutex commandPoolLock;

    PerfSampleWindow<120> packedUploadCpuWindow;
    PerfSampleWindow<120> composeCpuWindow;
    PerfSampleWindow<120> waitCpuWindow;
    PerfSampleWindow<120> compositorGpuWindow;
    u64 waitFailureInvalidFrame = 0;
    u64 waitFailureTimelineZero = 0;
    u64 waitFailureResourceMissing = 0;
    u64 waitFailureFiniteTimeout = 0;
    u64 waitFailureInfinite = 0;
};

}

#endif // MELONPRIME_VULKAN_OUTPUT_H
#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
