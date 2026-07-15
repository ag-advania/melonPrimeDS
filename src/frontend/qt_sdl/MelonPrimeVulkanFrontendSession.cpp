#include "MelonPrimeVulkanFrontendSession.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include "GPU.h"
#include "GPU3D_Vulkan.h"
#include "NDS.h"
#include "Platform.h"
#include "VulkanStructuredControlAbi.h"
#include "VulkanReference/VulkanSurfacePresenter.h"

using namespace melonDS;
using namespace melonDS::VulkanStructuredControlAbi;
using namespace MelonDSAndroid;

namespace
{
constexpr size_t kScreenPixels = MelonPrimeStructuredSnapshot::PixelCount;
constexpr size_t kScreenLines = MelonPrimeStructuredSnapshot::LineCount;
constexpr size_t kEnginePixels = kScreenPixels;
constexpr size_t kEngineLines = kScreenLines;

template <typename T, size_t N>
void CopyEngineArray(
    std::array<T, N>& destination,
    const T* source,
    size_t engine,
    size_t engineElementCount)
{
    std::memcpy(
        destination.data(),
        source + (engine * engineElementCount),
        engineElementCount * sizeof(T));
}

bool PixelIsVisible2D(u32 pixel)
{
    const u32 alpha = PixelAlpha(pixel);
    return alpha != 0u && (alpha & 0x40u) == 0u;
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
    structuredSnapshot = {};
    frameLatch.clearLatchedSoftPackedFrameSnapshot();
    pendingProducerFrame = nullptr;
    hasCompleteStructuredSnapshot_ = false;
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
    frameInputs.clear();
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
    frameInputs.clear();
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
    frameInputs.clear();
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

bool MelonPrimeVulkanFrontendSession::captureCompletedSnapshot(
    const GPU& gpu,
    u64 generation,
    MelonPrimeStructuredSnapshot& snapshot)
{
    auto source = std::make_unique<SapphireStructured2DFrameSnapshot>();
    if (!gpu.CopyStructured2DFrameSnapshot(*source)
        || !source->Complete
        || source->FrameSerial == 0
        || source->Generation != generation)
        return false;

    snapshot.generation = source->Generation;
    snapshot.frameSerial = source->FrameSerial;
    snapshot.screenSwap = source->ScreenSwap;

    const size_t topEngine = snapshot.screenSwap ? 1u : 0u;
    const size_t bottomEngine = snapshot.screenSwap ? 0u : 1u;
    const u32* plane0 = source->Plane0.data();
    const u32* plane1 = source->Plane1.data();
    const u32* control = source->Control.data();
    const u32* nativeFinal = source->NativeFinal.data();
    const u32* lineMeta = source->LineMeta.data();
    const u32* lineState = source->LineState.data();

    CopyEngineArray(snapshot.topPlane0, plane0, topEngine, kEnginePixels);
    CopyEngineArray(snapshot.topPlane1, plane1, topEngine, kEnginePixels);
    CopyEngineArray(snapshot.topControl, control, topEngine, kEnginePixels);
    CopyEngineArray(snapshot.topNativeFinal, nativeFinal, topEngine, kEnginePixels);
    CopyEngineArray(snapshot.topLineMeta, lineMeta, topEngine, kEngineLines);
    CopyEngineArray(snapshot.topLineState, lineState, topEngine, kEngineLines);
    CopyEngineArray(snapshot.bottomPlane0, plane0, bottomEngine, kEnginePixels);
    CopyEngineArray(snapshot.bottomPlane1, plane1, bottomEngine, kEnginePixels);
    CopyEngineArray(snapshot.bottomControl, control, bottomEngine, kEnginePixels);
    CopyEngineArray(snapshot.bottomNativeFinal, nativeFinal, bottomEngine, kEnginePixels);
    CopyEngineArray(snapshot.bottomLineMeta, lineMeta, bottomEngine, kEngineLines);
    CopyEngineArray(snapshot.bottomLineState, lineState, bottomEngine, kEngineLines);
    snapshot.complete = true;
    return true;
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

void MelonPrimeVulkanFrontendSession::buildSoftPackedSnapshot(
    const MelonPrimeStructuredSnapshot& source,
    u64 frameId,
    SoftPackedFrameSnapshot& destination)
{
    destination.clear();
    destination.frameId = frameId;
    destination.sourceFrameSerial = source.frameSerial;
    destination.rendererGeneration = source.generation;
    destination.frontBufferLatched = 0;
    destination.screenSwapLatched = source.screenSwap;

    const auto convertScreen = [](
        const std::array<u32, kScreenPixels>& sourcePlane0,
        const std::array<u32, kScreenPixels>& sourcePlane1,
        const std::array<u32, kScreenPixels>& sourceControl,
        const std::array<u32, kScreenPixels>& nativeFinal,
        const std::array<u32, kScreenLines>& rawLineMeta,
        const std::array<u32, kScreenLines>& rawLineState,
        std::array<u32, kScreenPixels>& packedPlane0,
        std::array<u32, kScreenPixels>& packedPlane1,
        std::array<u32, kScreenPixels>& packedControl,
        std::array<u32, kScreenLines>& packedLineMeta) {
        for (size_t y = 0; y < kScreenLines; ++y)
        {
            const u32 displayMode = (rawLineMeta[y] >> 16u) & 0x3u;
            const bool screensEnabled = (rawLineState[y] & (1u << 16u)) != 0u;
            const bool engineEnabled = (rawLineState[y] & (1u << 17u)) != 0u;
            const bool forcedBlank = (rawLineState[y] & (1u << 18u)) != 0u;
            const bool structuredDisplay =
                displayMode == 1u && screensEnabled && engineEnabled && !forcedBlank;
            // The compositor ABI expects MasterBrightness in bits 0..15 and
            // display mode in 16..17. Non-regular modes use the latched native
            // line as a 2D-only regular-display payload.
            packedLineMeta[y] = (structuredDisplay ? (rawLineState[y] & 0xFFFFu) : 0u)
                | (1u << 16u);

            const size_t rowBase = y * 256u;
            for (size_t x = 0; x < 256u; ++x)
            {
                const size_t index = rowBase + x;
                const u32 originalPlane0 = sourcePlane0[index];
                const u32 originalPlane1 = sourcePlane1[index];
                const bool plane0Is3D = structuredDisplay && Is3DLayer(originalPlane0);
                const bool plane1Is3D = structuredDisplay && Is3DLayer(originalPlane1);
                const bool has3DSlot = plane0Is3D || plane1Is3D;
                const bool hasAbove3D = plane1Is3D;

                if (!structuredDisplay || !has3DSlot)
                {
                    packedPlane0[index] = nativeFinal[index];
                    packedPlane1[index] = 0u;
                }
                else if (plane0Is3D)
                {
                    packedPlane0[index] = originalPlane1;
                    packedPlane1[index] = 0u;
                }
                else
                {
                    packedPlane0[index] = 0u;
                    packedPlane1[index] = originalPlane0;
                }

                const bool protectedBlack = IsOpaqueBlack2D(
                    hasAbove3D ? packedPlane1[index] : packedPlane0[index]);
                const u32 controlWithValidity = sourceControl[index] | SourceValidBit;
                packedControl[index] = ConvertSourceControlToPacked(
                    controlWithValidity,
                    has3DSlot,
                    hasAbove3D,
                    protectedBlack);
            }
        }
    };

    convertScreen(
        source.topPlane0, source.topPlane1, source.topControl,
        source.topNativeFinal, source.topLineMeta, source.topLineState,
        destination.packedTopPlane0, destination.packedTopPlane1,
        destination.packedTopControl, destination.packedTopLineMeta);
    convertScreen(
        source.bottomPlane0, source.bottomPlane1, source.bottomControl,
        source.bottomNativeFinal, source.bottomLineMeta, source.bottomLineState,
        destination.packedBottomPlane0, destination.packedBottomPlane1,
        destination.packedBottomControl, destination.packedBottomLineMeta);

    destination.topScreenStats = collectStats(
        destination.packedTopPlane0,
        destination.packedTopPlane1,
        destination.packedTopControl,
        destination.packedTopLineMeta);
    destination.bottomScreenStats = collectStats(
        destination.packedBottomPlane0,
        destination.packedBottomPlane1,
        destination.packedBottomControl,
        destination.packedBottomLineMeta);
    destination.valid = true;
}

SoftPackedScreenStats MelonPrimeVulkanFrontendSession::collectStats(
    const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& plane0,
    const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& plane1,
    const std::array<u32, MelonPrimeStructuredSnapshot::PixelCount>& control,
    const std::array<u32, MelonPrimeStructuredSnapshot::LineCount>& lineMeta)
{
    SoftPackedScreenStats stats{};
    for (u32 meta : lineMeta)
        stats.DisplayModeCounts[(meta >> 16u) & 0x3u]++;

    for (size_t index = 0; index < plane0.size(); ++index)
    {
        const u32 alpha = control[index] >> 24u;
        const u32 effect = alpha & PackedEffectMask;
        if (effect < stats.CompModeCounts.size())
            stats.CompModeCounts[effect]++;
        const bool slot = (alpha & Packed3DSlotFlag) != 0u;
        const bool above = (alpha & PackedAbove3DFlag) != 0u;
        if (slot)
            stats.StructuredSlotPixels++;
        if (slot && above)
            stats.StructuredAbovePixels++;
        if (!slot && above)
            stats.Structured2DOnlyPixels++;
        if (plane0[index] != 0u)
            stats.Plane0UsefulPixels++;
        if (plane1[index] != 0u)
            stats.Plane1UsefulPixels++;
        if (PixelIsVisible2D(plane0[index]))
            stats.Plane0VisiblePixels++;
        if (PixelIsVisible2D(plane1[index]))
            stats.Plane1VisiblePixels++;
        if (IsOpaqueBlack2D(plane0[index]))
            stats.Plane0OpaqueBlackPixels++;
        if (IsOpaqueBlack2D(plane1[index]))
            stats.Plane1OpaqueBlackPixels++;
        if (slot && above && PixelIsVisible2D(plane1[index]))
            stats.StructuredAboveVisiblePixels++;
        if (slot && above && IsOpaqueBlack2D(plane1[index]))
            stats.StructuredAboveBlackPixels++;
        if (!slot && above && PixelIsVisible2D(plane0[index]))
            stats.Structured2DOnlyVisiblePixels++;
        if ((alpha & PackedProtectedBlackFlag) != 0u)
            stats.ProtectedBlackPixels++;
    }
    return stats;
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
    frameInputs.erase(frame);
    return frame;
}

bool MelonPrimeVulkanFrontendSession::latchAndPrepareProducerFrameLocked(
    Frame* frame,
    VulkanRenderer3D& renderer3D,
    const Vulkan3DFrameView& frameView,
    bool composeOnProducer)
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

    const SoftPackedFrameSnapshot& latchedSnapshot = frameLatch.lastSnapshot();
    VulkanCompositionInputs inputs{};
    bool prepared = output.ensureFrameResources(frame, width, height)
        && output.prepareFrameForPresentation(
            frame, nds->GPU, frontBuffer, preparedFrameScreenSwap,
            frameLatch.mutableLastSnapshot(), renderer3D, frameView);
    if (prepared && composeOnProducer)
    {
        prepared = output.buildCompositionInputs(
                frame, frameView, scale, VulkanFilterMode::Nearest,
                false, false, false, inputs)
            && output.composeAndSubmitFrame(frame, inputs);
        if (prepared)
            frameInputs[frame] = inputs;
    }
    return prepared;
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
    const bool composeOnProducer = false;
    if (!latchAndPrepareProducerFrameLocked(frame, renderer3D, frameView, composeOnProducer))
    {
        frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
            return output.isFrameReferencedAsPendingPreviousSource(candidate);
        });
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    hasCompleteStructuredSnapshot_ = true;
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

bool MelonPrimeVulkanFrontendSession::submitCompletedFrame(
    VulkanRenderer3D& renderer3D)
{
    const MelonPrimeStructuredSnapshot& snapshot = structuredSnapshot;
    std::scoped_lock lock(stateMutex);
    if (!initialized || producerSuspended || nds == nullptr || !snapshot.complete
        || snapshot.generation != activeGeneration
        || snapshot.frameSerial == 0
        || snapshot.frameSerial == lastSubmittedSerial)
    {
        return false;
    }

    const Vulkan3DFrameView frameView = renderer3D.GetVulkan3DFrameView();
    if (!frameView.Valid
        || frameView.FrameSerial != snapshot.frameSerial
        || frameView.Generation != snapshot.generation)
    {
        return false;
    }

    Frame* frame = acquireProducerRenderFrameLocked();
    if (frame == nullptr)
        return false;

    const FrameQueuePolicy policy = queuePolicy();
    const bool composeOnProducer = false;
    if (!latchAndPrepareProducerFrameLocked(frame, renderer3D, frameView, composeOnProducer))
    {
        frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
            return output.isFrameReferencedAsPendingPreviousSource(candidate);
        });
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    hasCompleteStructuredSnapshot_ = true;
    lastSubmittedSerial = snapshot.frameSerial;
    frameQueue.synchronizeHistoryReferences([&](const Frame* candidate) {
        return output.isFrameReferencedAsPendingPreviousSource(candidate);
    });
    frameQueue.pushRenderedFrame(frame, policy);
    return true;
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

bool MelonPrimeVulkanFrontendSession::hasCompleteStructuredSnapshot() const
{
    std::scoped_lock lock(stateMutex);
    return !producerSuspended && hasCompleteStructuredSnapshot_;
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
