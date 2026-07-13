#pragma once

#if !defined(MELONPRIME_DS) || !defined(MELONPRIME_ENABLE_VULKAN)
#error "GPU_Vulkan.h is owned by the complete MelonPrime Vulkan build gate"
#endif

// MELONPRIME_VULKAN_RENDERER_SHELL_V1

#include "GPU_Soft.h"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace melonDS
{

struct VulkanRendererShellContract
{
    const char* ModeName = nullptr;
    bool ComputeSelected = false;
    bool UsesSoftwareCorrectnessBaseline = true;
    bool NativeVulkanRasterBootstrapAvailable = true;
    bool NativeVulkanClearPlaneBootstrapAvailable = true;
    bool NativeVulkanClearBitmapBootstrapAvailable = true;
    bool NativeVulkanVertexUploadBootstrapAvailable = true;
    bool NativeVulkanPolygonBatchBootstrapAvailable = true;
    bool NativeVulkanOpaquePipelineBootstrapAvailable = true;
    bool NativeVulkanTranslucentPipelineBootstrapAvailable = true;
    bool NativeVulkanShadowPipelineBootstrapAvailable = true;
    bool NativeVulkanToonHighlightContractAvailable = true;
    bool NativeVulkanToonHighlightShaderAbiAvailable = true;
    bool NativeVulkanToonHighlightDescriptorRuntimeAvailable = true;
    bool NativeVulkanToonHighlightGpuDrawAvailable = true;
    bool NativeVulkanTextureSamplingBootstrapAvailable = true;
    bool NativeVulkanTexturedPolygonBootstrapAvailable = true;
    bool NativeVulkanTextureCacheBootstrapAvailable = true;
    bool NativeVulkanTextureDecodeBootstrapAvailable = true;
    bool NativeVulkanTextureUploadRingAvailable = true;
    bool NativeVulkanPhase8SubsystemComplete = true;
    bool NativeVulkanSoftware2DUploadFinalAvailable = true;
    bool NativeVulkan2DCompositionAvailable = true;
    bool NativeVulkanFinalCompositionAvailable = true;
    bool NativeVulkanGpuResidentOutputAvailable = true;
    bool NativeVulkanPhase9SubsystemComplete = true;
    bool NativeVulkanOutputRingAvailable = true;
    bool NativeVulkanZeroCopyPresenterAvailable = true;
    bool NativeVulkanMultiWindowLeaseAvailable = true;
    bool NativeVulkanTimelinePresenterWaitAvailable = true;
    bool NativeVulkanPhase10SubsystemComplete = true;
    bool NativeVulkanRomIntegrationImplemented = false;
    bool NativeVulkanComputeStageGraphAvailable = true;
    bool NativeVulkanComputeSpecializationCacheAvailable = true;
    bool NativeVulkanComputeIndirectDispatchAvailable = true;
    bool NativeVulkanComputeBarrierGraphAvailable = true;
    bool NativeVulkanComputeHiresCoordinatesAvailable = true;
    bool NativeVulkanComputeVisibleOutputAvailable = true;
    bool NativeVulkanPhase11SubsystemComplete = true;
    bool NativeVulkanComputeRomVisible = false;
    bool NativeVulkanCapabilityAwareUiAvailable = true;
    bool NativeVulkanHardwareConfigMigrationAvailable = true;
    bool NativeVulkanLocalizedUiAvailable = true;
    bool NativeVulkanPhase12UiComplete = true;
    bool NativeVulkanPhase13FramePacingComplete = true;
    bool NativeVulkanPhase13DeviceLossFallbackComplete = true;
    bool NativeVulkanPhase13PipelineCacheComplete = true;
    bool NativeVulkanPhase13StabilityComplete = true;
    bool NativeVulkanRomScaleCompatibilityBridge = true;
    bool NativeVulkanCursorContainerSync = true;
    bool NativeVulkan3DImplemented = false;
    u32 ContractVersion = 24;
};

VulkanRendererShellContract DescribeVulkanRendererShell(bool computeSelected) noexcept;

// MELONPRIME_VULKAN_ANDROIDSTYLE_COMPAT_FRAME_V2
// Immutable frame handed from EmuThread to the Vulkan presenter. One screen may
// be compatibility-upscaled while the non-3D screen remains native 256x192.
struct VulkanCompatibilityFrame
{
    std::vector<u32> Top;
    std::vector<u32> Bottom;
    int TopWidth = 256;
    int TopHeight = 192;
    int BottomWidth = 256;
    int BottomHeight = 192;
    int RequestedScale = 1;
    int BuiltScale = 1;
    int EngineAScreen = 0;
    u64 FrameSerial = 0;
};

// Phase 6 establishes the Vulkan renderer identity and lifecycle while keeping
// Software 2D/3D/capture/CPU-BGRA output as the correctness source. Phase 7.1
// proves a native offscreen graphics pipeline; Phase 7.2 adds typed plain
// clear-plane targets; Phase 7.3 adds the repeated VRAM clear-bitmap pass;
// Phase 7.4 adds packed vertex/index/polygon upload; Phase 7.5 adds ordered,
// adjacent-only pipeline batches; Phase 7.6 adds an untextured opaque Vulkan
// draw bootstrap; Phase 7.7 adds untextured translucent blend/depth/stencil;
// Phase 7.8 adds two-stage shadow mask/reject/blend. ROM Renderer3D integration
// is still intentionally absent.
class VulkanRenderer final : public SoftRenderer
{
public:
    explicit VulkanRenderer(melonDS::NDS& nds, bool useComputeRenderer = false) noexcept;
    ~VulkanRenderer() override;

    bool Init() override;
    void Reset() override;
    void Stop() override;
    void PreSavestate() override;
    void PostSavestate() override;
    void SetRenderSettings(RendererSettings& settings) override;
    void DrawScanline(u32 line) override;
    void SwapBuffers() override;
    RendererOutput GetOutput() override;
    RendererOutputLease AcquireOutputLease() override;

    [[nodiscard]] std::shared_ptr<const VulkanCompatibilityFrame>
        AcquireCompatibilityFrame() const;

    [[nodiscard]] bool IsComputeRendererSelected() const noexcept
    {
        return ComputeRendererSelected;
    }

    [[nodiscard]] int GetRecordedScaleFactor() const noexcept
    {
        return ScaleFactor;
    }

    [[nodiscard]] bool GetRecordedBetterPolygons() const noexcept
    {
        return BetterPolygons;
    }

    [[nodiscard]] u64 GetFrameSerialForDiagnostics() const noexcept
    {
        return FrameSerial;
    }

    [[nodiscard]] u64 GetOutputGenerationForDiagnostics() const noexcept
    {
        return OutputGeneration;
    }

    // MELONPRIME_VULKAN_NATIVE_RASTER_P8_V1
    // Called while EmuInstance::renderLock is held. This copies the native
    // software-correctness 3D plane used only as the ownership reference for
    // GPU-resident high-resolution Vulkan composition.
    [[nodiscard]] bool CopyNative3DForPresenter(
        std::vector<u32>& output) const;

private:
    void AdvanceOutputGeneration() noexcept;
    void RebuildHighResolutionOutput();
    void ClearHighResolutionOutput() noexcept;
    void OnRendered3DLine(u32 line, const u32* pixels) noexcept override;

    bool ComputeRendererSelected = false;
    bool Initialized = false;
    int ScaleFactor = 1;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;
    static constexpr std::size_t CompatibilityFrameSlotCount = 3;
    static constexpr std::size_t NativePixelCount = 256u * 192u;

    std::vector<u32> Native3DFrame;
    std::array<std::uint8_t, NativePixelCount> Native3DVisible{};
    std::array<u32, NativePixelCount> Native3DBgra{};

    mutable std::mutex HighResolutionMutex;
    std::array<std::shared_ptr<VulkanCompatibilityFrame>,
        CompatibilityFrameSlotCount> CompatibilityFrames{};
    std::array<bool, CompatibilityFrameSlotCount>
        CompatibilityFrameProducerBusy{};
    std::size_t NextCompatibilityFrameSlot = 0;
    std::shared_ptr<const VulkanCompatibilityFrame>
        PublishedCompatibilityFrame;
    u64 LatestCompatibilityFrameSerial = 0;
    u64 DroppedCompatibilityFrames = 0;

    u64 FrameSerial = 0;
    u64 OutputGeneration = 1;
};

} // namespace melonDS
