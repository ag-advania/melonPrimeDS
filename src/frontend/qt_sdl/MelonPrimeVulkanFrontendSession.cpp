#include "MelonPrimeVulkanFrontendSession.h"
#include "MelonPrimeVulkanRuntimePacing.h"
#include "MelonPrimeSapphireFrameInput.h"
#include "MelonPrimeSapphirePipelineMode.h"
#include "SapphirePublished2DFrame.h"
#include "VulkanPreparedContentStats.h"

#include "GPU3D_Vulkan.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace melonDS;
using namespace MelonDSAndroid;

namespace {

void LogVulkanProducerDiscard(const char* reason)
{
    Platform::Log(Platform::LogLevel::Info, "[VulkanProducer] discard reason=%s\n", reason);
}

void LogVulkanProducerFrameContext(
    Frame* frame,
    const Vulkan3DFrameView& frameView,
    u64 activeGeneration,
    const NDS* nds)
{
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanProducer] frame=%p valid=%d serial=%llu generation=%llu activeGeneration=%llu frontBuffer=%d screenSwap=%d\n",
        static_cast<void*>(frame),
        frameView.Valid ? 1 : 0,
        static_cast<unsigned long long>(frameView.FrameSerial),
        static_cast<unsigned long long>(frameView.Generation),
        static_cast<unsigned long long>(activeGeneration),
        nds ? nds->GPU.FrontBuffer : -1,
        nds ? (nds->GPU.GPU3D.RenderScreenSwapAt3D ? 1 : 0) : -1);
}

} // namespace

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
    int renderScale = 1;
    bool graphicsHardwareActive = false;
    bool temporalHistoryRequired = false;
    if (nds != nullptr && nds->GPU.GPU3D.HasCurrentRenderer())
    {
        if (auto* renderer3D = dynamic_cast<VulkanRenderer3D*>(
                &nds->GPU.GPU3D.GetCurrentRenderer()))
        {
            renderScale = std::max(renderer3D->GetScaleFactor(), 1);
            graphicsHardwareActive =
                renderer3D->GetActiveBackendMode()
                == VulkanRenderer3D::BackendMode::GraphicsHardware;
        }
    }
    temporalHistoryRequired =
        sapphireTemporalEnabled()
        && frameLatch.isVulkanTemporal3dHistoryGateActive();
    const bool presentationLate =
        GetVulkanRuntimePacingState().presentationLate.load(std::memory_order_acquire);
    return MakeVulkanFrameQueuePolicy(
        renderScale,
        presentationLate,
        graphicsHardwareActive,
        temporalHistoryRequired);
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
    if (frame == nullptr || nds == nullptr)
    {
        LogVulkanProducerDiscard("invalidFrameView");
        return false;
    }
    if (!frameView.Valid)
    {
        LogVulkanProducerDiscard("invalidFrameView");
        return false;
    }
    if (frameView.Generation != activeGeneration)
    {
        LogVulkanProducerDiscard("generationMismatch");
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
    const SapphirePublished2DFrame& published = nds->GPU.GetPublished2DFrame();
    const DesktopSapphireFrameBuildResult buildResult = BuildDesktopSapphireFrameInput(
        frame,
        nds->GPU,
        published,
        frameView,
        activeGeneration,
        frontBuffer,
        preparedFrameScreenSwap);
    if (buildResult.rejected)
    {
        LogVulkanProducerDiscard(buildResult.rejectReason != nullptr
            ? buildResult.rejectReason
            : "frameInputRejected");
        return false;
    }

    frameLatch.setTemporalEnabled(sapphireTemporalEnabled());
    if (!sapphireTemporalEnabled())
        output.invalidateTemporalHistory();

    if (!frameLatch.latchSoftPackedFrameSnapshot(
            buildResult.input,
            buildResult.sidecar,
            useStructuredVulkan2D))
    {
        LogVulkanProducerDiscard("latchFailed");
        return false;
    }
    if (frameLatch.regularCaptureTransitionResyncPending())
    {
        frameLatch.clearRegularCaptureTransitionResyncPending();
        output.clearStructuredCaptureHistory();
        if (auto* renderer2D = nds->GPU.TryGetSapphireRenderer2D())
            renderer2D->ClearStructuredVulkan2DState();
        frameLatch.clearLatchedSoftPackedFrameSnapshot();
        LogVulkanProducerDiscard("resyncRequested");
        return false;
    }

    if (!output.ensureFrameResources(frame, width, height))
    {
        LogVulkanProducerDiscard("ensureResourcesFailed");
        return false;
    }
    if (!output.prepareFrameForPresentation(
            frame, nds->GPU, buildResult.input.frontBuffer,
            buildResult.input.preparedFrameScreenSwap,
            frameLatch.mutableLastSnapshot(), renderer3D))
    {
        LogVulkanProducerDiscard("prepareFailed");
        return false;
    }

    return true;
}

bool MelonPrimeVulkanFrontendSession::beginProducerFrame(VulkanRenderer3D& renderer3D)
{
    std::scoped_lock lock(stateMutex);
    if (!initialized || producerSuspended || nds == nullptr)
        return false;

    frameLatch.setTemporalEnabled(sapphireTemporalEnabled());
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
        if (renderer3D.HasColorTarget() && renderer3D.IsColorTargetInitialized())
        {
            (void)output.captureRenderer3dSnapshot(
                frame, renderer3D, nds->GPU.GPU3D.RenderScreenSwapAt3D);
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
    LogVulkanProducerFrameContext(frame, frameView, activeGeneration, nds);

    if (frameView.FrameSerial == 0)
    {
        LogVulkanProducerDiscard("zeroSerial");
        frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
            return output.isFrameReferencedAsPendingPreviousSource(candidate);
        });
        frameQueue.discardRenderedFrame(frame);
        return false;
    }
    if (frameView.FrameSerial == lastSubmittedSerial)
    {
        LogVulkanProducerDiscard("duplicateSerial");
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
    const VulkanPreparedContentStats contentStats = CollectVulkanPreparedContentStats(
        nds->GPU,
        nds->GPU.FrontBuffer,
        renderer3D.HasColorTarget());
    LogVulkanPreparedContentStats(
        frame->frameId,
        nds->GPU.FrontBuffer,
        nds->GPU.GPU3D.RenderScreenSwapAt3D,
        contentStats);
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanProducer] queuePush frameId=%llu frameSerial=%llu rendererGeneration=%llu frontBuffer=%d screenSwap=%d\n",
        static_cast<unsigned long long>(frame->frameId),
        static_cast<unsigned long long>(frameView.FrameSerial),
        static_cast<unsigned long long>(frameView.Generation),
        nds->GPU.FrontBuffer,
        nds->GPU.GPU3D.RenderScreenSwapAt3D ? 1 : 0);
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

VulkanPresentResult MelonPrimeVulkanFrontendSession::presentAcquiredFrame(
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
            return VulkanPresentResult::InvalidFrameInputs;
        }

        if (nds->GPU.GPU3D.HasCurrentRenderer())
        {
            renderer3D = dynamic_cast<VulkanRenderer3D*>(
                &nds->GPU.GPU3D.GetCurrentRenderer());
        }
    }
    if (renderer3D == nullptr)
        return VulkanPresentResult::InvalidFrameInputs;

    const int scale = frame->width >= 256
        ? std::max<int>(1, static_cast<int>(frame->width / 256u))
        : std::max(renderer3D->GetScaleFactor(), 1);

    VulkanCompositionInputs inputs{};
    bool buildInputs = false;
    {
        std::scoped_lock stateLock(stateMutex);
        const Vulkan3DFrameView liveView = renderer3D->GetVulkan3DFrameView();
        buildInputs = output.buildCompositionInputs(
            frame, *renderer3D, scale, VulkanFilterMode::Nearest,
            false, false, false, inputs);
        Platform::Log(
            Platform::LogLevel::Info,
            "[VulkanPresent] frameId=%llu queuedSerial=%llu queuedGeneration=%llu "
            "liveSerial=%llu liveGeneration=%llu buildInputs=%d\n",
            static_cast<unsigned long long>(frame->frameId),
            static_cast<unsigned long long>(frame->frameSerial),
            static_cast<unsigned long long>(frame->rendererGeneration),
            static_cast<unsigned long long>(liveView.FrameSerial),
            static_cast<unsigned long long>(liveView.Generation),
            buildInputs ? 1 : 0);
        if (!buildInputs)
            return VulkanPresentResult::InvalidFrameInputs;
    }
    const VulkanPresentResult presentResult =
        presenter.presentFrame(frame, output, inputs, timeoutNs);
    Platform::Log(
        Platform::LogLevel::Info,
        "[VulkanPresent] frameId=%llu surfacePresent=%d result=%u\n",
        static_cast<unsigned long long>(frame->frameId),
        presentResult == VulkanPresentResult::PresentedGameFrame ? 1 : 0,
        static_cast<unsigned>(presentResult));
    return presentResult;
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
