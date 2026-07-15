#include "MelonPrimeVulkanFrontendSession.h"

#include "GPU3D_Vulkan.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace melonDS;
using namespace MelonDSAndroid;

MelonPrimeVulkanFrontendSession::~MelonPrimeVulkanFrontendSession()
{
    shutdown();
}

bool MelonPrimeVulkanFrontendSession::initialize(NDS& newNds)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (initialized)
    {
        nds = &newNds;
        frameLatch.bindNds(&newNds);
        return true;
    }

    if (!output.init())
        return false;

    nds = &newNds;
    frameLatch.bindNds(&newNds);
    initialized = true;
    return true;
}

void MelonPrimeVulkanFrontendSession::clearProducerState()
{
    frameLatch.clearLatchedSoftPackedFrameSnapshot();
    pendingProducerFrame = nullptr;
    lastPresentedSerial = 0;
    lastPresentedFrameId = 0;
}

void MelonPrimeVulkanFrontendSession::shutdown()
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    output.releaseTemporalFrameReferences();
    frameQueue.synchronizeHistoryReferences({});
    frameQueue.clear();
    output.shutdown();
    clearProducerState();
    activePresenter = nullptr;
    stagedPresenter = nullptr;
    activeGeneration = 0;
    activeSurfaceGeneration = 0;
    lastSubmittedSerial = 0;
    initialized = false;
    producerSuspended = false;
    nds = nullptr;
}

void MelonPrimeVulkanFrontendSession::beginBackendSwitch()
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);

    if (!producerSuspended)
        stagedPresenter = activePresenter;

    producerSuspended = true;

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] begin switch active=%p staged=%p\n",
        static_cast<void*>(activePresenter),
        static_cast<void*>(stagedPresenter));
}

void MelonPrimeVulkanFrontendSession::completeBackendSwitch(
    bool vulkanPresentationActive)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);

    if (vulkanPresentationActive)
    {
        if (stagedPresenter != nullptr)
            activePresenter = stagedPresenter;
    }
    else
        activePresenter = nullptr;

    stagedPresenter = nullptr;
    producerSuspended = false;
    frameQueue.requestPresentationResync();
    output.invalidateTemporalHistory();

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] complete switch vulkan=%d active=%p\n",
        vulkanPresentationActive ? 1 : 0,
        static_cast<void*>(activePresenter));
}

void MelonPrimeVulkanFrontendSession::beginGeneration(u64 newGeneration)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (activeGeneration == newGeneration)
        return;

    activeGeneration = newGeneration;
    lastSubmittedSerial = 0;
    lastPresentedSerial = 0;
    lastPresentedFrameId = 0;
    clearProducerState();
    output.invalidateTemporalHistory();
    frameQueue.synchronizeHistoryReferences({});
    frameQueue.setActiveGenerations(activeGeneration, activeSurfaceGeneration);
    frameQueue.requestPresentationResync();
}

void MelonPrimeVulkanFrontendSession::beginSurfaceGeneration(u64 newGeneration)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (newGeneration == 0 || activeSurfaceGeneration == newGeneration)
        return;
    activeSurfaceGeneration = newGeneration;
    frameQueue.setActiveGenerations(activeGeneration, activeSurfaceGeneration);
    frameQueue.requestPresentationResync();
}

void MelonPrimeVulkanFrontendSession::synchronizeFrameReferencesLocked()
{
    u64 completedTimelineValue = 0;
    if (activePresenter != nullptr)
        (void)activePresenter->getCompletedTimelineValue(completedTimelineValue);
    frameQueue.synchronizePresentationCompletion(completedTimelineValue);
    frameQueue.synchronizeHistoryReferences([&](const Frame* frame) {
        return output.isFrameReferencedAsPendingPreviousSource(frame);
    });
}

FrameQueuePolicy MelonPrimeVulkanFrontendSession::queuePolicy()
{
    FrameQueuePolicy policy{};
    policy.MaxBacklogDepth = 2;
    policy.AllowStealPending = false;
    policy.AllowPreviousFrameReuse = true;
    policy.AllowDropForDeadline = false;
    policy.PreferOldestFrame = false;
    policy.PreserveBacklogOnPresent = false;
    return policy;
}

Frame* MelonPrimeVulkanFrontendSession::acquireProducerRenderFrameLocked()
{
    const FrameQueuePolicy policy = queuePolicy();
    synchronizeFrameReferencesLocked();
    Frame* frame = frameQueue.getRenderFrame(policy);
    if (frame == nullptr)
    {
        output.releaseTemporalFrameReferences();
        frameQueue.synchronizeHistoryReferences({});
        synchronizeFrameReferencesLocked();
        frame = frameQueue.getRenderFrame(policy);
    }
    if (frame == nullptr)
    {
        const FrameQueueStats stats = frameQueue.takeStatsSnapshotAndReset();
        Platform::Log(
            Platform::LogLevel::Warn,
            "[VulkanProducer] no render frame backlog=%llu max=%llu "
            "queued=%llu presented=%llu discarded=%llu "
            "referenceBlocked=%llu stateFailures=%llu\n",
            static_cast<unsigned long long>(stats.CurrentBacklogDepth),
            static_cast<unsigned long long>(stats.MaxBacklogDepth),
            static_cast<unsigned long long>(stats.RenderFramesQueued),
            static_cast<unsigned long long>(stats.PresentFramesReturned),
            static_cast<unsigned long long>(stats.RenderFramesDiscarded),
            static_cast<unsigned long long>(stats.ReferenceBlockedReuse),
            static_cast<unsigned long long>(stats.StateTransitionFailures));
        return nullptr;
    }
    return frame;
}

bool MelonPrimeVulkanFrontendSession::latchAndPrepareProducerFrameLocked(
    Frame* frame,
    VulkanRenderer3D& renderer3D,
    const Vulkan3DFrameView& frameView)
{
    if (frame == nullptr || nds == nullptr || !frameView.Valid
        || frameView.Generation != activeGeneration)
    {
        return false;
    }

    const int scale = static_cast<int>(frameView.Scale);
    const int width = 256 * scale;
    const int height = (192 + 2 + 192) * scale;
    frameQueue.validateRenderFrame(frame, width, height, FrameBackend::VulkanImage);
    frame->frameSerial = frameView.FrameSerial;
    frame->rendererGeneration = frameView.Generation;

    const int frontBuffer = nds->GPU.FrontBuffer;
    const bool preparedFrameScreenSwap = nds->GPU.GPU3D.RenderScreenSwapAt3D;
    const bool useStructuredVulkan2D =
        renderer3D.GetActiveBackendMode() == VulkanRenderer3D::BackendMode::GraphicsHardware;
    if (!frameLatch.latchSoftPackedFrameSnapshot(
            frame, frontBuffer, preparedFrameScreenSwap, useStructuredVulkan2D))
    {
        return false;
    }
    if (frameLatch.regularCaptureTransitionResyncPending())
    {
        frameLatch.clearRegularCaptureTransitionResyncPending();
        output.clearStructuredCaptureHistory();
        nds->GPU.GetSapphireRenderer2D().ClearStructuredVulkan2DState();
        frameLatch.clearLatchedSoftPackedFrameSnapshot();
        return false;
    }

    return output.ensureFrameResources(frame, width, height)
        && output.prepareFrameForPresentation(
            frame, nds->GPU, frontBuffer, preparedFrameScreenSwap,
            frameLatch.mutableLastSnapshot(), renderer3D, frameView);
}

bool MelonPrimeVulkanFrontendSession::beginProducerFrame(VulkanRenderer3D& renderer3D)
{
    std::scoped_lock lock(stateMutex);
    if (!initialized || producerSuspended || nds == nullptr)
        return false;

    (void)frameLatch.updateVulkanTemporal3dHistoryGate();

    Frame* frame = acquireProducerRenderFrameLocked();
    if (frame == nullptr)
        return false;

    const int scale = std::max(renderer3D.GetScaleFactor(), 1);
    const int width = 256 * scale;
    const int height = (192 + 2 + 192) * scale;
    frameQueue.validateRenderFrame(frame, width, height, FrameBackend::VulkanImage);
    frame->renderTimelineValue = 0;

    if (!output.ensureFrameResources(frame, width, height))
    {
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    const bool usePreRunSnapshot =
        renderer3D.GetActiveBackendMode() != VulkanRenderer3D::BackendMode::GraphicsHardware
        || frameLatch.structuredCaptureGateFrames() > 0;
    if (usePreRunSnapshot)
    {
        const Vulkan3DFrameView frameView = renderer3D.GetVulkan3DFrameView();
        if (frameView.Valid)
        {
            (void)output.captureRenderer3dSnapshot(
                frame, frameView, nds->GPU.GPU3D.RenderScreenSwapAt3D);
        }
    }

    pendingProducerFrame = frame;
    return true;
}

bool MelonPrimeVulkanFrontendSession::completeProducerFrame(VulkanRenderer3D& renderer3D)
{
    std::scoped_lock lock(stateMutex);
    Frame* frame = pendingProducerFrame;
    pendingProducerFrame = nullptr;
    if (!initialized || producerSuspended || nds == nullptr || frame == nullptr)
        return false;

    const Vulkan3DFrameView frameView = renderer3D.GetVulkan3DFrameView();
    if (frameView.FrameSerial == 0 || frameView.FrameSerial == lastSubmittedSerial)
    {
        frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
            return output.isFrameReferencedAsPendingPreviousSource(candidate);
        });
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    const FrameQueuePolicy policy = queuePolicy();
    if (!latchAndPrepareProducerFrameLocked(frame, renderer3D, frameView))
    {
        frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
            return output.isFrameReferencedAsPendingPreviousSource(candidate);
        });
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    lastSubmittedSerial = frameView.FrameSerial;
    frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
        return output.isFrameReferencedAsPendingPreviousSource(candidate);
    });
    frameQueue.pushRenderedFrame(frame, policy);
    return true;
}

void MelonPrimeVulkanFrontendSession::cancelProducerFrame()
{
    std::scoped_lock lock(stateMutex);
    Frame* frame = pendingProducerFrame;
    pendingProducerFrame = nullptr;
    if (frame == nullptr)
        return;

    frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
        return output.isFrameReferencedAsPendingPreviousSource(candidate);
    });
    frameQueue.discardRenderedFrame(frame);
}

Frame* MelonPrimeVulkanFrontendSession::acquirePresentFrame()
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (!initialized || producerSuspended)
        return nullptr;
    synchronizeFrameReferencesLocked();
    return frameQueue.getPresentCandidate(queuePolicy(), std::nullopt);
}

bool MelonPrimeVulkanFrontendSession::presentAcquiredFrame(
    Frame* frame,
    VulkanSurfacePresenter& presenter,
    u64 timeoutNs)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    VulkanRenderer3D* renderer3D = nullptr;
    {
        std::scoped_lock stateLock(stateMutex);
        if (!initialized || producerSuspended
            || activePresenter != &presenter
            || frame == nullptr || nds == nullptr)
        {
            return false;
        }

        if (nds->GPU.GPU3D.HasCurrentRenderer())
        {
            renderer3D = dynamic_cast<VulkanRenderer3D*>(
                &nds->GPU.GPU3D.GetCurrentRenderer());
        }
    }
    if (renderer3D == nullptr)
        return false;

    const Vulkan3DFrameView frameView = renderer3D->GetVulkan3DFrameView();
    const int scale = frame->width >= 256
        ? std::max<int>(1, static_cast<int>(frame->width / 256u))
        : std::max(renderer3D->GetScaleFactor(), 1);

    VulkanCompositionInputs inputs{};
    {
        std::scoped_lock stateLock(stateMutex);
        if (!output.buildCompositionInputs(
                frame, frameView, scale, VulkanFilterMode::Nearest,
                false, false, false, inputs))
        {
            return false;
        }
    }
    return presenter.presentFrame(frame, output, inputs, timeoutNs);
}

bool MelonPrimeVulkanFrontendSession::updatePresenterOverlay(
    VulkanSurfacePresenter& presenter,
    int surfaceId,
    const VulkanSurfaceOverlay& overlay)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    {
        std::scoped_lock stateLock(stateMutex);
        if (!initialized || producerSuspended || activePresenter != &presenter)
            return false;
    }
    return presenter.updateOverlay(surfaceId, overlay);
}

void MelonPrimeVulkanFrontendSession::commitPresentedFrame(Frame* frame)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (frame == nullptr)
        return;

    lastPresentedSerial = frame->frameSerial;
    lastPresentedFrameId = frame->frameId;
    frameQueue.commitPresentedFrame(frame, queuePolicy());
}

void MelonPrimeVulkanFrontendSession::deferPresentedFrame(Frame* frame)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (frame != nullptr)
        frameQueue.deferPresentedFrame(frame, queuePolicy());
}

void MelonPrimeVulkanFrontendSession::registerPresenter(VulkanSurfacePresenter* presenter)
{
    std::scoped_lock lock(stateMutex);
    if (producerSuspended)
        stagedPresenter = presenter;
    else
        activePresenter = presenter;

    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresenterTrace] register presenter=%p suspended=%d active=%p staged=%p\n",
        static_cast<void*>(presenter),
        producerSuspended ? 1 : 0,
        static_cast<void*>(activePresenter),
        static_cast<void*>(stagedPresenter));
}

void MelonPrimeVulkanFrontendSession::unregisterPresenter(VulkanSurfacePresenter* presenter)
{
    std::scoped_lock presentationLock(presentationCallMutex);
    std::scoped_lock stateLock(stateMutex);
    if (stagedPresenter == presenter)
        stagedPresenter = nullptr;
    if (activePresenter == presenter)
    {
        u64 completedTimelineValue = 0;
        (void)presenter->getCompletedTimelineValue(completedTimelineValue);
        frameQueue.synchronizePresentationCompletion(completedTimelineValue);
        activePresenter = nullptr;
    }
}

bool MelonPrimeVulkanFrontendSession::isInitialized() const
{
    std::scoped_lock lock(stateMutex);
    return initialized && !producerSuspended;
}

bool MelonPrimeVulkanFrontendSession::hasCompositedFrame() const
{
    std::scoped_lock lock(stateMutex);
    return initialized && !producerSuspended && lastSubmittedSerial != 0;
}

bool MelonPrimeVulkanFrontendSession::hasPresentedFrame() const
{
    std::scoped_lock lock(stateMutex);
    return initialized
        && !producerSuspended
        && activePresenter != nullptr
        && lastPresentedSerial != 0;
}

bool MelonPrimeVulkanFrontendSession::hasRegisteredPresenter() const
{
    std::scoped_lock lock(stateMutex);
    return !producerSuspended && activePresenter != nullptr;
}

bool MelonPrimeVulkanFrontendSession::backendSwitchInProgress() const
{
    std::scoped_lock lock(stateMutex);
    return producerSuspended;
}

bool MelonPrimeVulkanFrontendSession::isReadyForGeneration(
    u64 expectedGeneration) const
{
    std::scoped_lock lock(stateMutex);
    return initialized
        && !producerSuspended
        && activeGeneration == expectedGeneration;
}

u64 MelonPrimeVulkanFrontendSession::generation() const
{
    std::scoped_lock lock(stateMutex);
    return activeGeneration;
}
