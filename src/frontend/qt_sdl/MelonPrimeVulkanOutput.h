#pragma once

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

// Ported from SapphireRhodonite/melonDS-android 0.7.0.rc4.
#ifndef VULKANOUTPUT_H
#define VULKANOUTPUT_H

#include <cstddef>
#include <cstdint>
#include <array>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <vulkan/vulkan.h>

#include "MelonPrimeVulkanFrameQueue.h"
#include "MelonPrimeVulkanFilterMode.h"
#include "types.h"
#include "VulkanPerfStats.h"

namespace melonDS
{
class GPU;
class VulkanRenderer3D;
struct VulkanCompletedFrameView;
}

namespace MelonPrime
{

struct SoftPackedScreenStats
{
    std::array<u32, 4> DisplayModeCounts{};
    std::array<u32, 8> CompModeCounts{};
    int MinXOffset = 0;
    int MaxXOffset = 0;
    bool HasOffsets = false;
    u32 CaptureBackedComp4Pixels = 0;
    u32 CaptureBackedComp4Lines = 0;
    u32 RegularCaptureUses3dLines = 0;
    u32 VramCaptureUses3dLines = 0;
    u32 ForceLive3dCompMode7Lines = 0;
    u32 StructuredSlotPixels = 0;
    u32 StructuredAbovePixels = 0;
    u32 Structured2DOnlyPixels = 0;
    u32 Plane0UsefulPixels = 0;
    u32 Plane0VisiblePixels = 0;
    u32 Plane0OpaqueBlackPixels = 0;
    u32 Plane1UsefulPixels = 0;
    u32 Plane1VisiblePixels = 0;
    u32 Plane1OpaqueBlackPixels = 0;
    u32 StructuredAboveVisiblePixels = 0;
    u32 StructuredAboveBlackPixels = 0;
    u32 Structured2DOnlyVisiblePixels = 0;
    u32 ProtectedBlackPixels = 0;
};

struct SoftPackedFrameSnapshot
{
    static constexpr size_t kScreenWidth = 256u;
    static constexpr size_t kScreenHeight = 192u;
    static constexpr size_t kPixelCount = kScreenWidth * kScreenHeight;
    static constexpr size_t kLineCount = kScreenHeight;

    u64 frameId = 0;
    u64 sourceGeneration = 0;
    u64 renderer3dRenderSerial = 0;
    u64 renderer3dCompletionValue = 0;
    u32 renderer3dImageSlot = 0;
    bool renderer3dReferenceValid = false;
    int frontBufferLatched = -1;
    bool screenSwapLatched = false;
    bool valid = false;
    bool hasCapture3dSource = false;
    bool captureScreenSwap = false;
    bool captureScreenSwapValid = false;
    bool captureBackedClass4Only = false;
    std::array<u32, kPixelCount> packedTopPlane0{};
    std::array<u32, kPixelCount> packedTopPlane1{};
    std::array<u32, kPixelCount> packedTopControl{};
    std::array<u32, kLineCount> packedTopLineMeta{};
    std::array<u32, kPixelCount> packedBottomPlane0{};
    std::array<u32, kPixelCount> packedBottomPlane1{};
    std::array<u32, kPixelCount> packedBottomControl{};
    std::array<u32, kLineCount> packedBottomLineMeta{};
    std::array<u32, kPixelCount> capture3dSourceDsFrame{};
    std::array<u8, kLineCount> capture3dSourceLineValidMask{};
    std::array<u8, kLineCount> topScreenNeedsCapture3dMask{};
    std::array<u8, kLineCount> bottomScreenNeedsCapture3dMask{};
    std::array<u8, kLineCount> captureFallbackLines{};
    std::array<u32, kPixelCount> comp4TopPlaceholder{};
    std::array<u32, kPixelCount> comp4BottomPlaceholder{};
    SoftPackedScreenStats topScreenStats{};
    SoftPackedScreenStats bottomScreenStats{};

    void clear()
    {
        frameId = 0;
        sourceGeneration = 0;
        renderer3dRenderSerial = 0;
        renderer3dCompletionValue = 0;
        renderer3dImageSlot = 0;
        renderer3dReferenceValid = false;
        frontBufferLatched = -1;
        screenSwapLatched = false;
        valid = false;
        hasCapture3dSource = false;
        captureScreenSwap = false;
        captureScreenSwapValid = false;
        captureBackedClass4Only = false;
        packedTopPlane0.fill(0);
        packedTopPlane1.fill(0);
        packedTopControl.fill(0);
        packedTopLineMeta.fill(0);
        packedBottomPlane0.fill(0);
        packedBottomPlane1.fill(0);
        packedBottomControl.fill(0);
        packedBottomLineMeta.fill(0);
        capture3dSourceDsFrame.fill(0);
        capture3dSourceLineValidMask.fill(0);
        topScreenNeedsCapture3dMask.fill(0);
        bottomScreenNeedsCapture3dMask.fill(0);
        captureFallbackLines.fill(0);
        comp4TopPlaceholder.fill(0);
        comp4BottomPlaceholder.fill(0);
        topScreenStats = {};
        bottomScreenStats = {};
    }
};

struct PreparedSoftPackedFrameDebugView
{
    u64 frameId = 0;
    u64 sourceGeneration = 0;
    u64 renderer3dRenderSerial = 0;
    u64 renderer3dSnapshotSerial = 0;
    int frontBufferLatched = -1;
    bool screenSwapLatched = false;
    bool captureScreenSwap = false;
    bool captureScreenSwapValid = false;
    bool captureBackedClass4Only = false;
    const u32* capture3dSourceDsFrame = nullptr;
    const u8* capture3dSourceLineValidMask = nullptr;
    const u8* topScreenNeedsCapture3dMask = nullptr;
    const u8* bottomScreenNeedsCapture3dMask = nullptr;
    const u8* captureFallbackLines = nullptr;
    const u32* comp4TopPlaceholder = nullptr;
    const u32* comp4BottomPlaceholder = nullptr;
    SoftPackedScreenStats topScreenStats{};
    SoftPackedScreenStats bottomScreenStats{};
    bool valid = false;
};

struct VulkanCompositionInputs
{
    VkImage sourceImage{VK_NULL_HANDLE};
    VkImageView sourceImageView{VK_NULL_HANDLE};
    VkImage previousTopSourceImage{VK_NULL_HANDLE};
    VkImageView previousTopSourceImageView{VK_NULL_HANDLE};
    VkImage previousBottomSourceImage{VK_NULL_HANDLE};
    VkImageView previousBottomSourceImageView{VK_NULL_HANDLE};
    VkBuffer topPackedBuffer{VK_NULL_HANDLE};
    VkBuffer bottomPackedBuffer{VK_NULL_HANDLE};
    VkBuffer capture3dBuffer{VK_NULL_HANDLE};
    VkDeviceSize packedBufferSize{};
    VkDeviceSize capture3dBufferSize{};
    u32 packedStride{};
    u32 screenSwap{};
    u32 scale{};
    u32 rendererWidth{};
    u32 rendererHeight{};
    VulkanFilterMode filtering{VulkanFilterMode::Nearest};
    bool previousTopSourceValid{};
    bool previousBottomSourceValid{};
    bool capture3dSourceValid{};
    bool capture3dSourceScreenSwapValid{};
    bool capture3dSourceScreenSwap{};
    bool compositionCurrentSourceScreenSwap{};
    bool class4VramStructuredPair{};
    bool class4NoAboveVramStructuredPair{};
    bool class4PreservePackedVramValid{};
    bool class4PreservePackedVramScreenSwap{};
    bool topStructuredHandoffNoCurrent3d{};
    bool bottomStructuredHandoffNoCurrent3d{};
    bool topStructuredHandoffSuppress3d{};
    bool bottomStructuredHandoffSuppress3d{};
    bool needsReadback{};
    bool multiSurface{};
    bool validationMode{};
};

struct MelonPrimeVulkanOutputTemporalStats
{
    u64 FramesPrepared = 0;
    u64 FramesWithCapture3dSource = 0;
    u64 TopNeedsHighres = 0;
    u64 BottomNeedsHighres = 0;
    u64 TopPreviousSourceValid = 0;
    u64 BottomPreviousSourceValid = 0;
    u64 TopMissingHighresSource = 0;
    u64 BottomMissingHighresSource = 0;
    u64 TopStructuredSlot = 0;
    u64 BottomStructuredSlot = 0;
    u64 TopStructuredMissingAccumulator = 0;
    u64 BottomStructuredMissingAccumulator = 0;
    u64 TopAccumulatorAvailable = 0;
    u64 BottomAccumulatorAvailable = 0;
    u64 TopRegularCapture = 0;
    u64 BottomRegularCapture = 0;
    u64 TopVramCapture = 0;
    u64 BottomVramCapture = 0;
    u64 TopForceLiveCompMode7 = 0;
    u64 BottomForceLiveCompMode7 = 0;
    u64 TopCaptureBackedComp4 = 0;
    u64 BottomCaptureBackedComp4 = 0;
    u64 PackedTopOwner = 0;
    u64 PackedBottomOwner = 0;
    u64 LiveTopOwner = 0;
    u64 LiveBottomOwner = 0;
    u64 LiveOwnerOverride = 0;
    u64 SnapshotFrames = 0;
    u64 SnapshotTopOwner = 0;
    u64 SnapshotBottomOwner = 0;
    u64 SnapshotOwnerDiffersFromLive = 0;
    u64 TopPlane0UsefulPixels = 0;
    u64 TopPlane0VisiblePixels = 0;
    u64 TopPlane0OpaqueBlackPixels = 0;
    u64 TopPlane1UsefulPixels = 0;
    u64 TopPlane1VisiblePixels = 0;
    u64 TopPlane1OpaqueBlackPixels = 0;
    u64 TopStructuredAboveVisiblePixels = 0;
    u64 TopStructuredAboveBlackPixels = 0;
    u64 TopStructured2DOnlyVisiblePixels = 0;
    u64 TopProtectedBlackPixels = 0;
    u64 BottomPlane0UsefulPixels = 0;
    u64 BottomPlane0VisiblePixels = 0;
    u64 BottomPlane0OpaqueBlackPixels = 0;
    u64 BottomPlane1UsefulPixels = 0;
    u64 BottomPlane1VisiblePixels = 0;
    u64 BottomPlane1OpaqueBlackPixels = 0;
    u64 BottomStructuredAboveVisiblePixels = 0;
    u64 BottomStructuredAboveBlackPixels = 0;
    u64 BottomStructured2DOnlyVisiblePixels = 0;
    u64 BottomProtectedBlackPixels = 0;
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
    void invalidateTemporalHistory();
    void clearStructuredCaptureHistory();
    void releaseTemporalFrameReferences();
    bool prepareFrameForPresentation(
        VulkanFrame* frame,
        const melonDS::GPU& gpu,
        int frontBuffer,
        bool frameScreenSwap,
        SoftPackedFrameSnapshot& softPackedSnapshot,
        melonDS::VulkanRenderer3D& renderer3D,
        const melonDS::VulkanCompletedFrameView& completed3DView);
    bool composeAndSubmitFrame(VulkanFrame* frame, const VulkanCompositionInputs& inputs);
    bool buildCompositionInputs(
        const VulkanFrame* frame,
        const melonDS::VulkanRenderer3D& renderer3D,
        int scale,
        VulkanFilterMode filtering,
        bool needsReadback,
        bool multiSurface,
        bool validationMode,
        VulkanCompositionInputs& outInputs) const;
    bool validateFrameSubmission(VulkanFrame* frame, u64 waitTimeoutNs = UINT64_MAX);
    bool validateCompositorSubmission(VulkanFrame* frame, const melonDS::VulkanRenderer3D& renderer3D, int scale, u64 waitTimeoutNs = UINT64_MAX);
    bool validateRuntimePath(u32 width, u32 height, const melonDS::VulkanRenderer3D& renderer3D, int scale);
    bool isFrameReady(const VulkanFrame* frame) const;
    bool waitForFrame(const VulkanFrame* frame, u64 timeoutNs);
    bool isFrameReferencedAsPendingPreviousSource(const VulkanFrame* frame) const;
    bool readFramePixels(const VulkanFrame* frame, u32* destinationPixels, size_t destinationPixelCount, u64 waitTimeoutNs = UINT64_MAX);
    bool readPreparedRenderer3dPixels(
        const VulkanFrame* frame,
        u32* destinationPixels,
        size_t destinationPixelCount,
        u32& outWidth,
        u32& outHeight,
        u64 waitTimeoutNs = UINT64_MAX);
    bool getPreparedRenderer3dCaptureFrame(
        const VulkanFrame* frame,
        const u32*& outPixels,
        u32& outWidth,
        u32& outHeight) const;
    bool getPreparedRenderer3dDimensions(const VulkanFrame* frame, u32& outWidth, u32& outHeight) const;
    bool getPreparedPackedBuffers(
        const VulkanFrame* frame,
        const u32*& outTopPacked,
        const u32*& outBottomPacked,
        u32& outPackedStride,
        u32& outPackedHeight,
        bool& outScreenSwap) const;
    bool getPreparedSoftPackedFrameDebugView(
        const VulkanFrame* frame,
        PreparedSoftPackedFrameDebugView& outView) const;
    [[nodiscard]] VkImage getFrameImage(const VulkanFrame* frame) const;
    [[nodiscard]] VkImageView getFrameImageView(const VulkanFrame* frame) const;
    MelonPrimeVulkanOutputTemporalStats takeTemporalStatsSnapshotAndReset();

private:
    static constexpr size_t kPackedScreenWordCount =
        SoftPackedFrameSnapshot::kLineCount
        * ((SoftPackedFrameSnapshot::kScreenWidth * 3u) + 1u);

    struct CompositorPushConstants
    {
        u32 outputWidth;
        u32 outputHeight;
        u32 scale;
        u32 rendererWidth;
        u32 rendererHeight;
        u32 packedStride;
        u32 screenSwap;
        u32 filtering;
        u32 previousTopSourceValid;
        u32 previousBottomSourceValid;
        u32 captureSourceValid;
        u32 captureSourceScreenSwapValid;
        u32 captureSourceScreenSwap;
        u32 liveSourceScreenSwap;
        u32 class4VramStructuredPair;
        u32 class4NoAboveVramStructuredPair;
        u32 class4PreservePackedVramValid;
        u32 class4PreservePackedVramScreenSwap;
        u32 topStructuredHandoffNoCurrent3d;
        u32 bottomStructuredHandoffNoCurrent3d;
        u32 topStructuredHandoffSuppress3d;
        u32 bottomStructuredHandoffSuppress3d;
    };

    struct AccumulatePushConstants
    {
        u32 scale;
        u32 packedStride;
        u32 topLcd;
    };

    struct FrameResource
    {
        VkImage image{VK_NULL_HANDLE};
        VkImageView imageView{VK_NULL_HANDLE};
        VkDeviceMemory imageMemory{VK_NULL_HANDLE};

        VkBuffer stagingBuffer{VK_NULL_HANDLE};
        VkDeviceMemory stagingMemory{VK_NULL_HANDLE};
        VkDeviceSize stagingSize{};

        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkFence submitFence{VK_NULL_HANDLE};
        VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
        VkQueryPool timestampQueryPool{VK_NULL_HANDLE};
        VkBuffer topPackedBuffer{VK_NULL_HANDLE};
        VkDeviceMemory topPackedMemory{VK_NULL_HANDLE};
        void* topPackedMapped{};
        VkBuffer bottomPackedBuffer{VK_NULL_HANDLE};
        VkDeviceMemory bottomPackedMemory{VK_NULL_HANDLE};
        void* bottomPackedMapped{};
        VkBuffer capture3dBuffer{VK_NULL_HANDLE};
        VkDeviceMemory capture3dMemory{VK_NULL_HANDLE};
        void* capture3dMapped{};
        VkDeviceSize packedBufferSize{};
        VkImage renderer3dSnapshot{VK_NULL_HANDLE};
        VkImageView renderer3dSnapshotView{VK_NULL_HANDLE};
        VkDeviceMemory renderer3dSnapshotMemory{VK_NULL_HANDLE};
        u32 snapshotWidth{};
        u32 snapshotHeight{};
        VkImage previousTopRendererSourceImage{VK_NULL_HANDLE};
        VkImageView previousTopRendererSourceImageView{VK_NULL_HANDLE};
        bool previousTopRendererSourceValid{};
        VulkanFrame* previousTopSourceFrame{};
        bool previousTopSourcePending{};
        VkImage previousBottomRendererSourceImage{VK_NULL_HANDLE};
        VkImageView previousBottomRendererSourceImageView{VK_NULL_HANDLE};
        bool previousBottomRendererSourceValid{};
        VulkanFrame* previousBottomSourceFrame{};
        bool previousBottomSourcePending{};
        u64 softPackedFrameId{};
        u64 structuredGeneration{};
        u64 renderer3dRenderSerial{};
        u64 renderer3dSnapshotSerial{};
        u64 renderer3dCompletionValue{};
        u32 renderer3dImageSlot{};
        int frontBufferLatched{-1};
        bool captureScreenSwap{};
        bool captureScreenSwapValid{};
        bool captureBackedClass4Only{};
        bool class4NoAboveVramStructuredPair{};
        bool class4PreservePackedVramValid{};
        bool class4PreservePackedVramScreenSwap{};
        bool topStructuredHandoffNoCurrent3d{};
        bool bottomStructuredHandoffNoCurrent3d{};
        bool topStructuredHandoffSuppress3d{};
        bool bottomStructuredHandoffSuppress3d{};
        bool topPackedCarryFromPrevious{};
        bool bottomPackedCarryFromPrevious{};
        bool topPureAlternatingVramCapture{};
        bool bottomPureAlternatingVramCapture{};
        bool hasSoftPackedDebugData{};
        SoftPackedScreenStats topScreenStats{};
        SoftPackedScreenStats bottomScreenStats{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> capture3dSourceDsFrame{};
        std::array<u8, SoftPackedFrameSnapshot::kLineCount> capture3dSourceLineValidMask{};
        std::array<u8, SoftPackedFrameSnapshot::kLineCount> topScreenNeedsCapture3dMask{};
        std::array<u8, SoftPackedFrameSnapshot::kLineCount> bottomScreenNeedsCapture3dMask{};
        std::array<u8, SoftPackedFrameSnapshot::kLineCount> captureFallbackLines{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> comp4TopPlaceholder{};
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> comp4BottomPlaceholder{};

        u64 submissionValue{};
        u32 width{};
        u32 height{};
        bool screenSwap{};
        bool screenSwapToggledFromPrevious{};
        bool hasContent{};
        bool hasPreparedInputs{};
        bool replayTopComposedFromPrevious{};
        bool replayBottomComposedFromPrevious{};
        bool replayTopComposedFromLatest{};
        VulkanFrame* previousTopComposedFrame{};
        VulkanFrame* previousBottomComposedFrame{};
        bool hasRenderer3dSnapshot{};
        bool renderer3dSnapshotScreenSwap{};
        bool exactRenderer3dPairValid{};
        bool hasPreparedCapture3dSource{};
        bool snapshotFromPreRun{};
        bool snapshotFromInitializedTarget{};
        bool snapshotFromGraphicsBackend{};
        bool descriptorSetReady{};
        bool timestampPending{};
        VkImageView cachedRendererImageView{VK_NULL_HANDLE};
        VkImageView cachedPreviousTopRendererImageView{VK_NULL_HANDLE};
        VkImageView cachedPreviousBottomRendererImageView{VK_NULL_HANDLE};
        std::array<u32, 256 * 192> preparedCapture3dSource{};
    };

    struct CaptureSourceHistory
    {
        std::array<u32, SoftPackedFrameSnapshot::kPixelCount> pixels{};
        std::array<u8, SoftPackedFrameSnapshot::kLineCount> validLines{};
        u64 structuredGeneration{};
        u64 rendererSerial{};
        bool valid{};

        void clear() noexcept
        {
            pixels.fill(0);
            validLines.fill(0);
            structuredGeneration = 0;
            rendererSerial = 0;
            valid = false;
        }
    };

private:
    bool createSyncObjects();
    bool createCommandObjects();
    bool createCompositorResources();
    bool createTimestampQueryPool(VkQueryPool& queryPool);
    void destroyTimestampQueryPool(VkQueryPool& queryPool);
    void destroyCompositorResources();
    bool createFrameResource(VulkanFrame* frame, u32 width, u32 height);
    void destroyFrameResource(VulkanFrame* frame);
    void destroyFrameResources();
    u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;

    bool beginFrameCommand(FrameResource& resource, u64 waitTimeoutNs = UINT64_MAX);
    bool submitFrameCommand(VulkanFrame* frame, FrameResource& resource, bool signalTimeline);
    bool updateCompositorPackedBuffers(VulkanFrame* frame, FrameResource& resource, const SoftPackedFrameSnapshot& softPackedSnapshot);
    bool updatePreparedCapture3dSource(
        FrameResource& resource,
        SoftPackedFrameSnapshot& softPackedSnapshot,
        const FrameResource* previousResource,
        bool currentBackendIsGraphics,
        bool currentFrameNeedsCapture3dSource,
        melonDS::VulkanRenderer3D& renderer3D);
    bool ensureRenderer3dSnapshot(FrameResource& resource, u32 width, u32 height);
    void destroyRenderer3dSnapshot(FrameResource& resource);
    bool recordRenderer3dSnapshotCopy(
        FrameResource& resource,
        const melonDS::VulkanCompletedFrameView& completed3DView,
        bool rendererTargetScreenSwap);

    bool createAccumulateResources();
    void destroyAccumulateResources();
    bool ensureAccumulatedHighresImages(u32 width, u32 height);
    void destroyAccumulatedHighresImage(VkImage& image, VkImageView& view, VkDeviceMemory& memory, bool& valid, bool& layoutReady);
    bool recordAccumulateMerge(FrameResource& resource, bool topLcd, bool replaceExisting);
    bool recordDirectPresentationPrep(
        VulkanFrame* frame,
        FrameResource& resource,
        const melonDS::VulkanRenderer3D& renderer3D,
        const melonDS::VulkanCompletedFrameView& completed3DView,
        bool rendererTargetScreenSwap,
        bool accumulateTopHighres,
        bool accumulateBottomHighres,
        bool replaceAccumulatedHighres);
    bool dispatchCompositor(VulkanFrame* frame, FrameResource& resource, const VulkanCompositionInputs& inputs);
    void recordTemporalStats(
        const SoftPackedFrameSnapshot& softPackedSnapshot,
        const FrameResource& resource,
        bool topNeedsAccumulatedHighres,
        bool bottomNeedsAccumulatedHighres,
        bool topAccumulatorAvailable,
        bool bottomAccumulatorAvailable,
        bool packedScreenSwap,
        bool compositionCurrentSourceScreenSwap,
        bool hasRenderer3dSnapshot,
        bool renderer3dSnapshotScreenSwap);
    void consumeFrameGpuTiming(FrameResource& resource);
    void logPerformanceIfNeeded();
    bool readResourceImagePixels(
        FrameResource& resource,
        const VulkanFrame* frame,
        VkImage image,
        u32 width,
        u32 height,
        u32* destinationPixels,
        size_t destinationPixelCount,
        u64 waitTimeoutNs);

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

    VkImage accumulatedTopHighresImage{VK_NULL_HANDLE};
    VkImageView accumulatedTopHighresView{VK_NULL_HANDLE};
    VkDeviceMemory accumulatedTopHighresMemory{VK_NULL_HANDLE};
    bool accumulatedTopHighresValid{false};
    bool accumulatedTopHighresLayoutReady{false};
    bool accumulatedTopOwnerValid{false};
    bool accumulatedTopOwnerScreenSwap{true};
    u64 accumulatedTopStructuredGeneration{};
    u64 accumulatedTopRendererSerial{};
    VkImage accumulatedBottomHighresImage{VK_NULL_HANDLE};
    VkImageView accumulatedBottomHighresView{VK_NULL_HANDLE};
    VkDeviceMemory accumulatedBottomHighresMemory{VK_NULL_HANDLE};
    bool accumulatedBottomHighresValid{false};
    bool accumulatedBottomHighresLayoutReady{false};
    bool accumulatedBottomOwnerValid{false};
    bool accumulatedBottomOwnerScreenSwap{false};
    u64 accumulatedBottomStructuredGeneration{};
    u64 accumulatedBottomRendererSerial{};
    u32 accumulatedHighresWidth{0};
    u32 accumulatedHighresHeight{0};

    VkDescriptorSetLayout accumulateDescriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool accumulateDescriptorPool{VK_NULL_HANDLE};
    VkPipelineLayout accumulatePipelineLayout{VK_NULL_HANDLE};
    VkPipeline accumulatePipeline{VK_NULL_HANDLE};
    VkDescriptorSet accumulateTopDescriptorSet{VK_NULL_HANDLE};
    VkDescriptorSet accumulateBottomDescriptorSet{VK_NULL_HANDLE};
    bool accumulateTopDescriptorReady{false};
    bool accumulateBottomDescriptorReady{false};
    VkImageView cachedAccumulateTopSourceView{VK_NULL_HANDLE};
    VkImageView cachedAccumulateBottomSourceView{VK_NULL_HANDLE};

    std::unordered_map<VulkanFrame*, FrameResource> resources;
    std::mutex commandPoolLock;
    VulkanFrame* lastPreparedFrame{nullptr};
    VulkanFrame* lastTopRendererSourceFrame{nullptr};
    VulkanFrame* lastBottomRendererSourceFrame{nullptr};
    VulkanFrame* lastTopComposedFrame{nullptr};
    VulkanFrame* lastBottomComposedFrame{nullptr};
    std::vector<u32> lastValidTopPacked;
    std::vector<u32> lastValidBottomPacked;
    bool lastValidTopPackedAvailable{false};
    bool lastValidBottomPackedAvailable{false};
    bool lastPackedScreenSwapValid{false};
    bool lastPackedScreenSwap{false};
    u32 framesSinceTopLive3D{1024};
    u32 framesSinceBottomLive3D{1024};
    bool class4AsymmetricCadenceActive{};
    u32 class4AsymmetricCadencePhase{};
    bool class4BottomAboveHashValid{};
    u64 class4BottomAboveHash{};
    u32 class4BottomAboveStableFrames{};
    bool class4BottomAboveMotionActive{};
    bool class4NoAboveVramStructuredActive{};
    std::array<CaptureSourceHistory, 2> captureHistoryByOwner{};
    std::array<CaptureSourceHistory, 2> topComp4HistoryByOwner{};
    std::array<CaptureSourceHistory, 2> bottomComp4HistoryByOwner{};
    u32 packedDebugLogsRemaining{};
    u32 class4PairDebugLogsRemaining{};
    u32 regularComp7PackedOwnerDebugLogsRemaining{};
    u32 structuredComp7HandoffDebugLogsRemaining{};
    u32 ownershipIntroDebugLogsRemaining{};
    bool regularComp7PackedOwnerDebugActive{};
    std::mutex temporalStatsLock;
    MelonPrimeVulkanOutputTemporalStats temporalStats{};
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

#endif // VULKANOUTPUT_H
#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
