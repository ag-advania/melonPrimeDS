// MelonInstanceVulkan.h
//
// MELONPRIME-PC-ADAPT / port note: this file is a FUNCTION-DEFINITION-GRANULARITY extraction of the
// Vulkan presentation pipeline owned by `MelonDSAndroid::MelonInstance` in the reference Android
// frontend (melonDS-android tag 0.7.0.rc4, app/src/main/cpp/MelonInstance.h / MelonInstance.cpp).
// The class name (`MelonDSAndroid::MelonInstance`) is kept identical to the reference so that every
// kept member-function BODY in MelonInstanceVulkan.cpp can be copied byte-verbatim under its original
// `MelonInstance::` qualifier.
//
// This is NOT the full reference MelonInstance: only the members needed by the KEPT Vulkan-pipeline
// functions are declared here. Reference-only members whose owning functions were omitted (ROM/save
// loading, cheats, input, netplay, RetroAchievements/JNI plumbing, all "*ForDebug" capture/dump
// tooling) are dropped. See the mapping comment block at the top of MelonInstanceVulkan.cpp for the
// full kept/omitted function inventory with reference line ranges.
//
// Three kinds of deviation from the reference exist here, each called out at its point of use with a
// `MELONPRIME-PC-ADAPT` comment:
//   1. Members whose real reference type is JNI/Android-only and out of scope to port (RetroAchievements
//      manager) are replaced by a minimal local stand-in type with the same call surface used by kept
//      function bodies, so those call sites stay verbatim.
//   2. A handful of locals in the reference `runFrame()` cross the Pre/Post split boundary described in
//      MelonInstanceVulkan.cpp (Part 2 of the port task) and are promoted to members here.
//   3. `attachVulkanSurface`'s `ANativeWindow*` parameter (an Android NDK type with no desktop
//      equivalent) is adapted to `void*` (a native window/HWND handle), matching the same adaptation
//      already made in the verbatim-ported `VulkanSurfacePresenter::attachSurface`.

#ifndef MELONPRIME_SAPPHIRE_MELONINSTANCEVULKAN_H
#define MELONPRIME_SAPPHIRE_MELONINSTANCEVULKAN_H

#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <vector>

#include "NDS.h"

#include "Configuration.h"
#include "RewindManager.h"
#include "SaveManager.h"
#include "VulkanPerfStats.h" // resolves to src/VulkanPerfStats.h (already ported as part of the Sapphire core)
#include "renderer/FrameQueue.h"
#include "renderer/Renderer.h"
#include "renderer/ScreenshotRenderer.h"
#include "renderer/VulkanOutput.h"
#include "renderer/VulkanSurfacePresenter.h"

// GPU3D_Vulkan.h lives at the src/ root (already ported verbatim as part of the Sapphire GPU3D core
// port); this TU's include path already resolves it the same bare way other sapphire/ files resolve
// core headers like "NDS.h"/"types.h".
#include "GPU3D_Vulkan.h"

using namespace melonDS;

namespace MelonDSAndroid
{

// MELONPRIME-PC-ADAPT: stand-in for reference's `std::unique_ptr<RetroAchievements::RetroAchievementsManager>`
// (RetroAchievementsManager.h pulls in <jni.h>, rcheevos, and rc_client — an Android/JNI-only dependency
// tree that is out of scope for the Vulkan pipeline port). The only member this class's kept functions
// call is `FrameUpdate()` (reference MelonInstance.cpp:1596, inside runFrame's post-step), which is a
// per-frame RetroAchievements runtime tick; the desktop frontend has no RetroAchievements integration
// wired up yet, so this is a no-op. Keeps the `retroAchievementsManager->FrameUpdate();` call site in
// runFrameVulkanPostStep() byte-verbatim.
class RetroAchievementsManagerStub
{
public:
    void FrameUpdate() {}
};

class MelonInstance
{
public:
    // MELONPRIME-PC-ADAPT: new constructor (the reference constructor at MelonInstance.cpp:1056 is not in
    // the kept-function list — it wires JNI-only args (NDSArgs, Net, JNI ScreenshotRenderer ownership)
    // that are out of scope). This constructor takes the already-constructed `NDS*` and shared
    // `EmulatorConfiguration` directly; Part 3 (desktop Qt/EmuThread wiring) is responsible for
    // constructing/owning this MelonInstance alongside its NDS instance and keeping `nds`/
    // `currentConfiguration` valid for the MelonInstance's lifetime.
    MelonInstance(int instanceId, melonDS::NDS* nds, std::shared_ptr<EmulatorConfiguration> configuration);
    ~MelonInstance();

    int getInstanceId() const { return instanceId; }
    Renderer getCurrentRenderer() const { return currentRenderer; }

    // Reference MelonInstance.cpp:1281-1371 (kept verbatim).
    bool precompileVulkanPipelines(const VulkanSurfaceConfig& retroArchConfig);

    // MELONPRIME-PC-ADAPT: reference `runFrame()` (MelonInstance.cpp:1421-1804) split at the
    // `u32 nLines = nds->RunFrame();` emu-step line so desktop EmuThread's existing generic frame-step
    // call site can sit between the two halves. See MelonInstanceVulkan.cpp's top-of-file mapping
    // comment ("Part 2: runFrame split") for the full crossing-locals list and rationale.
    bool runFrameVulkanPreStep();
    void runFrameVulkanPostStep(melonDS::u32 nLines);

    // Reference MelonInstance.cpp:1948-2274 (kept verbatim, except attachVulkanSurface's parameter type).
    bool waitForPresentationFrame(Frame* frame, u64 timeoutNs);
    int attachVulkanSurface(void* window, u32 width, u32 height); // MELONPRIME-PC-ADAPT: ANativeWindow* -> void* (HWND)
    bool resizeVulkanSurface(int surfaceId, u32 width, u32 height);
    bool configureVulkanSurface(int surfaceId, const VulkanSurfaceConfig& config, const VulkanBackgroundImage& backgroundImage);
    void detachVulkanSurface(int surfaceId);
    bool presentVulkanFrame(
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> deadline,
        std::optional<std::chrono::time_point<std::chrono::steady_clock>> budgetDeadline);
    void requestVulkanPresentationResync();
    void requestVulkanFastForwardPresentationTransition();

    // MELONPRIME-PC-ADAPT: new; Part 3 wiring needs a way to (re)apply currentConfiguration->renderer /
    // renderSettings changes. Reference `updateConfiguration()` (MelonInstance.cpp:3975-3987) additionally
    // touches ndsSave/gbaSave/firmwareSave/rewindManager reconfiguration and audio settings that are
    // outside the Vulkan pipeline's scope, so this is a minimal replacement, not a port of that function.
    void updateConfiguration(std::shared_ptr<EmulatorConfiguration> newConfiguration)
    {
        currentConfiguration = std::move(newConfiguration);
        isRenderConfigurationDirty = true;
    }

private:
    // Reference MelonInstance.cpp:4189-4369 (kept verbatim).
    void updateRenderer();
    // Reference MelonInstance.cpp:3949-3973 (kept verbatim).
    void updateVulkanFastForwardRenderScale();
    // Reference MelonInstance.cpp:1806-1823 (kept verbatim).
    void handleVulkanRuntimeFailure(const char* reason);
    // Reference MelonInstance.cpp:4393-4451 (kept verbatim).
    bool updateVulkanScreenshot(Frame* frame, int scale, bool clearOnFailure);
    // Reference MelonInstance.cpp:4507-4723 (kept verbatim).
    void logVulkanPerformanceIfNeeded();
    // Reference MelonInstance.cpp:4737-4761 (kept verbatim).
    void clearLatchedSoftPackedFrameSnapshot();
    // Reference MelonInstance.cpp:4763-4812 (kept verbatim).
    bool updateVulkanTemporal3dHistoryGate();
    // Reference MelonInstance.cpp:4814-4818 (kept verbatim).
    bool isVulkanTemporal3dHistoryGateActive() const;
    // Reference MelonInstance.cpp:4819-8056 (kept verbatim; the entire soft-packed latch pipeline).
    bool latchSoftPackedFrameSnapshot(const Frame* frame, int frontBuffer, bool screenSwap, bool useStructuredVulkan2D);

    // MELONPRIME-PC-ADAPT: no-op stubs for reference private helpers in the omitted debug/JNI cluster
    // (MelonInstance.cpp:2764-3948, 4725-4736) that KEPT functions call. Keeping these as real (if inert)
    // member functions lets every call site in the kept function bodies stay byte-verbatim. See
    // MelonInstanceVulkan.cpp's mapping comment for the full omitted-function/call-site inventory.
    void clearPreparedVulkanDebugSnapshot() {}
    void clearPreparedOpenGlDebugSnapshot() {}
    void prepareOpenGlDebugSnapshot(int completedFrame) { (void)completedFrame; }
    void maybeCaptureDenseScreenBurstFrame(Frame* frameOverride, int scaleOverride, int completedFrame)
    {
        (void)frameOverride; (void)scaleOverride; (void)completedFrame;
    }
    void saveRewindState(RewindSaveState* rewindSaveState) { (void)rewindSaveState; }

private:
    int instanceId;
    melonDS::NDS* nds;

    std::shared_ptr<EmulatorConfiguration> currentConfiguration;
    FrameQueue frameQueue;
    std::unique_ptr<VulkanOutput> vulkanOutput;
    std::unique_ptr<VulkanSurfacePresenter> vulkanSurfacePresenter;
    std::vector<u32> vulkanReadbackFrame;
    Frame* lastCompletedVulkanFrame = nullptr;
    int lastCompletedVulkanScale = 1;
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenCapture3dDsFrame{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidTopScreenResolvedPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> lastValidBottomScreenResolvedPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidTopScreenResolvedPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> lastValidBottomScreenResolvedPrimaryLines{};
    bool hasLastValidTopScreenCapture3dDsFrame = false;
    bool hasLastValidBottomScreenCapture3dDsFrame = false;
    bool vulkanRegularCaptureTransitionResyncPending = false;
    int vulkanStructuredCaptureGateFrames = 0;
    int vulkanTemporal3dHistoryGateFrames = 0;
    int vulkanTemporal3dNotReadyFrames = 0;
    int vulkanTemporal3dHistoryDebugLogsRemaining = 0;
    bool lastVulkanFastForwardPresentationState = false;
    int vulkanFastForwardPreviousFrameFallbackFrames = 0;
    SoftPackedFrameSnapshot lastSoftPackedFrameSnapshot;
    SoftPackedFrameSnapshot previousSoftPackedFrameSnapshot;
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopPlane0{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopPlane1{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineATopControl{};
    std::array<u32, SoftPackedFrameSnapshot::kLineCount> cachedEngineATopLineMeta{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomPlane0{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomPlane1{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedEngineABottomControl{};
    std::array<u32, SoftPackedFrameSnapshot::kLineCount> cachedEngineABottomLineMeta{};
    bool cachedEngineATopValid = false;
    bool cachedEngineABottomValid = false;
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayTopPrimary{};
    std::array<u32, SoftPackedFrameSnapshot::kPixelCount> cachedAtypicalDisplayBottomPrimary{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayTopPrimaryLines{};
    std::array<u8, SoftPackedFrameSnapshot::kLineCount> cachedAtypicalDisplayBottomPrimaryLines{};
    int framesSinceLastScreenSwapToggle = 1024;
    bool wasInAlternatingMode = false;
    std::unique_ptr<ScreenshotRenderer> screenshotRenderer;
    RewindManager rewindManager;
    Renderer currentRenderer = Renderer::Vulkan;
    bool isRenderConfigurationDirty = true;
    bool vulkanRuntimeConfigLogged = false;
    bool vulkanRuntimeFailureHandled = false;
    int vulkanPrepareFailureCount = 0;
    u64 vulkanSoftPackedMissingWindow = 0;
    u64 vulkanHeldPreviousFrameWindow = 0;
    u64 vulkanPrepareFailedWindow = 0;
    int frame = 0;
    PerfSampleWindow<120> vulkanRunFrameCpuWindow;
    PerfSampleWindow<120> vulkanSetupCpuWindow;
    PerfSampleWindow<120> vulkanNdsRunCpuWindow;
    PerfSampleWindow<120> vulkanPostRunCpuWindow;
    PerfSampleWindow<120> vulkanComposeCpuWindow;
    std::atomic_bool openGlDebugSnapshotRequested{false};
    std::atomic<float> slot2AnalogX{0.0f};
    std::atomic<float> slot2AnalogY{0.0f};
    std::unique_ptr<RetroAchievementsManagerStub> retroAchievementsManager;
    std::unique_ptr<SaveManager> ndsSave;
    std::unique_ptr<SaveManager> gbaSave;
    std::unique_ptr<SaveManager> firmwareSave;

    // MELONPRIME-PC-ADAPT: promoted from reference runFrame() locals that cross the Pre/Post split
    // boundary (see MelonInstanceVulkan.cpp's "Part 2" mapping comment). Each is written in
    // runFrameVulkanPreStep() and consumed in runFrameVulkanPostStep() within the same frame; they carry
    // no state across frames (fully overwritten every runFrameVulkanPreStep() call), matching the
    // reference locals' lifetime.
    bool crossStep_measuringVulkan = false;
    u64 crossStep_runFrameStartNs = 0;
    u64 crossStep_ndsRunStartNs = 0;
    int crossStep_vulkanRenderScale = 1;
    FrameBackend crossStep_frameBackend = FrameBackend::OpenGlTexture;
    FrameQueuePolicy crossStep_frameQueuePolicy{};
    Frame* crossStep_renderFrame = nullptr;
    bool crossStep_isRendererAccelerated = false;
};

}

#endif // MELONPRIME_SAPPHIRE_MELONINSTANCEVULKAN_H
