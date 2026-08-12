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
#include <string>
#include <vector>

#include "MelonPrimeVulkanSurface.h"
#include "VulkanAmdAntiLag.h"
#include "VulkanCommon.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanNvidiaReflex.h"
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
// Thread ownership. Every Vulkan object below is created and destroyed by
// whichever thread calls Init() / RecreateSwapchain() / Shutdown(), and those
// never run concurrently with a frame: Init() and Shutdown() happen on the GUI
// thread while the panel is not published to MainWindow::panel, and swapchain
// recreation happens inside BeginFrame() on the presenting thread. GUI-thread
// events (resize, DPI change, fullscreen) only set the atomic flags below; they
// never touch a Vulkan object.
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
    // first swapchain. GUI thread.
    bool Init(QWidget* surfaceWidget);

    // Waits for the device to go idle and destroys everything, including the
    // surface and the instance reference. Safe to call more than once.
    void Shutdown() noexcept;
    void Quiesce() noexcept;

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
    // boundary instead of one per event.
    void NotifySurfaceChanged() noexcept { SwapchainDirty.store(true, std::memory_order_release); }
    // Published by the GUI thread for truthful swapchain diagnostics. The
    // presenter never reads QWidget state from its emulation-thread path.
    void SetWindowFullscreen(bool fullscreen) noexcept
    {
        WindowFullscreen.store(fullscreen, std::memory_order_release);
    }

    // Either thread. A changed value marks the swapchain out of date, because
    // the present mode is baked into VkSwapchainCreateInfoKHR.
    void SetVSync(bool enabled) noexcept;

    // Presenter thread. Applies saved preferences without opening a latency
    // frame; startup uses this before emitting the first effective-state log.
    void SetLowLatencyPreferences(int reflexMode, bool antiLag2Enabled);
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
    // 0), a swapchain that could not be rebuilt yet, and a lost device. It is
    // not necessarily an error -- check LastError()/HasFailed().

    // `requestedWidth/Height` are the widget's physical pixel size, used only
    // when the surface reports currentExtent == 0xFFFFFFFF (Wayland).
    bool BeginFrame(melonDS::u32 requestedWidth, melonDS::u32 requestedHeight);

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
    // (device lost, surface lost, out of memory). The panel turns this into a
    // renderer runtime failure.
    [[nodiscard]] bool HasFailed() const noexcept { return Failed; }
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
        melonDS::u64 targetFrameIntervalNs);
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
    };

    bool AcquireContext();
    bool CreateSurface(QWidget* widget);
    bool CreateDeviceObjects();
    bool CreateRenderPass();
    bool CreatePipelines();
    bool CreateDescriptorObjects();
    bool CreateSamplers();

    bool RecreateSwapchain(melonDS::u32 requestedWidth, melonDS::u32 requestedHeight);
    void DestroySwapchainObjects(bool immediate);
    bool ChoosePresentMode(const std::vector<VkPresentModeKHR>& available, VkPresentModeKHR& out,
                           std::string& reason) const;
    bool ChooseSurfaceFormat(VkSurfaceFormatKHR& out, std::string& reason) const;

    bool EnsureLayerImage(LayerTexture& texture, melonDS::u32 width, melonDS::u32 height,
                          const char* debugName);
    bool EnsureStaging(VkDeviceSize bytes);
    VkDescriptorSet AcquireDescriptorSet(VkImageView view, VkSampler sampler);

    bool Fail(const char* operation, VkResult result);
    bool Fail(std::string reason);

    melonDS::VulkanContext* Context = nullptr;
    bool ContextAcquired = false;

    QWidget* SurfaceWidget = nullptr;
    VulkanSurface::Surface Surface;

    melonDS::VulkanDevice Device;
    melonDS::Vk::FrameRing Frames;

    melonDS::VulkanNvidiaReflex Reflex;
    melonDS::VulkanAmdAntiLag AntiLag;
    melonDS::VulkanPresentPacer PresentPacer;
    // Frame index handed to Anti-Lag's INPUT/PRESENT pair. It follows the
    // Reflex frame id when Reflex is running so both features describe the same
    // frame, and falls back to its own counter otherwise (an AMD GPU has
    // Anti-Lag and no Reflex, so that is the normal case for it).
    melonDS::u64 LowLatencyFrameIndex = 0;
    // Last state LogLowLatencyStateIfChanged() reported, so the per-frame path
    // logs a transition once instead of every frame.
    int LoggedReflexMode = -1;
    bool LoggedReflexActive = false;
    bool LoggedAntiLagActive = false;
    bool LowLatencyStateLogged = false;

    VkSwapchainKHR Swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR SurfaceFormat{};
    VkPresentModeKHR PresentMode = VK_PRESENT_MODE_FIFO_KHR;
    VkExtent2D SwapchainExtent{0, 0};
    std::vector<VkImage> SwapchainImages;
    std::vector<VkImageView> SwapchainImageViews;
    std::vector<VkFramebuffer> SwapchainFramebuffers;

    // One render-finished semaphore per swapchain image, not per frame slot.
    //
    // The frame fence covers the submission, not the present that waits on the
    // semaphore, so with more swapchain images than frames in flight a
    // per-slot semaphore could be re-signalled while a present was still
    // waiting on it. VulkanSync.h documents exactly this case.
    std::vector<VkSemaphore> RenderFinished;

    // Fence of the frame that last targeted each swapchain image, so a frame
    // never records into an image the GPU has not finished with.
    std::vector<VkFence> ImagesInFlight;

    VkRenderPass RenderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipeline PipelineOpaque = VK_NULL_HANDLE;
    VkPipeline PipelineBlended = VK_NULL_HANDLE;
    VkSampler SamplerNearest = VK_NULL_HANDLE;
    VkSampler SamplerLinear = VK_NULL_HANDLE;

    // Per frame-in-flight descriptor pool, reset at the top of every frame.
    // Freeing individual sets is never needed and vkResetDescriptorPool is the
    // cheapest way to recycle a whole frame's worth.
    std::array<VkDescriptorPool, melonDS::Vk::FramesInFlight> DescriptorPools{};

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
    std::atomic_bool WindowFullscreen{false};
    std::atomic_bool VSyncRequested{true};
    bool VSyncApplied = true;

    melonDS::u32 CurrentImageIndex = 0;
    VkCommandBuffer CurrentCommandBuffer = VK_NULL_HANDLE;
    bool FrameOpen = false;
    bool CompositionOpen = false;
    bool Initialized = false;
    bool Failed = false;
    bool FirstPresentLogged = false;

    std::string Error;
};

} // namespace MelonPrime

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // MELONPRIME_VULKAN_PRESENTER_H
