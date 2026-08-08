#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#ifndef MELONPRIME_VULKAN_OUTPUT_H
#define MELONPRIME_VULKAN_OUTPUT_H

#include <cassert>
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
    // The emulated frame these inputs describe. Everything downstream is
    // checked against it so a dispatch can never mix two frames' 2D and 3D.
    u64 generation{};
    // The 3D renderer's submission serial at the moment these inputs were
    // built, and how many times its color target had been taken back for a new
    // 3D frame. Recorded so the 3D image can be tied to a specific render
    // submission rather than "whatever is in the color target right now".
    // These are for tracing only: the actual ordering comes from the pipeline
    // barriers, never from comparing serials.
    u64 rendererSubmissionSerial{};
    u64 colorImageReuseSerial{};
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
        u64 generation,
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

    // Who may touch a frame's resources right now.
    //
    // Idle       the GPU is not reading this frame's inputs; the emulation
    //            thread may rewrite the shared structured planes for it.
    // Recording  its command buffer is open; nothing may be rewritten.
    // Submitted  its dispatch may still be executing; the packed planes, the
    //            output image and the bound descriptors must all stay put.
    enum class SubmissionState : u8
    {
        Idle,
        Recording,
        Submitted,
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

        // This frame's own structured planes, persistently mapped.
        //
        // They are per-frame precisely so that submitFence is a complete
        // statement about when they may be rewritten. A single shared pair
        // cannot work here: acquireFrameForCpuWrite() waits on the fence of the
        // frame being acquired, so with one shared buffer a dispatch still
        // reading it from a different slot was never waited for, and the
        // emulation thread could overwrite below/above/control/lineMeta
        // underneath it. One dispatch then saw a mix of two generations, which
        // is what put the background and 3D in front of the UI on alternating
        // frames. DX12 gets away with one composition input buffer only because
        // it serializes each composition with Commands.WaitIdle().
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

        SubmissionState submissionState{SubmissionState::Idle};
        // Set true only between acquireFrameForCpuWrite() and the submission
        // that consumes the write. Asserted wherever the planes are written.
        bool cpuWriteOwnership{};
        // The emulated frame this resource's inputs, submission and completed
        // dispatch belong to. They must agree at every step.
        u64 preparedGeneration{};
        u64 submittedGeneration{};
        u64 completedGeneration{};
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

    // Waits for any dispatch still reading the shared structured planes, then
    // hands the emulation thread the right to rewrite them.
    bool acquireFrameForCpuWrite(VulkanFrame* frame, u64 timeoutNs = UINT64_MAX);
    bool waitForResourceSubmission(FrameResource& resource, u64 timeoutNs);
    // Returns a resource to Idle after recording or submission failed, so a
    // later reuse cannot block forever on a fence that will never signal.
    bool recoverFrameResourceAfterAbortedSubmission(FrameResource& resource);
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
    void logPackedFrameIfNeeded(
        const VulkanFrame* frame,
        const FrameResource& resource,
        const VulkanCompositionInputs& inputs) const;
    void logFrameSyncIfNeeded(const VulkanFrame* frame, const VulkanCompositionInputs& inputs);
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

    // Counts compositor dispatches, so a log line can say which composition a
    // given structured generation and 3D submission ended up in.
    u64 compositorSubmissionSerial{};
    u64 lastLoggedColorImageReuseSerial{};

    std::unordered_map<VulkanFrame*, FrameResource> resources;
    std::mutex commandPoolLock;

    PerfSampleWindow<120> packedUploadCpuWindow;
    PerfSampleWindow<120> composeCpuWindow;
    PerfSampleWindow<120> waitCpuWindow;
    PerfSampleWindow<120> compositorGpuWindow;
    // Ownership violations. All must stay at zero; they are surfaced in the
    // developer performance log rather than being silently tolerated.
    u64 packedWriteWhileSubmitted = 0;
    u64 generationMismatch = 0;
    // Composition inputs naming buffers other than the dispatching frame's own.
    u64 packedBufferIdentityMismatch = 0;
    u64 fenceRecoveryFailure = 0;
    u64 staleTimelinePresented = 0;

    u64 waitFailureInvalidFrame = 0;
    u64 waitFailureTimelineZero = 0;
    u64 waitFailureResourceMissing = 0;
    u64 waitFailureFiniteTimeout = 0;
    u64 waitFailureInfinite = 0;
};

}

#endif // MELONPRIME_VULKAN_OUTPUT_H
#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
