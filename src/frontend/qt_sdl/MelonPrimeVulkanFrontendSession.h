#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanFrontendSession requires the Vulkan build gate"
#endif

#include <array>
#include <mutex>
#include <unordered_map>

#include "types.h"
#include "VulkanReference/FrameQueue.h"
#include "VulkanReference/VulkanOutput.h"

namespace melonDS
{
class NDS;
class GPU;
class VulkanRenderer3D;
}

namespace MelonDSAndroid
{
class VulkanSurfacePresenter;
struct VulkanSurfaceOverlay;
}

struct MelonPrimeStructuredSnapshot
{
    static constexpr size_t PixelCount = 256u * 192u;
    static constexpr size_t LineCount = 192u;

    u64 generation = 0;
    u64 frameSerial = 0;
    bool screenSwap = false;
    bool complete = false;
    std::array<u32, PixelCount> topPlane0{};
    std::array<u32, PixelCount> topPlane1{};
    std::array<u32, PixelCount> topControl{};
    std::array<u32, PixelCount> topNativeFinal{};
    std::array<u32, LineCount> topLineMeta{};
    std::array<u32, LineCount> topLineState{};
    std::array<u32, PixelCount> bottomPlane0{};
    std::array<u32, PixelCount> bottomPlane1{};
    std::array<u32, PixelCount> bottomControl{};
    std::array<u32, PixelCount> bottomNativeFinal{};
    std::array<u32, LineCount> bottomLineMeta{};
    std::array<u32, LineCount> bottomLineState{};
};

class MelonPrimeVulkanFrontendSession
{
public:
    MelonPrimeVulkanFrontendSession() = default;
    ~MelonPrimeVulkanFrontendSession();

    MelonPrimeVulkanFrontendSession(const MelonPrimeVulkanFrontendSession&) = delete;
    MelonPrimeVulkanFrontendSession& operator=(const MelonPrimeVulkanFrontendSession&) = delete;

    bool initialize(melonDS::NDS& nds);
    void shutdown();
    void beginBackendSwitch();
    void completeBackendSwitch(bool vulkanPresentationActive);
    void beginGeneration(u64 generation);
    void beginSurfaceGeneration(u64 generation);

    static bool captureCompletedSnapshot(
        const melonDS::GPU& gpu,
        u64 generation,
        MelonPrimeStructuredSnapshot& snapshot);
    bool submitCompletedFrame(melonDS::VulkanRenderer3D& renderer3D);

    MelonPrimeStructuredSnapshot& producerStructuredSnapshot() { return structuredSnapshot; }

    Frame* acquirePresentFrame();
    bool presentAcquiredFrame(
        Frame* frame,
        MelonDSAndroid::VulkanSurfacePresenter& presenter,
        u64 timeoutNs);
    bool updatePresenterOverlay(
        MelonDSAndroid::VulkanSurfacePresenter& presenter,
        int surfaceId,
        const MelonDSAndroid::VulkanSurfaceOverlay& overlay);
    void commitPresentedFrame(Frame* frame);
    void deferPresentedFrame(Frame* frame);

    void registerPresenter(MelonDSAndroid::VulkanSurfacePresenter* presenter);
    void unregisterPresenter(MelonDSAndroid::VulkanSurfacePresenter* presenter);

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] bool hasCompleteStructuredSnapshot() const;
    [[nodiscard]] bool hasCompositedFrame() const;
    [[nodiscard]] bool hasRegisteredPresenter() const;
    [[nodiscard]] bool backendSwitchInProgress() const;
    [[nodiscard]] u64 generation() const;

private:
    static MelonDSAndroid::SoftPackedScreenStats collectStats(
        const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& plane0,
        const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& plane1,
        const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& control,
        const std::array<u32, MelonPrimeStructuredSnapshot::LineCount>& lineMeta);
    static void buildSoftPackedSnapshot(
        const MelonPrimeStructuredSnapshot& source,
        u64 frameId,
        MelonDSAndroid::SoftPackedFrameSnapshot& destination);
    static FrameQueuePolicy queuePolicy();
    void clearProducerState();
    void synchronizeFrameReferencesLocked();

    mutable std::mutex stateMutex;
    melonDS::NDS* nds = nullptr;
    MelonDSAndroid::VulkanOutput output;
    FrameQueue frameQueue;
    std::unordered_map<Frame*, MelonDSAndroid::VulkanCompositionInputs> frameInputs;
    MelonDSAndroid::VulkanSurfacePresenter* activePresenter = nullptr;
    MelonDSAndroid::VulkanSurfacePresenter* stagedPresenter = nullptr;
    MelonPrimeStructuredSnapshot structuredSnapshot{};
    MelonDSAndroid::SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot{};
    MelonDSAndroid::SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot{};
    bool hasCompleteStructuredSnapshot_ = false;
    u64 activeGeneration = 0;
    u64 activeSurfaceGeneration = 0;
    u64 lastSubmittedSerial = 0;
    bool initialized = false;
    bool producerSuspended = false;
};
