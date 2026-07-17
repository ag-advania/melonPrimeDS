// MelonPrimeDS - experimental Metal renderer (Metal-plan Phase 8)

#ifndef GPU_METAL_H
#define GPU_METAL_H

#if defined(MELONPRIME_ENABLE_METAL)

// MELONPRIME_METAL_HIRES_VISIBLE_OUTPUT_V1

#include "GPU_Soft.h"

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace melonDS
{

class MetalRenderer2D;

class MetalRenderer : public SoftRenderer
{
public:
    explicit MetalRenderer(melonDS::NDS& nds, bool useComputeRenderer = false) noexcept;
    ~MetalRenderer() override;

    bool Init() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void Start3DRendering() override;
    void Finish3DRendering() override;
    void DrawScanline(u32 line) override;
    void DrawSprites(u32 line) override;
    void VBlank() override;
    void VBlankEnd() override;
    void AllocCapture(u32 bank, u32 start, u32 len) override;
    void SyncVRAMCapture(
        u32 bank,
        u32 start,
        u32 len,
        bool complete) override;
    void SwapBuffers() override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

private:
    struct MetalOutputState;
    struct MetalFullGpuState;
    struct MetalCaptureState;

    std::unique_ptr<MetalRenderer2D> Metal2D_A;
    std::unique_ptr<MetalRenderer2D> Metal2D_B;
    int ScaleFactor = 1;
    bool ComputeRendererSelected = false;
    u16 FrameMasterBrightnessA = 0;
    u16 FrameMasterBrightnessB = 0;
    // MELONPRIME_METAL_HIRES_SCALE_AUTHORITY_V2
    // MELONPRIME_METAL_OUTPUT_LEASE_V1
    // MELONPRIME_METAL_MASTER_BRIGHTNESS_V1
    // MELONPRIME_METAL_OUTPUT_STATE_ATOMIC_PUBLICATION_V1: OutputState is
    // published from the emulation/render thread (scale reconfigure swap) and
    // loaded from the presenter thread (AcquireOutputLease). The same
    // shared_ptr object must only be accessed via std::atomic_load/store/
    // exchange -- plain copy/assign is a C++ data race even though the
    // control block's refcount is itself atomic. Use Load/Store/Exchange
    // helpers below; never touch OutputState with operator= or copy except
    // through those helpers.
    std::shared_ptr<MetalOutputState> OutputState;
    std::unique_ptr<MetalFullGpuState> FullGpuState;
    // shared_ptr (not unique_ptr): capture-upload completion handlers capture
    // this by value so a reconfigure (scale change) mid-flight can never
    // leave a handler writing through a dangling pointer -- see
    // UploadCpuCompletedCaptures() in GPU_MetalCaptureMethods.inc.
    // CaptureState is only mutated on the emulation/render thread today, so
    // it does not need atomic publication (unlike OutputState).
    std::shared_ptr<MetalCaptureState> CaptureState;
    // MELONPRIME_METAL_GPU_RESIDENT_2D_V1
    // MELONPRIME_METAL_GPU_DISPLAY_CAPTURE_V1

    [[nodiscard]] std::shared_ptr<MetalOutputState> LoadOutputState() const
    {
        return std::atomic_load_explicit(
            &OutputState, std::memory_order_acquire);
    }

    void StoreOutputState(std::shared_ptr<MetalOutputState> state)
    {
        std::atomic_store_explicit(
            &OutputState, std::move(state), std::memory_order_release);
    }

    std::shared_ptr<MetalOutputState> ExchangeOutputState(
        std::shared_ptr<MetalOutputState> next)
    {
        return std::atomic_exchange_explicit(
            &OutputState, std::move(next), std::memory_order_acq_rel);
    }

    void ConfigureMetal2DMirror(void* preferredDevice);
    bool ConfigureMetalVisibleOutput(void* preferredDevice);
    bool InitializeMetalFullGpuOutput();
    bool IsMetalFullGpuFrameEligible() const;
    bool ComposeMetalFullGpuOutput();

    bool ConfigureMetalCaptureState(void* preferredDevice);
    void BeginMetalCaptureFrame();
    void CaptureMetalDisplayCaptureLine(u32 line);
    bool EncodeMetalDisplayCapture(
        void* engineA2DTexture,
        void* high3DTexture);
    bool UploadCpuCompletedCaptures();
    // Ordinary member function (not a lambda) so the completion-handler block
    // it defines captures capturedState/layerSerials directly out of a real
    // function parameter/local, rather than through a C++ lambda's own
    // reference capture -- nesting an Objective-C block's capture behind a
    // lambda's capture-by-reference produced a reproducible use-after-scope
    // crash (EXC_BAD_ACCESS in the async completion callback). textureRaw is
    // an id<MTLTexture> passed as void* so this declaration stays usable from
    // non-Objective-C++ includers of this header; see the .inc definition.
    bool UploadCaptureTexture(
        std::shared_ptr<MetalCaptureState> capturedState,
        void* textureRaw,
        unsigned long slice,
        const uint16_t* source,
        unsigned long nativeWidth,
        unsigned long nativeHeight,
        std::vector<int> affectedLayers);
    // Returns an idle MetalCaptureState::StagingBuffer* (opaque here so this
    // header stays free of the nested type's Objective-C members), or
    // nullptr if every ring slot is busy. Declared as a member (rather than
    // a free function) because MetalCaptureState is a private nested type;
    // see the .inc definition for the concrete return type.
    void* AcquireStagingSlot(MetalCaptureState& state, unsigned long requiredBytes);
    [[nodiscard]] bool MetalCaptureReady() const;
    [[nodiscard]] bool MetalCaptureFrameHadCapture() const;
    [[nodiscard]] bool MetalCaptureFrameSupported() const;
    [[nodiscard]] bool MetalCaptureResourcesCoherent() const;
    [[nodiscard]] void* GetMetalCapture128Texture() const;
    [[nodiscard]] void* GetMetalCapture256Texture() const;

    void CaptureMetalVisible3DFrame();
    void ComposeMetalVisibleOutput();
    // MELONPRIME_METAL_FRAME_SNAPSHOT_V1
};

} // namespace melonDS

#endif // MELONPRIME_ENABLE_METAL
#endif // GPU_METAL_H
