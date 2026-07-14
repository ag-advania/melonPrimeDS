/*
    Copyright 2016-2025 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>
#include <volk.h>

#include "GPU3D.h"
#include "GPU3D_AcceleratedFrontend.h"
#include "GPU3D_TexcacheVulkan.h"
#include "VulkanPerfStats.h"

namespace melonDS
{
class GPU;

// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
// GPU-resident input prepared for the next Sapphire-style composition pass.
// The buffer is CPU-produced 2D metadata uploaded to Vulkan; no Vulkan image is
// read back to the CPU.
struct SapphireVulkanCompositionInput
{
    VkImage Source3DImage = VK_NULL_HANDLE;
    VkImageView Source3DImageView = VK_NULL_HANDLE;
    VkBuffer Structured2DBuffer = VK_NULL_HANDLE;
    VkDeviceSize Structured2DBufferSize = 0;
    u32 PackedStrideWords = 0;
    u32 ScreenCount = 0;
    u32 NativeWidth = 0;
    u32 NativeHeight = 0;
    u32 Source3DWidth = 0;
    u32 Source3DHeight = 0;
    u64 FrameSerial = 0;
    bool ScreenSwap = false;
    bool Valid = false;
};

// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
// GPU-only resource set consumed by the upcoming final composition dispatch.
// Resources are allocated and descriptors are populated in A4; no image is
// copied back to the CPU and no final shader dispatch is claimed yet.
struct SapphireVulkanCompositionResources
{
    VkImage OutputImage = VK_NULL_HANDLE;
    VkImageView OutputImageView = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkFormat Format = VK_FORMAT_UNDEFINED;
    u32 Width = 0;
    u32 Height = 0;
    u32 Layers = 0;
    u64 FrameSerial = 0;
    bool ResourcesReady = false;
    bool DescriptorsReady = false;
};

// MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
// Command-recording context and push-constant ABI for the final GPU compositor.
// A5 intentionally does not claim the final shader pipeline or dispatch.
struct SapphireCompositionPushConstants
{
    u32 OutputWidth = 0;
    u32 OutputHeight = 0;
    u32 Scale = 1;
    u32 RendererWidth = 0;
    u32 RendererHeight = 0;
    u32 PackedStride = 0;
    u32 ScreenSwap = 0;
    u32 Filtering = 0;
    u32 PreviousTopSourceValid = 0;
    u32 PreviousBottomSourceValid = 0;
    u32 CaptureSourceValid = 0;
    u32 CaptureSourceScreenSwapValid = 0;
    u32 CaptureSourceScreenSwap = 0;
    u32 LiveSourceScreenSwap = 0;
    u32 Class4VramStructuredPair = 0;
    u32 Class4NoAboveVramStructuredPair = 0;
    u32 Class4PreservePackedVramValid = 0;
    u32 Class4PreservePackedVramScreenSwap = 0;
    u32 TopStructuredHandoffNoCurrent3d = 0;
    u32 BottomStructuredHandoffNoCurrent3d = 0;
    u32 TopStructuredHandoffSuppress3d = 0;
    u32 BottomStructuredHandoffSuppress3d = 0;
};
static_assert(sizeof(SapphireCompositionPushConstants)
    == VulkanStructuredControlAbi::CompositorPushConstantBytes);

struct SapphireVulkanCompositionCommandContext
{
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    u64 FrameSerial = 0;
    bool Ready = false;
};

// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_EXACT_ABI_A7
// Exact descriptor and push-constant ABI of Sapphire's VulkanCompositor shader.
// Pipeline creation and vkCmdDispatch remain intentionally disabled until A4's
// output/history/capture resources are migrated to these exact image2D contracts.
struct SapphireCompositionDescriptorAbi
{
    static constexpr u32 OutputImageBinding = MP_VK_COMPOSITOR_OUTPUT_IMAGE_BINDING;
    static constexpr u32 Current3DImageBinding = MP_VK_COMPOSITOR_CURRENT_3D_BINDING;
    static constexpr u32 TopPackedBufferBinding = MP_VK_COMPOSITOR_TOP_PACKED_BINDING;
    static constexpr u32 BottomPackedBufferBinding = MP_VK_COMPOSITOR_BOTTOM_PACKED_BINDING;
    static constexpr u32 PreviousTop3DImageBinding = MP_VK_COMPOSITOR_PREVIOUS_TOP_3D_BINDING;
    static constexpr u32 Capture3DBufferBinding = MP_VK_COMPOSITOR_CAPTURE_3D_BINDING;
    static constexpr u32 PreviousBottom3DImageBinding = MP_VK_COMPOSITOR_PREVIOUS_BOTTOM_3D_BINDING;
    static constexpr u32 BindingCount = MP_VK_COMPOSITOR_BINDING_COUNT;
    static constexpr u32 PushConstantBytes = MP_VK_COMPOSITOR_PUSH_CONSTANT_BYTES;
};
static_assert(SapphireCompositionDescriptorAbi::BindingCount
    == VulkanStructuredControlAbi::CompositorBindingCount);
static_assert(SapphireCompositionDescriptorAbi::PushConstantBytes
    == VulkanStructuredControlAbi::CompositorPushConstantBytes);

// MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_SHADER_MODULE_A6
// The exact Sapphire compositor SPIR-V is now owned by the core renderer and
// materialized as a VkShaderModule. The exact A7 descriptor and push-constant ABI is declared above; pipeline
// creation and dispatch remain deferred until A4 resources match that ABI.
struct SapphireVulkanCompositionShaderModule
{
    VkShaderModule Module = VK_NULL_HANDLE;
    size_t SpirvBytes = 0;
    u64 FrameSerial = 0;
    bool Ready = false;
};

class VulkanRenderer3D : public Renderer3D
{
public:
    enum class BackendMode : u8
    {
        GraphicsHardware = 1,
    };

    static std::unique_ptr<VulkanRenderer3D> New(melonDS::GPU3D& gpu3D) noexcept;

    explicit VulkanRenderer3D(melonDS::GPU3D& gpu3D) noexcept;
    ~VulkanRenderer3D() override;

    // MELONPRIME_VULKAN_REFERENCE_PORT_V0_V5_V1 target-API adapters
    // MELONPRIME_SAPPHIRE_VULKAN_FRAME_LIFECYCLE_A1
    bool Init() override { return EnsureVulkanReadyForValidation(); }
    void Reset() override { Reset(GPU); }
    void RenderFrame() override
    {
        BeginCaptureFrame();
        SetCaptureScreenSwapHint(GPU.ScreenSwap);
        RenderFrame(GPU);
    }
    void FinishRendering() override
    {
        PrepareCaptureFrame();
        Blit(GPU);
    }
    void RestartFrame() override { RestartFrame(GPU); }
    void VCount144() override { VCount144(GPU); }
    void Blit() override { Blit(GPU); }
    void StopRenderer() override { Stop(GPU); }

    // MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
    void BeginStructured2DFrame(u64 frameSerial) override;
    void SubmitStructured2DLine(const SapphireStructured2DLine& line) override;
    void EndStructured2DFrame(u64 frameSerial, bool screenSwap) override;
    [[nodiscard]] bool HasCompleteStructured2DFrame() const noexcept
    {
        return Structured2DFrameValid;
    }
    [[nodiscard]] u64 GetStructured2DFrameSerial() const noexcept
    {
        return Structured2DFrameSerial;
    }
    [[nodiscard]] bool GetStructured2DScreenSwap() const noexcept
    {
        return Structured2DScreenSwap;
    }
    [[nodiscard]] const u32* GetStructured2DPlane0() const noexcept
    {
        return Structured2DPlane0.data();
    }
    [[nodiscard]] const u32* GetStructured2DPlane1() const noexcept
    {
        return Structured2DPlane1.data();
    }
    [[nodiscard]] const u32* GetStructured2DControl() const noexcept
    {
        return Structured2DControl.data();
    }
    [[nodiscard]] const u32* GetStructured2DNativeFinal() const noexcept
    {
        return Structured2DNativeFinal.data();
    }
    [[nodiscard]] const u32* GetStructured2DLineMeta() const noexcept
    {
        return Structured2DLineMeta.data();
    }
    [[nodiscard]] const u32* GetStructured2DLineState() const noexcept
    {
        return Structured2DLineState.data();
    }

    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
    [[nodiscard]] SapphireVulkanCompositionInput
        GetSapphireVulkanCompositionInput() const noexcept
    {
        SapphireVulkanCompositionInput input{};
        input.Source3DImage = ColorImage;
        input.Source3DImageView = ColorImageView;
        input.Structured2DBuffer = Structured2DGpuBuffer;
        input.Structured2DBufferSize = Structured2DGpuBufferSize;
        input.PackedStrideWords = Structured2DPackedStrideWords;
        input.ScreenCount = 2;
        input.NativeWidth = 256;
        input.NativeHeight = 192;
        input.Source3DWidth = ColorImageWidth;
        input.Source3DHeight = ColorImageHeight;
        input.FrameSerial = Structured2DGpuFrameSerial;
        input.ScreenSwap = Structured2DScreenSwap;
        input.Valid = Structured2DFrameValid
            && Structured2DGpuBufferValid
            && Structured2DGpuFrameSerial == Structured2DFrameSerial
            && ColorImageInitialized
            && ColorImage != VK_NULL_HANDLE
            && ColorImageView != VK_NULL_HANDLE;
        return input;
    }

    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
    [[nodiscard]] SapphireVulkanCompositionResources
        GetSapphireVulkanCompositionResources() const noexcept
    {
        SapphireVulkanCompositionResources resources{};
        resources.OutputImage = SapphireCompositionImage;
        resources.OutputImageView = SapphireCompositionImageView;
        resources.DescriptorSet = SapphireCompositionDescriptorSet;
        resources.PipelineLayout = SapphireCompositionPipelineLayout;
        resources.Format = SapphireCompositionFormat;
        resources.Width = SapphireCompositionWidth;
        resources.Height = SapphireCompositionHeight;
        resources.Layers = 2;
        resources.FrameSerial = SapphireCompositionFrameSerial;
        resources.ResourcesReady = SapphireCompositionResourcesReady;
        resources.DescriptorsReady = SapphireCompositionDescriptorsReady;
        return resources;
    }

    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
    [[nodiscard]] SapphireVulkanCompositionCommandContext
        GetSapphireVulkanCompositionCommandContext() const noexcept
    {
        SapphireVulkanCompositionCommandContext context{};
        context.CommandPool = SapphireCompositionCommandPool;
        context.CommandBuffer = SapphireCompositionCommandBuffer;
        context.Fence = SapphireCompositionFence;
        context.PipelineLayout = SapphireCompositionPipelineLayout;
        context.FrameSerial = SapphireCompositionCommandFrameSerial;
        context.Ready = SapphireCompositionCommandContextReady;
        return context;
    }

    // MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_SHADER_MODULE_A6
    [[nodiscard]] SapphireVulkanCompositionShaderModule
        GetSapphireVulkanCompositionShaderModule() const noexcept
    {
        SapphireVulkanCompositionShaderModule shader{};
        shader.Module = SapphireCompositionShaderModule;
        shader.SpirvBytes = SapphireCompositionShaderSpirvBytes;
        shader.FrameSerial = SapphireCompositionShaderFrameSerial;
        shader.Ready = SapphireCompositionShaderModuleReady;
        return shader;
    }
    void SetRenderSettings(bool threaded, bool betterPolygons, int scale, bool hiresCoordinates) override
    {
        (void)hiresCoordinates;
        SetRenderSettings(
            threaded, betterPolygons, scale,
            false, false, 0.0f, 0.0f, true, false, false, GPU);
    }


    void Reset(melonDS::GPU& gpu);
    void VCount144(melonDS::GPU& gpu);
    void RenderFrame(melonDS::GPU& gpu);
    void RestartFrame(melonDS::GPU& gpu);
    u32* GetLine(int line) override;

    void SetupAccelFrame();
    void PrepareCaptureFrame();
    void BeginCaptureFrame();
    void SetCaptureScreenSwapHint(bool screenSwap);
    [[nodiscard]] bool UsesStructured2DMetadata() const noexcept override { return true; }
    void Blit(const melonDS::GPU& gpu);
    void Stop(const melonDS::GPU& gpu);

    void SetRenderSettings(
        bool threaded,
        bool betterPolygons,
        int scale,
        bool useSimplePipeline,
        bool conservativeCoverageEnabled,
        float conservativeCoveragePx,
        float conservativeCoverageDepthBias,
        bool conservativeCoverageApplyRepeat,
        bool conservativeCoverageApplyClamp,
        bool debug3dClearMagenta,
        melonDS::GPU& gpu) noexcept;

    void SetThreaded(bool threaded, melonDS::GPU& gpu) noexcept;
    [[nodiscard]] bool IsThreaded() const noexcept;

    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }
    [[nodiscard]] bool UsesBetterPolygons() const noexcept { return BetterPolygons; }
    [[nodiscard]] bool UsesSimplePipeline() const noexcept { return UseSimplePipeline; }
    [[nodiscard]] bool IsCoverageFixEnabled() const noexcept { return CoverageFixEnabled; }
    [[nodiscard]] float GetCoverageFixPx() const noexcept { return CoverageFixPx; }
    [[nodiscard]] float GetCoverageFixDepthBias() const noexcept { return CoverageFixDepthBias; }
    [[nodiscard]] bool IsCoverageFixRepeatEnabled() const noexcept { return CoverageFixApplyRepeat; }
    [[nodiscard]] bool IsCoverageFixClampEnabled() const noexcept { return CoverageFixApplyClamp; }
    [[nodiscard]] float GetPassiveCoverageFixRepeatPx() const noexcept { return PassiveCoverageFixRepeatPx; }
    [[nodiscard]] bool IsDebug3dClearMagentaEnabled() const noexcept { return Debug3dClearMagenta; }
    [[nodiscard]] size_t GetAsyncRenderContextCount() const noexcept { return AsyncRenderContextCount; }
    [[nodiscard]] bool WaitsForReadbackSourceOnly() const noexcept { return true; }
    [[nodiscard]] bool GetCurrentRenderScreenSwap() const noexcept { return CurrentRenderScreenSwap; }
    [[nodiscard]] bool IsCurrentCaptureScreenSwapHintValid() const noexcept { return HasCurrentCaptureScreenSwapHint; }
    [[nodiscard]] bool GetCurrentCaptureScreenSwapHint() const noexcept { return CurrentCaptureScreenSwapHint; }
    [[nodiscard]] bool IsLastValidExactCaptureAvailable() const noexcept { return HasLastValidExactCapture; }
    [[nodiscard]] bool GetLastValidExactCaptureScreenSwap() const noexcept { return LastValidExactCaptureScreenSwap; }
    [[nodiscard]] bool EnsureVulkanReadyForValidation();
    [[nodiscard]] bool HasColorTarget() const noexcept { return ColorImage != VK_NULL_HANDLE && ColorImageView != VK_NULL_HANDLE; }
    [[nodiscard]] bool IsColorTargetInitialized() const noexcept { return ColorImageInitialized; }
    [[nodiscard]] VkImage GetColorTargetImage() const noexcept { return ColorImage; }
    [[nodiscard]] VkImageView GetColorTargetImageView() const noexcept { return ColorImageView; }
    [[nodiscard]] u32 GetColorTargetWidth() const noexcept { return ColorImageWidth; }
    [[nodiscard]] u32 GetColorTargetHeight() const noexcept { return ColorImageHeight; }
    [[nodiscard]] std::vector<u32> CaptureColorTargetForDebug();
    [[nodiscard]] std::vector<u32> CaptureTopDepthForDebug();
    [[nodiscard]] std::vector<u32> CaptureTopAttrForDebug();
    [[nodiscard]] std::vector<u32> CaptureTopCoverageForDebug();
    void requestPostFastForwardDrain();
    void SetBackendMode(BackendMode mode) noexcept;
    void InvalidatePresentationState(bool discardColorTarget) noexcept;
    [[nodiscard]] BackendMode GetRequestedBackendMode() const noexcept { return RequestedBackendMode; }
    [[nodiscard]] BackendMode GetResolvedRequestedBackendMode() const noexcept { return resolveRequestedBackendMode(); }
    [[nodiscard]] BackendMode GetActiveBackendMode() const noexcept { return ActiveBackendMode; }
    [[nodiscard]] static const char* backendModeName(BackendMode mode) noexcept;

private:
    class IVulkan3DBackend;
    class SimpleGraphicsBackend;

    static constexpr u32 MaxTextureDescriptors = 128;
    static constexpr u32 MaxActiveTextureDescriptors = MaxTextureDescriptors - 1;
    static constexpr u32 FallbackTextureDescriptorIndex = MaxTextureDescriptors - 1;
    static constexpr u32 ToonTableEntryCount = 32;

    enum class RasterDispatchPath : u8
    {
        DirectTiles = 0,
        LegacyWorklist = 1,
    };

    enum class RasterExecutionProfile : u8
    {
        AdrenoCpuDense = 0,
        AdrenoCpuSparse = 1,
        MaliDenseScan = 2,
        MaliCpuDense = 3,
        GeneralNonUniform = 4,
        LegacyFallback = 5,
        Count = 6,
    };

    enum class RasterSceneMode : u8
    {
        DenseNoBoundary = 0,
        DenseBoundary = 1,
        SparseActive = 2,
        Count = 3,
    };

    enum class RasterTileLoopMode : u8
    {
        DenseGroupList = 0,
        SparseActive = 1,
        LegacyWorklist = 2,
        Count = 3,
    };

    enum class TextureSamplingPath : u8
    {
        BaseSingleDescriptor = 0,
        CompatDynamicUniform = 1,
        NonUniform = 2,
    };

    enum class CapturePathMode : u8
    {
        Disabled = 0,
        CaptureLineExport = 1,
        FallbackReadback = 2,
        Count = 3,
    };

    struct DescriptorSetCache
    {
        bool Ready = false;
        VkImageView ColorImageView = VK_NULL_HANDLE;
        VkBuffer TriangleBuffer = VK_NULL_HANDLE;
        VkImageView FallbackTextureView = VK_NULL_HANDLE;
        VkSampler FallbackTextureSampler = VK_NULL_HANDLE;
        VkBuffer ResultBuffer = VK_NULL_HANDLE;
        VkBuffer BinMaskBuffer = VK_NULL_HANDLE;
        VkBuffer GroupListBuffer = VK_NULL_HANDLE;
        VkBuffer ToonBuffer = VK_NULL_HANDLE;
        VkBuffer SpanSetupBuffer = VK_NULL_HANDLE;
        VkBuffer WorkOffsetBuffer = VK_NULL_HANDLE;
        VkBuffer CaptureLineBuffer = VK_NULL_HANDLE;
        std::array<VkDescriptorImageInfo, MaxTextureDescriptors> TextureInfos{};
    };

    struct GraphicsDescriptorSetCache
    {
        bool Ready = false;
        VkBuffer TriangleBuffer = VK_NULL_HANDLE;
        VkBuffer ToonBuffer = VK_NULL_HANDLE;
        VkBuffer ClearBuffer = VK_NULL_HANDLE;
        VkImageView AttrImageView = VK_NULL_HANDLE;
        VkImageView DepthImageView = VK_NULL_HANDLE;
        VkSampler AttachmentSampler = VK_NULL_HANDLE;
        std::array<VkDescriptorImageInfo, MaxTextureDescriptors> TextureInfos{};
    };

    struct RenderContext
    {
        VkCommandPool CommandPool = VK_NULL_HANDLE;
        VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
        VkFence FrameFence = VK_NULL_HANDLE;
        VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet GraphicsDescriptorSet = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, MaxTextureDescriptors> SingleTextureDescriptorSets{};
        VkBuffer TriangleBuffer = VK_NULL_HANDLE;
        VkDeviceMemory TriangleMemory = VK_NULL_HANDLE;
        VkDeviceSize TriangleBufferSize = 0;
        void* TriangleMapped = nullptr;
        VkBuffer GraphicsVertexBuffer = VK_NULL_HANDLE;
        VkDeviceMemory GraphicsVertexMemory = VK_NULL_HANDLE;
        VkDeviceSize GraphicsVertexBufferSize = 0;
        void* GraphicsVertexMapped = nullptr;
        VkBuffer BinMaskBuffer = VK_NULL_HANDLE;
        VkDeviceMemory BinMaskMemory = VK_NULL_HANDLE;
        VkDeviceSize BinMaskBufferSize = 0;
        void* BinMaskMapped = nullptr;
        VkBuffer GroupListBuffer = VK_NULL_HANDLE;
        VkDeviceMemory GroupListMemory = VK_NULL_HANDLE;
        VkDeviceSize GroupListBufferSize = 0;
        void* GroupListMapped = nullptr;
        VkBuffer SpanSetupBuffer = VK_NULL_HANDLE;
        VkDeviceMemory SpanSetupMemory = VK_NULL_HANDLE;
        VkDeviceSize SpanSetupBufferSize = 0;
        void* SpanSetupMapped = nullptr;
        VkBuffer WorkOffsetBuffer = VK_NULL_HANDLE;
        VkDeviceMemory WorkOffsetMemory = VK_NULL_HANDLE;
        VkDeviceSize WorkOffsetBufferSize = 0;
        void* WorkOffsetMapped = nullptr;
        VkBuffer ToonBuffer = VK_NULL_HANDLE;
        VkDeviceMemory ToonMemory = VK_NULL_HANDLE;
        VkDeviceSize ToonBufferSize = 0;
        void* ToonMapped = nullptr;
        VkBuffer ClearBuffer = VK_NULL_HANDLE;
        VkDeviceMemory ClearMemory = VK_NULL_HANDLE;
        VkDeviceSize ClearBufferSize = 0;
        void* ClearMapped = nullptr;
        VkBuffer CaptureLineBuffer = VK_NULL_HANDLE;
        VkDeviceMemory CaptureLineMemory = VK_NULL_HANDLE;
        VkDeviceSize CaptureLineBufferSize = 0;
        void* CaptureLineMapped = nullptr;
        VkQueryPool TimestampQueryPool = VK_NULL_HANDLE;
        bool TimestampPending = false;
        DescriptorSetCache DescriptorCache{};
        std::array<DescriptorSetCache, MaxTextureDescriptors> SingleTextureDescriptorCaches{};
        GraphicsDescriptorSetCache GraphicsDescriptorCache{};
    };

    struct RasterPushConstants
    {
        u32 width;
        u32 height;
        u32 clearColor;
        u32 clearDepth;
        u32 triangleCount;
        u32 dispCnt;
        u32 alphaRef;
        u32 fogColor;
        u32 fogOffset;
        u32 fogShift;
        u32 clearAttr;
        u32 fogDensityPacked[9];
        u32 edgeColorPacked[8];
        u32 variantKey;
        u32 passIndex;
        u32 triangleBase;
        u32 depthBlendMode;
    };
    static_assert(sizeof(RasterPushConstants) == 128u, "RasterPushConstants must fit maxPushConstantsSize=128");

    struct TriangleGpu
    {
        float x0;
        float y0;
        float z0;
        float w0;
        float x1;
        float y1;
        float z1;
        float w1;
        float x2;
        float y2;
        float z2;
        float w2;
        float u0;
        float v0;
        float u1;
        float v1;
        float u2;
        float v2;
        u32 yBounds;
        u32 texLayer;
        u32 color0Rgba8;
        u32 color1Rgba8;
        u32 color2Rgba8;
        u32 flags;
        u32 texArrayIndex;
        u32 texWidth;
        u32 texHeight;
        u32 texParam;
        u32 polyAttr;
        u32 variantKey;
    };

    struct GraphicsVertexGpu
    {
        float x;
        float y;
        float z;
        float reciprocalW;
        float u;
        float v;
        u32 colorRgba8;
        u32 flags;
        u32 texLayer;
        u32 texArrayIndex;
        u32 texWidth;
        u32 texHeight;
        u32 texParam;
        u32 polyAttr;
    };

    struct SpanSetupGpu
    {
        float minX;
        float minY;
        float maxX;
        float maxY;
        u32 yMin;
        u32 yMax;
        u32 variantKey;
        u32 valid;
        float edgeInv0;
        float edgeInv1;
        float edgeInv2;
    };

    struct GraphicsPolygonDraw
    {
        u32 firstTriangle = 0;
        u32 triangleCount = 0;
        u32 polyAttr = 0;
        u32 flags = 0;
        u32 firstVertex = 0;
        u32 vertexCount = 0;
        u32 firstEdgeIndex = 0;
        u32 edgeIndexCount = 0;
        u32 edgeColorOverrideMask = 0;
        u32 edgeColorOverridePacked = 0;
    };

    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_INPUT_A3
    static constexpr u32 Structured2DPackedStrideWords = (256u * 4u) + 2u;
    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_RESOURCES_A4
    // MELONPRIME_SAPPHIRE_VULKAN_GPU_COMPOSITION_COMMAND_A5
    // MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_SHADER_MODULE_A6
    bool ensureSapphireCompositionShaderModule();
    void destroySapphireCompositionShaderModule() noexcept;

    bool ensureSapphireCompositionCommandContext();
    void destroySapphireCompositionCommandContext() noexcept;

    bool ensureSapphireCompositionResources();
    bool updateSapphireCompositionDescriptors();
    void destroySapphireCompositionResources() noexcept;

    bool ensureStructured2DGpuBuffer();
    void destroyStructured2DGpuBuffer() noexcept;
    void packStructured2DFrame();
    bool uploadStructured2DFrameToGpu();

    bool ensureInitialized();
    void destroyVulkan();

    bool createCommandObjects();
    bool createCommandObjects(VkCommandPool& commandPool, VkCommandBuffer& commandBuffer);
    bool createSyncObjects();
    bool createFence(VkFence& fence);
    bool createTimestampQueryPool(VkQueryPool& queryPool);
    bool createDescriptorObjects();
    bool createGraphicsDescriptorObjects();
    bool createComputePipeline();
    bool createGraphicsPipelines();
    bool createPipelineCache(TextureSamplingPath samplingPath);
    void savePipelineCache();
    std::string buildPipelineCacheFileName(TextureSamplingPath samplingPath) const;

    bool ensureRenderTarget(u32 width, u32 height);
    void destroyRenderTarget();
    bool ensureTriangleBuffer(RenderContext* context, size_t triangleCount);
    void destroyTriangleBuffer(RenderContext* context);
    bool ensureGraphicsVertexBuffer(RenderContext* context, size_t vertexCount);
    void destroyGraphicsVertexBuffer(RenderContext* context);
    bool ensureGraphicsSceneVertexBuffer(size_t vertexCount);
    void destroyGraphicsSceneVertexBuffer();
    bool ensureGraphicsEdgeIndexBuffer(size_t indexCount);
    void destroyGraphicsEdgeIndexBuffer();
    bool ensureCpuSpanSetupBuffer(RenderContext& context, size_t triangleCount);
    void destroyCpuSpanSetupBuffer(RenderContext& context);
    bool ensureCpuBinBuffers(RenderContext& context, size_t triangleCount, u32 width, u32 height);
    void destroyCpuBinBuffers(RenderContext& context);
    bool ensureCpuWorkOffsetBuffer(RenderContext& context, u32 width, u32 height, size_t triangleCount);
    void destroyCpuWorkOffsetBuffer(RenderContext& context);
    bool ensureResultBuffer(u32 width, u32 height);
    void destroyResultBuffer();
    bool ensureBinMaskBuffer(size_t triangleCount, u32 width, u32 height);
    void destroyBinMaskBuffer();
    bool ensureGroupListBuffer(size_t triangleCount, u32 width, u32 height);
    void destroyGroupListBuffer();
    bool ensureSpanSetupBuffer(size_t triangleCount);
    void destroySpanSetupBuffer();
    bool ensureWorkOffsetBuffer(u32 width, u32 height, size_t triangleCount);
    void destroyWorkOffsetBuffer();
    bool ensureToonBuffer(RenderContext* context);
    void destroyToonBuffer(RenderContext* context);
    bool updateToonBuffer(RenderContext* context, const u16* toonTable);
    bool ensureGraphicsClearBuffer(RenderContext* context);
    void destroyGraphicsClearBuffer(RenderContext* context);
    bool updateGraphicsClearBuffer(RenderContext* context, const melonDS::GPU& gpu);
    bool ensureCaptureLineBuffer(RenderContext* context);
    void destroyCaptureLineBuffer(RenderContext* context);
    void destroyAllCaptureLineBuffers();
    void resetCaptureLineState();
    void selectActiveCaptureLineBufferSlot(u32 slot);
    void syncActiveCaptureLineBufferSlot();
    void storeActiveCaptureLineBufferSlot();
    void clearRawReadbackState();
    bool finalizeCaptureLineFrame(bool blocking = true);
    bool finalizeCaptureReadback(bool blocking = true);
    bool createFallbackTexture();
    void destroyFallbackTexture();

    bool createReadbackBuffer(u32 width, u32 height);
    void destroyReadbackBuffer();
    bool ensureCaptureReadbackImage();
    void destroyCaptureReadbackImage();
    bool createResultReadbackBuffer();
    void destroyResultReadbackBuffer();
    bool readbackGraphicsAttrImageToCpu(std::vector<u32>& outAttrPixels);
    bool readbackGraphicsDepthImageToCpu(std::vector<u32>& outDepthPixels);

    void updateDescriptorSet(RenderContext* context, u32 singleTextureDescriptorIndex = FallbackTextureDescriptorIndex);
    bool updateCaptureExportDescriptorSet(RenderContext* context);
    void updateGraphicsDescriptorSet(RenderContext* context);
    static bool descriptorImageInfoEquals(const VkDescriptorImageInfo& lhs, const VkDescriptorImageInfo& rhs);
    VkDescriptorSet getDescriptorSet(RenderContext* context, u32 singleTextureDescriptorIndex) const;
    DescriptorSetCache& getDescriptorSetCache(RenderContext* context, u32 singleTextureDescriptorIndex);
    GraphicsDescriptorSetCache& getGraphicsDescriptorSetCache(RenderContext* context);
    void invalidateDescriptorSetCache(RenderContext* context);
    void invalidateAllDescriptorSetCaches();
    void invalidateGraphicsDescriptorSetCache(RenderContext* context);
    void invalidateAllGraphicsDescriptorSetCaches();
    [[nodiscard]] bool usesSingleDescriptorTexturePath() const noexcept;
    [[nodiscard]] u32 getTextureBindingDescriptorCount() const noexcept;
    [[nodiscard]] TextureSamplingPath resolveTextureSamplingPath() const noexcept;
    [[nodiscard]] static const char* textureSamplingPathName(TextureSamplingPath path) noexcept;
    [[nodiscard]] BackendMode resolveRequestedBackendMode() const noexcept;
    void refreshActiveBackendMode() noexcept;
    [[nodiscard]] static const char* rasterExecutionProfileName(RasterExecutionProfile profile) noexcept;
    [[nodiscard]] static const char* rasterSceneModeName(RasterSceneMode mode) noexcept;
    [[nodiscard]] static const char* rasterTileLoopModeName(RasterTileLoopMode mode) noexcept;
    [[nodiscard]] static const char* capturePathModeName(CapturePathMode mode) noexcept;
    u32 findMemoryType(u32 typeBits, VkMemoryPropertyFlags properties) const;
    bool tryAcquireRenderContext(RenderContext& context, bool countMisses = true);
    bool waitForRenderContext(RenderContext& context);
    RenderContext* tryAcquireReadyRenderContext() noexcept;
    bool waitForAllRenderContexts();
    bool waitForReadbackSource();
    bool waitForTextureCacheMutationSafePoint();
    bool waitForDeviceIdle(const char* reason);
    RenderContext& acquireNextRenderContext() noexcept;
    void consumeGpuTiming(RenderContext* context);
    void logPerformanceIfNeeded();
    bool useCpuTileBinning() const noexcept;
    bool prepareCpuTileBins(RenderContext& context, const RasterPushConstants& pushConstants);

    void WarmTextureCache(melonDS::GPU& gpu);
    void buildGraphicsTriangleList(melonDS::GPU& gpu);
    void buildTriangleList(melonDS::GPU& gpu);

    bool selectGraphicsDepthStencilFormat();
    bool dispatchRasterAndReadback(
        RenderContext* context,
        u32 rgbaColor,
        u32 clearDepth,
        u32 dispCnt,
        u32 alphaRef,
        u32 fogColor,
        u32 fogOffset,
        u32 fogShift,
        u32 clearAttr,
        const u8* fogDensityTable,
        const u16* edgeColorTable,
        const u16* toonTable,
        bool readbackToCpu,
        bool captureReadbackPath = false);
    bool dispatchGraphicsRasterAndReadback(
        RenderContext* context,
        u32 rgbaColor,
        u32 clearDepth,
        u32 dispCnt,
        u32 alphaRef,
        u32 fogColor,
        u32 fogOffset,
        u32 fogShift,
        u32 clearAttr,
        const u8* fogDensityTable,
        const u16* edgeColorTable,
        const u16* toonTable,
        bool readbackToCpu,
        bool captureReadbackPath = false);
    bool submitGraphicsCaptureExportForCurrentFrame();
    bool readbackColorTargetToCpu(bool capturePath = false);
    bool readbackResultBufferToCpu();
    bool copyReadyCaptureLineToLineCache();
    bool restoreLastValidExactCaptureToLineCache();
    void convertReadbackToLineCache();
    void fillLineCacheWithCaptureFallbackColor();
    u32 buildClearColorRgba8(const melonDS::GPU& gpu) const;
    void clearLineCache();
    void ResetActiveBackend(melonDS::GPU& gpu);
    void VCount144ActiveBackend(melonDS::GPU& gpu);
    void RenderFrameActiveBackend(melonDS::GPU& gpu);
    void RestartFrameActiveBackend(melonDS::GPU& gpu);
    u32* GetLineActiveBackend(int line);
    void SetupAccelFrameActiveBackend();
    void PrepareCaptureFrameActiveBackend();
    void BeginCaptureFrameActiveBackend();
    void BlitActiveBackend(const melonDS::GPU& gpu);
    void StopActiveBackend(const melonDS::GPU& gpu);
    IVulkan3DBackend& activeBackend() noexcept;
    void activateBackendMode(BackendMode mode) noexcept;

private:
    TexcacheVulkan Texcache;

    int ScaleFactor = 1;
    bool BetterPolygons = true;
    bool CoverageFixEnabled = false;
    float CoverageFixPx = 0.0f;
    float CoverageFixDepthBias = 0.0f;
    bool CoverageFixApplyRepeat = true;
    bool CoverageFixApplyClamp = false;
    float PassiveCoverageFixRepeatPx = 0.2f;
    bool Debug3dClearMagenta = false;
    bool Threaded = false;

    bool Initialized = false;
    bool InitFailed = false;
    bool HasCpuFrame = false;
    bool FrameIdentical = false;
    bool ContextAcquired = false;
    u32 LastSubmittedRenderPolygonCount = 0;

    VkInstance Instance = VK_NULL_HANDLE;
    VkPhysicalDevice PhysicalDevice = VK_NULL_HANDLE;
    VkDevice Device = VK_NULL_HANDLE;
    VkQueue Queue = VK_NULL_HANDLE;
    u32 QueueFamilyIndex = 0;

    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence FrameFence = VK_NULL_HANDLE;
    VkQueryPool TimestampQueryPool = VK_NULL_HANDLE;
    bool TimestampPending = false;

    VkDescriptorSetLayout DescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet DescriptorSet = VK_NULL_HANDLE;
    std::array<VkDescriptorSet, MaxTextureDescriptors> SingleTextureDescriptorSets{};
    DescriptorSetCache DescriptorCache{};
    std::array<DescriptorSetCache, MaxTextureDescriptors> SingleTextureDescriptorCaches{};
    VkDescriptorSetLayout GraphicsDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool GraphicsDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet GraphicsDescriptorSet = VK_NULL_HANDLE;
    GraphicsDescriptorSetCache GraphicsDescriptorCache{};
    TextureSamplingPath ActiveTextureSamplingPath = TextureSamplingPath::CompatDynamicUniform;
    BackendMode RequestedBackendMode = BackendMode::GraphicsHardware;
    BackendMode ActiveBackendMode = BackendMode::GraphicsHardware;
    bool UseSimplePipeline = true;
    std::unique_ptr<IVulkan3DBackend> SimpleGraphicsBackendInstance;
    RasterExecutionProfile ActiveRasterExecutionProfile = RasterExecutionProfile::LegacyFallback;
    RasterTileLoopMode ActiveRasterTileLoopMode = RasterTileLoopMode::DenseGroupList;
    CapturePathMode ActiveCapturePathMode = CapturePathMode::Disabled;
    VkPipelineLayout PipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout GraphicsPipelineLayout = VK_NULL_HANDLE;
    VkPipelineCache ComputePipelineCache = VK_NULL_HANDLE;
    std::string ComputePipelineCacheFile;
    VkPipeline InterpPipeline = VK_NULL_HANDLE;
    VkPipeline BinPipeline = VK_NULL_HANDLE;
    VkPipeline WorkOffsetsPipeline = VK_NULL_HANDLE;
    VkPipeline SortPipeline = VK_NULL_HANDLE;
    VkPipeline DepthBlendPipeline = VK_NULL_HANDLE;
    static constexpr u32 RasterSceneModeCount = static_cast<u32>(RasterSceneMode::Count);
    static constexpr u32 RasterWModeCount = 3;
    static constexpr u32 RasterShadeModeCount = 6;
    static constexpr u32 RasterTextureModeCount = 3;
    static constexpr u32 RasterTranslucencyModeCount = 3;
    static constexpr u32 RasterPipelineVariantCount =
        RasterSceneModeCount * RasterWModeCount * RasterShadeModeCount * RasterTextureModeCount * RasterTranslucencyModeCount;
    std::array<VkPipeline, RasterPipelineVariantCount> RasterPipelines{};
    static constexpr u32 FinalPipelineVariantCount = 8;
    std::array<VkPipeline, FinalPipelineVariantCount> FinalPipelines{};
    VkPipeline CaptureLineExportPipeline = VK_NULL_HANDLE;
    static constexpr u32 GraphicsWModeCount = 2;
    static constexpr u32 GraphicsDepthCompareModeCount = 2;
    static constexpr u32 GraphicsDepthWriteModeCount = 2;
    static constexpr u32 GraphicsFogWriteModeCount = 2;
    static constexpr u32 GraphicsAlphaBlendModeCount = 2;
    static constexpr u32 GraphicsOpaquePipelineCount = GraphicsWModeCount * GraphicsDepthCompareModeCount;
    static constexpr u32 GraphicsTranslucentPipelineCount =
        GraphicsWModeCount * GraphicsDepthCompareModeCount * GraphicsDepthWriteModeCount * GraphicsFogWriteModeCount * GraphicsAlphaBlendModeCount;
    static constexpr u32 GraphicsBgZeroTranslucentPipelineCount =
        GraphicsWModeCount * GraphicsDepthCompareModeCount * GraphicsDepthWriteModeCount * GraphicsFogWriteModeCount;
    static constexpr u32 GraphicsShadowMaskPipelineCount = GraphicsWModeCount;
    static constexpr u32 GraphicsShadowMaskBgZeroPipelineCount = GraphicsWModeCount;
    static constexpr u32 GraphicsShadowClearPipelineCount = GraphicsWModeCount * GraphicsDepthCompareModeCount;
    static constexpr u32 GraphicsShadowBlendBgZeroPipelineCount =
        GraphicsWModeCount * GraphicsDepthCompareModeCount * GraphicsDepthWriteModeCount * GraphicsFogWriteModeCount * GraphicsAlphaBlendModeCount;
    static constexpr u32 GraphicsShadowBlendPipelineCount =
        GraphicsWModeCount * GraphicsDepthCompareModeCount * GraphicsDepthWriteModeCount * GraphicsFogWriteModeCount * GraphicsAlphaBlendModeCount;
    static constexpr u32 GraphicsEdgeMarkPipelineCount = GraphicsWModeCount;
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaquePipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFragmentDepthPipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFastModulatePipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFastModulateToonPipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFastModulatePlainPipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFastModulateOpaqueAlphaToonPipelines{};
    std::array<VkPipeline, GraphicsOpaquePipelineCount> GraphicsOpaqueFastModulateOpaqueAlphaPlainPipelines{};
    std::array<VkPipeline, GraphicsTranslucentPipelineCount> GraphicsTranslucentPipelines{};
    std::array<VkPipeline, GraphicsBgZeroTranslucentPipelineCount> GraphicsBgZeroTranslucentPipelines{};
    std::array<VkPipeline, GraphicsShadowMaskPipelineCount> GraphicsShadowMaskPipelines{};
    std::array<VkPipeline, GraphicsShadowMaskBgZeroPipelineCount> GraphicsShadowMaskBgZeroPipelines{};
    std::array<VkPipeline, GraphicsShadowClearPipelineCount> GraphicsShadowClearPipelines{};
    std::array<VkPipeline, GraphicsShadowBlendBgZeroPipelineCount> GraphicsShadowBlendBgZeroPipelines{};
    std::array<VkPipeline, GraphicsShadowBlendPipelineCount> GraphicsShadowBlendPipelines{};
    std::array<VkPipeline, GraphicsEdgeMarkPipelineCount> GraphicsEdgeMarkPipelines{};
    std::array<VkPipeline, GraphicsWModeCount> GraphicsOpaqueUiOverlayPipelines{};
    VkPipeline GraphicsClearPipeline = VK_NULL_HANDLE;
    VkPipeline GraphicsStencilBitClearPipeline = VK_NULL_HANDLE;
    VkPipeline GraphicsFinalEdgePipeline = VK_NULL_HANDLE;
    VkPipeline GraphicsFinalEdgeFogPipeline = VK_NULL_HANDLE;
    VkPipeline GraphicsFinalFogPipeline = VK_NULL_HANDLE;
    VkRenderPass GraphicsRasterRenderPass = VK_NULL_HANDLE;
    VkRenderPass GraphicsFinalRenderPass = VK_NULL_HANDLE;
    VkFramebuffer GraphicsRasterFramebuffer = VK_NULL_HANDLE;
    VkFramebuffer GraphicsFinalFramebuffer = VK_NULL_HANDLE;
    VkSampler GraphicsAttachmentSampler = VK_NULL_HANDLE;
    VkFormat GraphicsDepthStencilFormat = VK_FORMAT_UNDEFINED;
    bool GraphicsReady = false;
    static constexpr u32 ResultLayerCount = 8;
    static constexpr size_t AsyncRenderContextCount = 6;
    static constexpr u32 TimestampQueryCount = 9;
    std::array<RenderContext, AsyncRenderContextCount> RenderContexts{};
    size_t NextRenderContextIndex = 0;
    RenderContext* LastSubmittedRenderContext = nullptr;
    RasterDispatchPath ActiveRasterDispatchPath = RasterDispatchPath::DirectTiles;
    bool CpuTileBinningEnabled = false;

    VkImage ColorImage = VK_NULL_HANDLE;
    VkDeviceMemory ColorImageMemory = VK_NULL_HANDLE;
    VkImageView ColorImageView = VK_NULL_HANDLE;
    VkImage AttrImage = VK_NULL_HANDLE;
    VkDeviceMemory AttrImageMemory = VK_NULL_HANDLE;
    VkImageView AttrImageView = VK_NULL_HANDLE;
    VkImage DepthImage = VK_NULL_HANDLE;
    VkDeviceMemory DepthImageMemory = VK_NULL_HANDLE;
    VkImageView DepthImageView = VK_NULL_HANDLE;
    VkImage DepthStencilImage = VK_NULL_HANDLE;
    VkDeviceMemory DepthStencilImageMemory = VK_NULL_HANDLE;
    VkImageView DepthStencilImageView = VK_NULL_HANDLE;
    u32 ColorImageWidth = 0;
    u32 ColorImageHeight = 0;
    bool ColorImageInitialized = false;

    VkBuffer ReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ReadbackMemory = VK_NULL_HANDLE;
    VkDeviceSize ReadbackSize = 0;
    void* ReadbackMapped = nullptr;
    u32 RawReadbackWidth = 0;
    u32 RawReadbackHeight = 0;
    VkImage CaptureReadbackImage = VK_NULL_HANDLE;
    VkDeviceMemory CaptureReadbackMemory = VK_NULL_HANDLE;
    bool CaptureReadbackImageInitialized = false;
    VkBuffer ResultReadbackBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ResultReadbackMemory = VK_NULL_HANDLE;
    VkDeviceSize ResultReadbackSize = 0;
    void* ResultReadbackMapped = nullptr;

    VkBuffer TriangleBuffer = VK_NULL_HANDLE;
    VkDeviceMemory TriangleMemory = VK_NULL_HANDLE;
    VkDeviceSize TriangleBufferSize = 0;
    void* TriangleMapped = nullptr;
    VkBuffer GraphicsVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory GraphicsVertexMemory = VK_NULL_HANDLE;
    VkDeviceSize GraphicsVertexBufferSize = 0;
    void* GraphicsVertexMapped = nullptr;
    VkBuffer GraphicsSceneVertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory GraphicsSceneVertexMemory = VK_NULL_HANDLE;
    VkDeviceSize GraphicsSceneVertexBufferSize = 0;
    void* GraphicsSceneVertexMapped = nullptr;
    VkBuffer GraphicsEdgeIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory GraphicsEdgeIndexMemory = VK_NULL_HANDLE;
    VkDeviceSize GraphicsEdgeIndexBufferSize = 0;
    void* GraphicsEdgeIndexMapped = nullptr;

    VkBuffer ResultBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ResultMemory = VK_NULL_HANDLE;
    VkDeviceSize ResultBufferSize = 0;

    VkBuffer BinMaskBuffer = VK_NULL_HANDLE;
    VkDeviceMemory BinMaskMemory = VK_NULL_HANDLE;
    VkDeviceSize BinMaskBufferSize = 0;

    VkBuffer GroupListBuffer = VK_NULL_HANDLE;
    VkDeviceMemory GroupListMemory = VK_NULL_HANDLE;
    VkDeviceSize GroupListBufferSize = 0;

    VkBuffer SpanSetupBuffer = VK_NULL_HANDLE;
    VkDeviceMemory SpanSetupMemory = VK_NULL_HANDLE;
    VkDeviceSize SpanSetupBufferSize = 0;

    VkBuffer WorkOffsetBuffer = VK_NULL_HANDLE;
    VkDeviceMemory WorkOffsetMemory = VK_NULL_HANDLE;
    VkDeviceSize WorkOffsetBufferSize = 0;

    VkBuffer ToonBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ToonMemory = VK_NULL_HANDLE;
    VkDeviceSize ToonBufferSize = 0;
    void* ToonMapped = nullptr;
    VkBuffer ClearBuffer = VK_NULL_HANDLE;
    VkDeviceMemory ClearMemory = VK_NULL_HANDLE;
    VkDeviceSize ClearBufferSize = 0;
    void* ClearMapped = nullptr;
    VkBuffer CaptureLineBuffer = VK_NULL_HANDLE;
    VkDeviceMemory CaptureLineMemory = VK_NULL_HANDLE;
    VkDeviceSize CaptureLineBufferSize = 0;
    void* CaptureLineMapped = nullptr;
    static constexpr u32 CaptureLineBufferSlotCount = 2;
    std::array<VkBuffer, CaptureLineBufferSlotCount> CaptureLineBuffers{};
    std::array<VkDeviceMemory, CaptureLineBufferSlotCount> CaptureLineMemories{};
    std::array<VkDeviceSize, CaptureLineBufferSlotCount> CaptureLineBufferSizes{};
    std::array<void*, CaptureLineBufferSlotCount> CaptureLineMappedSlots{};
    u32 ActiveCaptureLineBufferSlot = 0;
    int PendingCaptureLineBufferSlot = -1;
    int ReadyCaptureLineBufferSlot = -1;
    bool PendingCaptureLineScreenSwap = false;
    bool ReadyCaptureLineScreenSwap = false;

    VkImage FallbackTextureImage = VK_NULL_HANDLE;
    VkDeviceMemory FallbackTextureMemory = VK_NULL_HANDLE;
    VkImageView FallbackTextureView = VK_NULL_HANDLE;
    VkSampler FallbackTextureSampler = VK_NULL_HANDLE;
    VkBuffer FallbackTextureStagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory FallbackTextureStagingMemory = VK_NULL_HANDLE;

    std::array<VkDescriptorImageInfo, MaxTextureDescriptors> ActiveTextureDescriptors{};
    u32 ActiveTextureDescriptorCount = 0;

    std::vector<TriangleGpu> Triangles;
    std::vector<GraphicsVertexGpu> GraphicsVertices;
    std::vector<GraphicsVertexGpu> GraphicsSceneVertices;
    std::vector<GraphicsPolygonDraw> GraphicsPolygons;
    AcceleratedScene SharedGraphicsScene{};
    std::vector<u32> GraphicsOpaqueDrawIndices;
    std::vector<u32> GraphicsNeedOpaqueDrawIndices;
    std::vector<u32> GraphicsAlphaDrawIndices;
    std::vector<u32> GraphicsShadowMaskDrawIndices;
    std::vector<u32> GraphicsShadowDrawIndices;
    u32 GraphicsHiddenAlphaZeroFinalEdgePolyIdOverride = 0xFFFFFFFFu;
    u32 GraphicsHiddenAlphaZeroFinalEdgeColorOverride = 0;
    std::vector<u32> RawReadbackRgba;
    std::vector<u32> RawResultReadback;
    // MELONPRIME_SAPPHIRE_VULKAN_STRUCTURED_2D_A2
    std::array<u32, 2 * 256 * 192> Structured2DPlane0{};
    std::array<u32, 2 * 256 * 192> Structured2DPlane1{};
    std::array<u32, 2 * 256 * 192> Structured2DControl{};
    std::array<u32, 2 * 256 * 192> Structured2DNativeFinal{};
    // Per engine/line: DispCnt, MasterBrightness and enabled/blank/screen flags.
    std::array<u32, 2 * 192> Structured2DLineMeta{};
    std::array<u32, 2 * 192> Structured2DLineState{};
    // Upload layout per engine/line: plane0[256], plane1[256], control[256],
    // nativeFinal[256], full DispCnt[1], brightness/status[1].
    std::array<u32, 2 * 192 * Structured2DPackedStrideWords> Structured2DPacked{};
    VkBuffer Structured2DGpuBuffer = VK_NULL_HANDLE;
    VkDeviceMemory Structured2DGpuMemory = VK_NULL_HANDLE;
    VkDeviceSize Structured2DGpuBufferSize = 0;
    void* Structured2DGpuMapped = nullptr;
    bool Structured2DGpuMemoryCoherent = false;
    bool Structured2DGpuBufferValid = false;
    u64 Structured2DGpuFrameSerial = 0;
    VkImage SapphireCompositionImage = VK_NULL_HANDLE;
    VkDeviceMemory SapphireCompositionMemory = VK_NULL_HANDLE;
    VkImageView SapphireCompositionImageView = VK_NULL_HANDLE;
    VkSampler SapphireCompositionSampler = VK_NULL_HANDLE;
    VkDescriptorSetLayout SapphireCompositionDescriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool SapphireCompositionDescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet SapphireCompositionDescriptorSet = VK_NULL_HANDLE;
    VkPipelineLayout SapphireCompositionPipelineLayout = VK_NULL_HANDLE;
    VkFormat SapphireCompositionFormat = VK_FORMAT_R8G8B8A8_UNORM;
    u32 SapphireCompositionWidth = 0;
    u32 SapphireCompositionHeight = 0;
    u64 SapphireCompositionFrameSerial = 0;
    bool SapphireCompositionResourcesReady = false;
    bool SapphireCompositionDescriptorsReady = false;
    VkCommandPool SapphireCompositionCommandPool = VK_NULL_HANDLE;
    VkCommandBuffer SapphireCompositionCommandBuffer = VK_NULL_HANDLE;
    VkFence SapphireCompositionFence = VK_NULL_HANDLE;
    u64 SapphireCompositionCommandFrameSerial = 0;
    bool SapphireCompositionCommandContextReady = false;
    // MELONPRIME_SAPPHIRE_VULKAN_COMPOSITOR_SHADER_MODULE_A6
    VkShaderModule SapphireCompositionShaderModule = VK_NULL_HANDLE;
    size_t SapphireCompositionShaderSpirvBytes = 0;
    u64 SapphireCompositionShaderFrameSerial = 0;
    bool SapphireCompositionShaderModuleReady = false;
    std::array<u8, 2 * 192> Structured2DLineReceived{};
    u64 Structured2DPendingSerial = 0;
    u64 Structured2DFrameSerial = 0;
    bool Structured2DScreenSwap = false;
    bool Structured2DFrameValid = false;

    std::array<u32, 256 * 192> LineCache{};
    std::array<u32, 256 * 192> LastValidExactCaptureLineCache{};
    u32 ExactCaptureFallbackPackedColor = 0;
    bool ExactCaptureFallbackValid = false;
    bool HasLastValidExactCapture = false;
    bool LastValidExactCaptureScreenSwap = false;
    bool CurrentCaptureScreenSwapHint = false;
    bool HasCurrentCaptureScreenSwapHint = false;
    bool CurrentRenderScreenSwap = false;
    PFN_vkResetQueryPoolEXT ResetQueryPool = nullptr;
    float TimestampPeriodNs = 0.0f;
    bool TimestampQueriesSupported = false;
    PerfSampleWindow<120> RenderCpuWindow;
    PerfSampleWindow<120> FenceWaitCpuWindow;
    PerfSampleWindow<120> GpuWindow;
    PerfSampleWindow<120> TriangleCountWindow;
    PerfSampleWindow<120> PassCountWindow;
    PerfSampleWindow<120> InterpCpuWindow;
    PerfSampleWindow<120> BinCpuWindow;
    PerfSampleWindow<120> WorkOffsetsCpuWindow;
    PerfSampleWindow<120> SortCpuWindow;
    PerfSampleWindow<120> RasterCpuWindow;
    PerfSampleWindow<120> GraphicsSceneBuildCpuWindow;
    PerfSampleWindow<120> GraphicsMainCpuWindow;
    PerfSampleWindow<120> GraphicsAlphaCpuWindow;
    PerfSampleWindow<120> DepthBlendCpuWindow;
    PerfSampleWindow<120> FinalCpuWindow;
    PerfSampleWindow<120> CaptureLineExportCpuWindow;
    PerfSampleWindow<120> CpuActiveTileCountWindow;
    PerfSampleWindow<120> CpuTileCountWindow;
    PerfSampleWindow<120> CpuActiveGroupCountWindow;
    PerfSampleWindow<120> CpuActiveDispatchWindow;
    PerfSampleWindow<120> InterpGpuWindow;
    PerfSampleWindow<120> BinGpuWindow;
    PerfSampleWindow<120> WorkOffsetsGpuWindow;
    PerfSampleWindow<120> SortGpuWindow;
    PerfSampleWindow<120> RasterGpuWindow;
    PerfSampleWindow<120> DepthBlendGpuWindow;
    PerfSampleWindow<120> FinalGpuWindow;
    PerfSampleWindow<120> CaptureLineExportGpuWindow;
    PerfSampleWindow<120> EarlySubmitCpuWindow;
    PerfSampleWindow<120> EarlySubmitContextWaitCpuWindow;
    u32 LastGraphicsOpaqueDrawCount = 0;
    u32 LastGraphicsNeedOpaqueDrawCount = 0;
    u32 LastGraphicsAlphaDrawCount = 0;
    u32 LastGraphicsOpaqueWDrawCount = 0;
    u32 LastGraphicsOpaqueZDrawCount = 0;
    u32 LastGraphicsOpaqueTexturedDrawCount = 0;
    u32 LastGraphicsOpaqueUntexturedDrawCount = 0;
    u32 LastGraphicsOpaqueModulateDrawCount = 0;
    u32 LastGraphicsOpaqueDecalDrawCount = 0;
    u32 LastGraphicsOpaqueToonDrawCount = 0;
    u32 LastGraphicsOpaqueHighlightDrawCount = 0;
    u32 LastGraphicsOpaqueLinearDrawCount = 0;
    u32 LastGraphicsOpaqueRepeatDrawCount = 0;
    u32 LastGraphicsOpaqueMirrorDrawCount = 0;
    u32 LastGraphicsOpaqueRepeatSDrawCount = 0;
    u32 LastGraphicsOpaqueRepeatTDrawCount = 0;
    u32 LastGraphicsOpaqueMirrorSDrawCount = 0;
    u32 LastGraphicsOpaqueMirrorTDrawCount = 0;
    u32 LastGraphicsOpaqueClampSDrawCount = 0;
    u32 LastGraphicsOpaqueClampTDrawCount = 0;
    u32 LastGraphicsOpaqueFullAlphaDrawCount = 0;
    u32 LastGraphicsOpaqueHighresRepeatModelDrawCount = 0;
    u64 ContextMissCount = 0;
    u64 LateFrameCount = 0;
    u64 DroppedFrameCount = 0;
    u64 CpuDirectTilesPathCount = 0;
    u64 DirectTilesPathCount = 0;
    u64 LegacyWorklistPathCount = 0;
    u64 ReadbackColorRequestCount = 0;
    u64 ReadbackResultRequestCount = 0;
    u64 CapturePrepareRequestCount = 0;
    std::array<u64, 4> CaptureModeCounts{};
    std::array<u64, 4> CaptureSizeModeCounts{};
    std::array<u64, static_cast<size_t>(RasterExecutionProfile::Count)> RasterExecutionProfileCounts{};
    std::array<u64, static_cast<size_t>(RasterTileLoopMode::Count)> RasterTileLoopModeCounts{};
    std::array<u64, static_cast<size_t>(CapturePathMode::Count)> CapturePathModeCounts{};
    u64 CaptureSource3dCount = 0;
    u64 CaptureEnabledCount = 0;
    u64 CaptureLineExportCount = 0;
    u64 RasterSpecializedShadeModeCount = 0;
    u64 RasterSpecializedTextureModeCount = 0;
    u64 RasterSpecializedTranslucencyModeCount = 0;
    u64 RasterSpecializedAllModesCount = 0;
    u64 EarlySubmitAttemptCount = 0;
    u64 EarlySubmitHitCount = 0;
    u64 EarlySubmitMissCount = 0;
    u64 EarlySubmitSkipVCount215Count = 0;
    u32 CaptureDebugLogsRemaining = 0;
    u32 PaletteUiGateLogCooldown = 0;
    bool PaletteUiGateLastActive = false;
    u32 PaletteUiOpaqueReplayLogCooldown = 0;
    bool PaletteUiOpaqueReplayLastActive = false;
    u32 GraphicsDrawDispatchMissingLogCooldown = 0;
    bool SkipRenderAtVCount215 = false;
    bool InEarlySubmitAttempt = false;
    u64 CurrentEarlySubmitContextWaitNs = 0;
    bool CaptureReadbackPending = false;
    RenderContext* PendingCaptureReadbackContext = nullptr;
    bool CaptureLinePending = false;
    bool CaptureLineReady = false;
    bool ExactCaptureLineCachePrepared = false;
    bool ExactCaptureLineCacheFresh = false;
    bool CaptureLineDataIsRgba8 = false;
    RenderContext* PendingCaptureLineContext = nullptr;
    const u32* ReadyCaptureLineData = nullptr;
    u32 PostFastForwardDrainFrames = 0;
};
}
