/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.
    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.
*/

#ifndef MELONPRIME_VULKAN_PRESENTER_H
#define MELONPRIME_VULKAN_PRESENTER_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "MelonPrimeVulkanSurface.h"
#include "MelonPrimeVulkanLatencyController.h"
#include "VulkanAmdAntiLag.h"
#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanNvidiaReflex.h"
#include "VulkanPresentLatencyCapture.h"
#include "VulkanPresentPacer.h"
#include "VulkanPresentedFrame.h"
#include "VulkanSync.h"

class QWidget;

namespace MelonPrime
{

// ---------------------------------------------------------------------------
// Native Vulkan presentation: swapchain ownership and the final present.
//
// The composed DS screens arrive as a leased device-local buffer and are copied
// directly into sampled images. CPU HUD/OSD sources still use the upload ring;
// nothing on the visible screen path is read back. The presenter and renderer
// hold ref-counted views of the same VulkanDevice, and their host queue access
// is serialized by that device's queue mutex.
//
// Thread ownership. Every Vulkan object below is created and destroyed by the
// presenter/emulation thread. GUI-thread events (resize, DPI, fullscreen and
// Linux native-surface lifecycle) only publish snapshots or set atomic flags;
// they never create, destroy or rebind a Vulkan object.
// ---------------------------------------------------------------------------

class VulkanPresenter
{
public:
    // Layers the presenter keeps a persistent texture for. One image per slot,
    // reallocated only when its dimensions change, so the steady-state frame
    // does no image creation at all.
    enum class Layer : melonDS::u32
    {
        ScreenTop = 0,      // composed top screen, internal resolution
        ScreenBottom,       // composed bottom screen, internal resolution
        Hud,                // Custom HUD overlay, widget-sized, premultiplied
        Osd,                // OSD message strip, premultiplied
        Count
    };

    enum class Blend : melonDS::u32
    {
        // The composed DS screens. The compositor writes BGRX, so the alpha
        // channel carries no coverage information and blending is disabled.
        Opaque = 0,
        // QImage::Format_ARGB32_Premultiplied sources: ONE / ONE_MINUS_SRC_ALPHA,
        // the same factors ScreenPanelGL uses for the HUD and OSD.
        Premultiplied,
    };

    // One quad, in swapchain (physical) pixels. Matches the push-constant block
    // in MelonPrimeVulkanPresentShaders/Present.vert.
    struct Quad
    {
        float Axis[4]{1.0f, 0.0f, 0.0f, 1.0f};      // X axis (xy), Y axis (zw)
        float Origin[4]{0.0f, 0.0f, 1.0f, 1.0f};    // origin (xy), viewport size (zw)
        float UvRect[4]{0.0f, 0.0f, 1.0f, 1.0f};
        float Tint[4]{1.0f, 1.0f, 1.0f, 1.0f};
    };

    VulkanPresenter() = default;
    ~VulkanPresenter();

    VulkanPresenter(const VulkanPresenter&) = delete;
    VulkanPresenter& operator=(const VulkanPresenter&) = delete;

    // Acquires the shared VkInstance, creates the platform surface for
    // `surfaceWidget`, selects a physical device that can present to it,
    // creates the logical device, the render pass, the two pipelines and the
    // first swapchain. Presenter/emulation thread for all platforms.
    bool Init(QWidget* surfaceWidget);

    // Presenter-thread entry point. No QWidget or QPA accessor is read; all
    // native handles and the requested physical size come from the GUI
    // thread's post-lifecycle snapshot.
    bool Init(const VulkanSurface::NativeWindowSnapshot& snapshot);

    // Waits for the device to go idle and destroys everything, including the
    // surface and the instance reference. Safe to call more than once.
    void Shutdown() noexcept;
    void Quiesce() noexcept;

    // Must run after acquiring the renderer output lease and after BeginFrame
    // admits a writable presenter slot, before UploadLayerFromImage(). A
    // resource-generation change is the only direct descriptor path that may
    // quiesce the device.
    bool PrepareDirectOutputDescriptors(const melonDS::VulkanPresentedFrame& frame);
    // Rebinds a retained screen layer for the current frame without allocating
    // descriptors or uploading renderer output. The caller must have already
    // proved that the renderer frame identity is unchanged.
    bool ReuseScreenLayerFromFrame(
        Layer layer, const melonDS::VulkanPresentedFrame& frame);
    [[nodiscard]] bool HasRetainedDirectLayer(Layer layer) const noexcept;
    void InvalidateScreenLayerRetention() noexcept;
    // Renderer transition hook. The preallocated descriptor sets survive; only
    // their resource/view identity mapping is invalidated.
    void InvalidateDirectDescriptorCache() noexcept;

    [[nodiscard]] bool IsInitialized() const noexcept { return Initialized; }
    [[nodiscard]] const std::string& LastError() const noexcept { return Error; }

    // The platform surface, so the panel can keep a platform-owned layer's
    // geometry in step with the widget (macOS CAMetalLayer). GUI thread.
    [[nodiscard]] const VulkanSurface::Surface& GetPlatformSurface() const noexcept
    {
        return Surface;
    }

    // GUI thread. Coalesced: the swapchain is not rebuilt here, only marked
    // out of date, so a resize drag costs one rebuild at the next frame
    // boundary instead of one per event. A non-zero geometry revision is an
    // edge trigger; repeated publication of the same snapshot cannot create
    // another dirty event.
    void NotifySurfaceChanged(std::uint64_t geometryRevision = 0) noexcept;
    // Published by the GUI thread for truthful swapchain diagnostics. The
    // presenter never reads QWidget state from its emulation-thread path.
    void SetWindowFullscreen(bool fullscreen) noexcept
    {
        WindowFullscreen.store(fullscreen, std::memory_order_release);
    }
    // The screen panel supplies the renderer-owned identity before presenter
    // admission. A newer epoch may restart the serial sequence; within one
    // epoch, serials must never move backwards.
    void SetPresentedFrameIdentity(melonDS::u64 serial, melonDS::u64 epoch) noexcept
    {
        PendingPresentedFrameSerial = serial;
        PendingPresentedFrameEpoch = epoch;
    }
    void SetPresentedFrameSerial(melonDS::u64 serial) noexcept
    {
        SetPresentedFrameIdentity(serial, 0);
    }
    [[nodiscard]] bool IsPresentedFrameIdentityMonotonic() const noexcept
    {
        if (PendingPresentedFrameSerial == 0)
            return true;
        return PendingPresentedFrameEpoch > LastPresentedFrameEpoch
            || (PendingPresentedFrameEpoch == LastPresentedFrameEpoch
                && (LastPresentedFrameSerial == 0
                    || PendingPresentedFrameSerial >= LastPresentedFrameSerial));
    }

    // Either thread. A changed value marks the swapchain out of date, because
    // the present mode is baked into VkSwapchainCreateInfoKHR.
    void SetVSync(bool enabled) noexcept;

    // Presenter thread. Applies saved preferences without opening a latency
    // frame; startup uses this before emitting the first effective-state log.
    void SetLowLatencyPreferences(int reflexMode, bool antiLag2Enabled);
    // The renderer publishes configuration requests, but only the presenter
    // knows whether the vendor path is available and still accepted at
    // runtime. Screen-panel admission must use this effective authority.
    [[nodiscard]] bool HasEffectiveLowLatencyAuthority() const noexcept
    {
        return Latency.HasEffectiveAuthority();
    }
    void SetGenericPresentPacingPolicy(int policy) noexcept { PresentPacer.SetPolicy(policy); }
    // Physical-pixel size of the current swapchain. Zero before the first
    // successful BeginFrame().
    [[nodiscard]] melonDS::u32 GetWidth() const noexcept { return SwapchainExtent.width; }
    [[nodiscard]] melonDS::u32 GetHeight() const noexcept { return SwapchainExtent.height; }

    // --- frame path --------------------------------------------------------
    //
    // BeginFrame -> UploadLayer* -> BeginComposition -> DrawLayer* -> EndFrame.
    //
    // BeginFrame() returning false means "no frame this time" and the caller
    // must NOT call EndFrame(); that covers a minimized window (surface extent
    // 0), a swapchain that could not be rebuilt yet, a latency-budget skip, and
    // a lost device. It is not necessarily an error -- check
    // LastError()/HasFailed().

    // `requestedWidth/Height` are the widget's physical pixel size, used only
    // when the surface reports currentExtent == 0xFFFFFFFF (Wayland).
    // `waitForPresentSlot=false` performs a side-effect-free readiness probe
    // before swapchain acquisition. A busy presenter slot is an intentional
    // latency skip, not a presenter failure; the caller can distinguish it
    // through LastBeginWasLatencySkip().
    bool BeginFrame(
        melonDS::u32 requestedWidth,
        melonDS::u32 requestedHeight,
        bool waitForPresentSlot = true);

    // Records the staging copy for one layer. Tightly packed or `rowBytes`-
    // strided BGRA8. Must be called before BeginComposition(), because
    // vkCmdCopyBufferToImage cannot be recorded inside a render pass.
    bool UploadLayer(
        Layer layer,
        const void* pixels,
        melonDS::u32 width,
        melonDS::u32 height,
        std::size_t rowBytes);

    // Updates a persistent layer image without re-uploading unchanged pixels.
    // Source coordinates are expressed in the full `width` x `height` image.
    bool UploadLayerRegion(
        Layer layer,
        const void* pixels,
        melonDS::u32 width,
        melonDS::u32 height,
        std::size_t rowBytes,
        melonDS::u32 x,
        melonDS::u32 y,
        melonDS::u32 regionWidth,
        melonDS::u32 regionHeight);

    [[nodiscard]] bool HasLayerContent(Layer layer) const noexcept
    {
        return Layers[static_cast<std::size_t>(layer)].HasContent;
    }

    // GPU-native screen upload. The composed source buffer belongs to the
    // renderer but was produced on this presenter's shared main queue, so the
    // copy needs only a queue-ordered buffer barrier and no CPU fence wait.
    bool UploadLayerFromBuffer(
        Layer layer,
        const melonDS::VulkanPresentedFrame& frame,
        VkDeviceSize sourceOffset);

    // Binds the compositor's sampleable image directly. The image belongs to
    // the retained renderer lease and is already in SHADER_READ_ONLY_OPTIMAL;
    // this path deliberately contains no high-resolution buffer copy.
    bool UploadLayerFromImage(
        Layer layer,
        const melonDS::VulkanPresentedFrame& frame);

    // Begins the render pass and clears the whole swapchain image to black,
    // which is what produces the letterbox/pillarbox bars.
    void BeginComposition();

    // Draws one quad from a previously uploaded layer. Layers are drawn in call
    // order, so the caller controls the compositing order.
    void DrawLayer(Layer layer, const Quad& quad, Blend blend, bool linearFilter);
    void DrawRadar(
        const Quad& quad, float opacity, melonDS::u32 sourceCenterY,
        melonDS::u32 sourceRadius);

    // Ends the render pass, submits and presents. Returns false on a hard
    // failure; VK_ERROR_OUT_OF_DATE_KHR / VK_SUBOPTIMAL_KHR are handled
    // internally by marking the swapchain dirty and are NOT failures.
    bool EndFrame();

    // True once a VkResult the presenter cannot recover from was observed
    // (device lost, out of memory, etc.). Surface loss is intentionally not a
    // permanent failure: it sets NeedsSurfaceRebind() instead.
    [[nodiscard]] bool HasFailed() const noexcept { return Failed; }
    [[nodiscard]] bool LastBeginWasLatencySkip() const noexcept
    {
        return LastBeginLatencySkip;
    }
    [[nodiscard]] bool NeedsSurfaceRebind() const noexcept
    {
        return SurfaceRebindRequested;
    }
    void ClearSurfaceRebindRequest() noexcept
    {
        SurfaceRebindRequested = false;
    }
    [[nodiscard]] melonDS::u32 GetFrameIndex() const noexcept
    {
        return Frames.GetFrameIndex();
    }

    // --- vendor low-latency path -------------------------------------------
    //
    // NVIDIA Reflex and AMD Anti-Lag 2 both live here rather than on the 3D
    // renderer, because both are scoped to the object that owns the swapchain
    // and the present queue -- which is this class. The renderer only carries
    // the user's setting; it has no surface and never calls
    // vkQueuePresentKHR, so it could not drive either extension.
    //
    // The emulation thread drives these five in order once per emulated frame,
    // through ScreenPanelVulkan's Screen.h hooks. RENDERSUBMIT_* and PRESENT_*
    // are NOT in this list: those are emitted inside EndFrame(), tight around
    // the real vkQueueSubmit and vkQueuePresentKHR, which is the only place
    // they can be truthful.
    //
    // `reflexMode` is the config value (0 off, 1 on, 2 on+boost) and
    // `antiLag2Enabled` the config bool; both are re-applied here every frame
    // so a settings-dialog change takes effect on the next frame without
    // recreating the device or the swapchain.
    //
    // `targetFrameIntervalNs` is the emulator's own frame interval for this
    // frame, taken from the frame limiter's step rather than from the display
    // refresh rate -- the DS frame rate follows the configured TargetFPS, not
    // the monitor. It is zero whenever the emulator is not running at a fixed
    // rate (fast-forward, slow motion, unlimited FPS), which is what keeps
    // target-time presentation scheduling off in those modes.
    void BeginLowLatencyFrame(
        int reflexMode,
        bool antiLag2Enabled,
        bool normalSpeed,
        melonDS::u64 targetFrameIntervalNs,
        melonDS::u64 logicalFrameId);
    void MarkLowLatencyInputSample();
    void MarkLowLatencySimulationStart();
    void MarkLowLatencySimulationEnd();
    void FinishLowLatencyFrame();

    // Requested / Supported / Enabled / Actual / Reason for both features, as
    // one line each. Emitted once after Init() and again whenever the effective
    // state changes, so a log excerpt always says what is really running.
    void LogLowLatencyState(const char* context);

private:
    // Emitted only when the effective state actually changed, so a per-frame
    // caller cannot flood the log.
    void LogLowLatencyStateIfChanged();

#ifdef MELONPRIME_ENABLE_DEVELOPER_FEATURES
    // Periodic vkGetLatencyTimingsNV dump. Developer builds only -- it exists
    // to prove the markers reach the driver, which a shipping user's log does
    // not need.
    void ReportLatencyTimings();
    melonDS::u32 LatencyTimingCountdown = 0;
#endif

    struct LayerTexture
    {
        melonDS::Vk::Image Image;
        melonDS::u32 Width = 0;
        melonDS::u32 Height = 0;
        bool HasContent = false;
        VkImage DirectImage = VK_NULL_HANDLE;
        VkImageView DirectView = VK_NULL_HANDLE;
        melonDS::u64 DirectResourceGeneration = 0;
        std::array<VkDescriptorSet, 2> DirectDescriptorSets{};
        bool UsesDirect = false;

        // Only persistent descriptor-cache bindings may cross a presentation
        // frame. Current-frame direct state above is still cleared at every
        // BeginFrame; this retained state is restored only after the caller
        // proves that the renderer frame identity is unchanged.
        VkImage RetainedDirectImage = VK_NULL_HANDLE;
        VkImageView RetainedDirectView = VK_NULL_HANDLE;
        melonDS::u64 RetainedDirectResourceGeneration = 0;
        std::array<VkDescriptorSet, 2> RetainedDirectDescriptorSets{};
        bool RetainedDirectValid = false;
    };

    struct DirectDescriptorCacheEntry
    {
        VkImage Image = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        melonDS::u64 ResourceGeneration = 0;
        std::array<VkDescriptorSet, 2> Sets{};
        bool Valid = false;
    };

    bool AcquireContext();
    bool CreateSurface(QWidget* widget);
    bool CreateSurface(const VulkanSurface::NativeWindowSnapshot& snapshot);
    bool CreateDeviceObjects();
    bool CreateRenderPass();
    bool CreatePipelines();
    bool CreateDescriptorObjects();
    bool CreateSamplers();

    bool RecreateSwapchain(melonDS::u32 requestedWidth, melonDS::u32 requestedHeight);
    void DestroySwapchainObjects(bool immediate);
    bool CreatePresentOwnershipResources(melonDS::u32 imageCount);
    void DestroyPresentOwnershipResources();
    bool RecordPresentOwnershipAcquire();
    bool ChoosePresentMode(const std::vector<VkPresentModeKHR>& available, VkPresentModeKHR& out,
                           std::string& reason) const;
    bool ChooseSurfaceFormat(
        VkSurfaceFormatKHR& out,
        std::string& reason,
        VkResult* failureResult = nullptr) const;

    bool EnsureLayerImage(Layer layer, LayerTexture& texture, melonDS::u32 width, melonDS::u32 height,
                          const char* debugName);
    bool EnsureStaging(VkDeviceSize bytes);
    bool UpdateLayerDescriptorSets(Layer layer);
    bool UpdateDirectDescriptorSets(
        DirectDescriptorCacheEntry& entry,
        VkImage image,
        VkImageView view,
        melonDS::u64 resourceGeneration);
    bool EnsureDirectDescriptor(
        VkImage image,
        VkImageView view,
        melonDS::u64 resourceGeneration);
    static void ClearRetainedDirectBinding(LayerTexture& texture) noexcept;
    const DirectDescriptorCacheEntry* FindDirectDescriptor(
        VkImage image,
        VkImageView view,
        melonDS::u64 resourceGeneration) const noexcept;
    VkDescriptorSet AcquireDescriptorSet(Layer layer, bool linearFilter) const noexcept;

    bool Fail(const char* operation, VkResult result);
    bool Fail(std::string reason);
    bool RequestSurfaceRebind(const char* operation, VkResult result);

    melonDS::VulkanContext* Context = nullptr;
    bool ContextAcquired = false;

    QWidget* SurfaceWidget = nullptr;
    VulkanSurface::Surface Surface;
    std::uint64_t SurfaceGeneration = 0;
    std::uintptr_t SurfaceNativeHandle = 0;

    melonDS::VulkanDevice Device;
    melonDS::Vk::FrameRing Frames;

    // Both vendor low-latency state machines, the logical frame index they
    // share, and the log-on-change latch. The pacer stays here because it
    // participates in swapchain construction.
    VulkanLatencyController Latency;
    melonDS::VulkanPresentPacer PresentPacer;
    // A/B measurement instrument. Stateless and free unless the build defines
    // MELONPRIME_VULKAN_LATENCY_CAPTURE; it never influences what is presented.
    melonDS::VulkanPresentLatencyCapture LatencyCapture;
    melonDS::u64 PendingPresentedFrameSerial = 0;
    melonDS::u64 PendingPresentedFrameEpoch = 0;
    melonDS::u64 LastPresentedFrameSerial = 0;
    melonDS::u64 LastPresentedFrameEpoch = 0;
    // A strict latest-submission fence timeout is a pacing budget miss, not a
    // renderer failure. Consume this at the next frame boundary so the
    // presenter stays alive and retries on the following frame.
    bool SkipNextPresentationForLatencyBudget = false;
    bool LastBeginLatencySkip = false;

    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    // Monotonic presenter-owned identity for diagnostics and retained-output
    // invalidation. It changes only after a successful swapchain creation.
    std::uint64_t SwapchainGeneration = 0;
    VkSurfaceFormatKHR SurfaceFormat{};
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D SwapchainExtent{0, 0};
    // Disabled by default. This is only enabled for a split-family device
    // when the explicit SyncVal-clean environment gate is present.
    bool UseSplitQueueExclusiveExperiment = false;
    std::vector<VkImage> SwapchainImages;
    // Distinct successful-acquire observations since the current swapchain was
    // created. This is diagnostic only, not a WSI availability query or fence map:
    // it must never gate, wait, or recreate the swapchain.
    std::vector<bool> SwapchainImageAcquireObserved;
    melonDS::u32 DistinctSwapchainImagesAcquiredSinceRecreate = 0;
    std::vector<VkImageView> SwapchainImageViews;
    std::vector<VkFramebuffer> SwapchainFramebuffers;

    // One render-finished semaphore per swapchain image, not per frame slot.
    //
    // The frame fence covers the submission, not the present that waits on the
    // semaphore, so with more swapchain images than frames in flight a
    // per-slot semaphore could be re-signalled while a present was still
    // waiting on it. VulkanSync.h documents exactly this case.
    std::vector<VkSemaphore> RenderFinished;
    VkCommandPool PresentOwnershipCommandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> PresentOwnershipCommandBuffers;
    std::vector<VkSemaphore> PresentOwnershipFinished;

    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline PipelineOpaque = VK_NULL_HANDLE;
    VkPipeline PipelineBlended = VK_NULL_HANDLE;
    VkSampler SamplerNearest = VK_NULL_HANDLE;
    VkSampler SamplerLinear = VK_NULL_HANDLE;

    // Descriptor lifetime classification:
    // A: SetLayout, samplers, and all preallocated descriptor sets live for the
    // presenter. B: fallback layer images and direct renderer views live until
    // their owner recreates them. C: descriptor identity is cached without
    // retaining renderer-owned images or output states.
    VkDescriptorPool PersistentDescriptorPool = VK_NULL_HANDLE;
    std::array<std::array<VkDescriptorSet, 2>, static_cast<std::size_t>(Layer::Count)>
        LayerDescriptorSets{};
    static constexpr std::size_t kDirectCompositorSlotCount = 3;
    static constexpr std::size_t kDirectArraySliceCount = 2;
    static constexpr std::size_t kDirectViewCacheCount =
        kDirectCompositorSlotCount * kDirectArraySliceCount;
    static constexpr std::size_t kDirectDescriptorSetCount =
        kDirectViewCacheCount * 2;
    static_assert(kDirectViewCacheCount == 6);
    static_assert(kDirectDescriptorSetCount == 12);
    std::array<DirectDescriptorCacheEntry, kDirectViewCacheCount>
        DirectDescriptorCache{};
    melonDS::u64 CachedDirectResourceGeneration = 0;

    // Per frame-in-flight transient pool, kept separate from persistent layer
    // sets. It is reset lazily, only when the previous use of that slot
    // allocated from it; steady-state direct-descriptor-cache hits therefore
    // avoid a reset without ever invalidating persistent descriptors.
    // Freeing individual sets is never needed and vkResetDescriptorPool is the
    // cheapest way to recycle a whole frame's worth.
    std::array<VkDescriptorPool, melonDS::Vk::FramesInFlight> DescriptorPools{};
    std::array<bool, melonDS::Vk::FramesInFlight> TransientDescriptorPoolUsed{};

    // Per frame-in-flight upload ring. Reset in BeginFrame() after that slot's
    // fence has signalled, which is what makes overwriting last-use contents
    // safe without per-allocation tracking.
    std::array<melonDS::Vk::StagingRing, melonDS::Vk::FramesInFlight> Staging;
    VkDeviceSize StagingCapacity = 0;
    // Growth requested by an upload that did not fit, applied at the next
    // frame boundary. Never applied mid-frame: the open command buffer already
    // references the current ring's VkBuffer.
    VkDeviceSize PendingStagingRequest = 0;

    std::array<LayerTexture, static_cast<std::size_t>(Layer::Count)> Layers;

    std::atomic_bool SwapchainDirty{false};
    std::atomic<std::uint64_t> PendingGeometryRevision{0};
    std::atomic_bool WindowFullscreen{false};
    std::atomic_bool VSyncRequested{true};
    bool VSyncApplied = true;

    melonDS::u32 CurrentImageIndex = 0;
#if defined(MELONPRIME_ENABLE_RENDERER_PERF_TELEMETRY)
    // Validation-only probe: counts consecutive successful acquisitions that
    // return the same swapchain image index. This is not queue ownership.
    melonDS::u32 PreviousAcquiredImageIndex = 0;
    bool HasPreviousAcquiredImageIndex = false;
#endif
    VkCommandBuffer CurrentCommandBuffer = VK_NULL_HANDLE;
    bool FrameOpen = false;
    bool CompositionOpen = false;
    bool Initialized = false;
    bool Failed = false;
    bool SurfaceRebindRequested = false;
    bool FirstPresentLogged = false;

    std::string Error;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_PRESENTER_H
