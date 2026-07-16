#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "MelonPrimeVulkanFrontendSession requires the Vulkan build gate"
#endif

#include <mutex>

#include "types.h"
#include "DesktopVulkanResourceLease.h"
#include "GPU3D_Vulkan.h"
#include "VulkanReference/FrameQueue.h"
#include "VulkanReference/VulkanOutput.h"
#include "VulkanReference/VulkanSurfacePresenter.h"
#include "SapphireVulkanFrameLatch.h"

namespace melonDS
{
class NDS;
class VulkanRenderer3D;
}

namespace MelonDSAndroid
{
class VulkanSurfacePresenter;
}

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

    bool beginProducerFrame(melonDS::VulkanRenderer3D& renderer3D);
    bool completeProducerFrame(melonDS::VulkanRenderer3D& renderer3D);
    void cancelProducerFrame();

    Frame* acquirePresentFrame();
    MelonDSAndroid::VulkanPresentResult presentAcquiredFrame(
        Frame* frame,
        MelonDSAndroid::VulkanSurfacePresenter& presenter,
        u64 timeoutNs);
    void commitPresentedFrame(Frame* frame);
    void deferPresentedFrame(Frame* frame);

    void registerPresenter(MelonDSAndroid::VulkanSurfacePresenter* presenter);
    void unregisterPresenter(MelonDSAndroid::VulkanSurfacePresenter* presenter);

    [[nodiscard]] bool isInitialized() const;
    [[nodiscard]] bool hasCompositedFrame() const;
    [[nodiscard]] bool hasPresentedFrame() const;
    [[nodiscard]] bool hasRegisteredPresenter() const;
    [[nodiscard]] bool backendSwitchInProgress() const;
    [[nodiscard]] bool isReadyForGeneration(u64 generation) const;
    [[nodiscard]] u64 generation() const;

private:
    FrameQueuePolicy queuePolicy();
    void clearProducerState();
    void synchronizeFrameReferencesLocked();
    Frame* acquireProducerRenderFrameLocked();
    bool latchAndPrepareProducerFrameLocked(
        Frame* frame,
        melonDS::VulkanRenderer3D& renderer3D,
        const melonDS::Vulkan3DFrameView& frameView);

    std::mutex presentationCallMutex;
    mutable std::mutex stateMutex;
    melonDS::NDS* nds = nullptr;
    MelonDSAndroid::VulkanOutput output;
    FrameQueue frameQueue;
    MelonDSAndroid::DesktopVulkanResourceLease resourceLease;
    MelonDSAndroid::VulkanSurfacePresenter* activePresenter = nullptr;
    MelonDSAndroid::VulkanSurfacePresenter* stagedPresenter = nullptr;
    MelonDSAndroid::SapphireVulkanFrameLatch frameLatch;
    Frame* pendingProducerFrame = nullptr;
    u64 activeGeneration = 0;
    u64 activeSurfaceGeneration = 0;
    u64 lastSubmittedSerial = 0;
    u64 lastPresentedSerial = 0;
    u64 lastPresentedFrameId = 0;
    bool initialized = false;
    bool producerSuspended = false;
};
