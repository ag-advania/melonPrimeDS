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
    std::scoped_lock lock(stateMutex);
    if (initialized)
    {
        nds = &newNds;
        return true;
    }

    if (!output.init())
        return false;

    nds = &newNds;
    initialized = true;
    return true;
}

void MelonPrimeVulkanFrontendSession::shutdown()
{
    std::scoped_lock lock(stateMutex);
    frameQueue.clear();
    frameInputs.clear();
    output.releaseTemporalFrameReferences();
    output.shutdown();
    lastCompleteSnapshot = {};
    activePresenter = nullptr;
    activeGeneration = 0;
    lastSubmittedSerial = 0;
    initialized = false;
    nds = nullptr;
}

void MelonPrimeVulkanFrontendSession::beginGeneration(u64 newGeneration)
{
    std::scoped_lock lock(stateMutex);
    if (activeGeneration == newGeneration)
        return;

    activeGeneration = newGeneration;
    lastSubmittedSerial = 0;
    lastCompleteSnapshot = {};
    frameInputs.clear();
    frameQueue.requestPresentationResync();
    output.invalidateTemporalHistory();
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
    policy.AllowStealPending = true;
    policy.AllowPreviousFrameReuse = true;
    policy.PreferOldestFrame = false;
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

bool MelonPrimeVulkanFrontendSession::submitCompletedFrame(
    VulkanRenderer3D& renderer3D,
    const MelonPrimeStructuredSnapshot& snapshot)
{
    std::scoped_lock lock(stateMutex);
    if (!initialized || nds == nullptr || !snapshot.complete
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

    const FrameQueuePolicy policy = queuePolicy();
    Frame* frame = nullptr;
    std::array<Frame*, FRAME_QUEUE_SIZE> deferredFrames{};
    size_t deferredFrameCount = 0;
    for (size_t attempt = 0; attempt < FRAME_QUEUE_SIZE; ++attempt)
    {
        Frame* candidate = frameQueue.getRenderFrame(policy);
        if (candidate == nullptr)
            break;
        const bool presentationComplete = activePresenter == nullptr
            || activePresenter->waitForFrameConsumption(candidate, 0);
        const bool retainedByTemporalHistory =
            output.isFrameReferencedAsPendingPreviousSource(candidate);
        if (presentationComplete && !retainedByTemporalHistory)
        {
            frame = candidate;
            break;
        }
        deferredFrames[deferredFrameCount++] = candidate;
    }
    for (size_t index = 0; index < deferredFrameCount; ++index)
        frameQueue.recycleRenderFrame(deferredFrames[index]);
    if (frame == nullptr)
        return false;

    const int scale = static_cast<int>(frameView.Scale);
    const int width = 256 * scale;
    const int height = (192 + 2 + 192) * scale;
    frameQueue.validateRenderFrame(frame, width, height, FrameBackend::VulkanImage);
    frame->source3dFrameSerial = frameView.FrameSerial;
    frame->rendererGeneration = frameView.Generation;

    SoftPackedFrameSnapshot packedSnapshot{};
    buildSoftPackedSnapshot(snapshot, frame->frameId, packedSnapshot);
    VulkanCompositionInputs inputs{};
    const bool prepared = output.ensureFrameResources(frame, width, height)
        && output.prepareFrameForPresentation(
            frame, nds->GPU, 0, snapshot.screenSwap, packedSnapshot, renderer3D, frameView)
        && output.buildCompositionInputs(
            frame, frameView, scale, VulkanFilterMode::Nearest,
            false, false, false, inputs)
        && output.composeAndSubmitFrame(frame, inputs);
    if (!prepared)
    {
        frameQueue.discardRenderedFrame(frame);
        return false;
    }

    frameInputs[frame] = inputs;
    lastCompleteSnapshot = snapshot;
    lastSubmittedSerial = snapshot.frameSerial;
    frameQueue.pushRenderedFrame(frame, policy);
    return true;
}

Frame* MelonPrimeVulkanFrontendSession::acquirePresentFrame()
{
    std::scoped_lock lock(stateMutex);
    if (!initialized)
        return nullptr;
    return frameQueue.getPresentCandidate(queuePolicy(), std::nullopt);
}

bool MelonPrimeVulkanFrontendSession::presentAcquiredFrame(
    Frame* frame,
    VulkanSurfacePresenter& presenter,
    u64 timeoutNs)
{
    std::scoped_lock lock(stateMutex);
    const auto iterator = frameInputs.find(frame);
    if (!initialized || frame == nullptr || iterator == frameInputs.end())
        return false;
    return presenter.presentFrame(frame, output, iterator->second, timeoutNs);
}

void MelonPrimeVulkanFrontendSession::commitPresentedFrame(Frame* frame)
{
    std::scoped_lock lock(stateMutex);
    if (frame != nullptr)
        frameQueue.commitPresentedFrame(frame, queuePolicy());
}

void MelonPrimeVulkanFrontendSession::deferPresentedFrame(Frame* frame)
{
    std::scoped_lock lock(stateMutex);
    if (frame != nullptr)
        frameQueue.deferPresentedFrame(frame, queuePolicy());
}

void MelonPrimeVulkanFrontendSession::registerPresenter(VulkanSurfacePresenter* presenter)
{
    std::scoped_lock lock(stateMutex);
    activePresenter = presenter;
}

void MelonPrimeVulkanFrontendSession::unregisterPresenter(VulkanSurfacePresenter* presenter)
{
    std::scoped_lock lock(stateMutex);
    if (activePresenter == presenter)
        activePresenter = nullptr;
}

bool MelonPrimeVulkanFrontendSession::isInitialized() const
{
    std::scoped_lock lock(stateMutex);
    return initialized;
}

bool MelonPrimeVulkanFrontendSession::hasCompleteStructuredSnapshot() const
{
    std::scoped_lock lock(stateMutex);
    return lastCompleteSnapshot.complete;
}

bool MelonPrimeVulkanFrontendSession::hasCompositedFrame() const
{
    std::scoped_lock lock(stateMutex);
    return initialized && lastSubmittedSerial != 0;
}

bool MelonPrimeVulkanFrontendSession::hasRegisteredPresenter() const
{
    std::scoped_lock lock(stateMutex);
    return activePresenter != nullptr;
}

u64 MelonPrimeVulkanFrontendSession::generation() const
{
    std::scoped_lock lock(stateMutex);
    return activeGeneration;
}
