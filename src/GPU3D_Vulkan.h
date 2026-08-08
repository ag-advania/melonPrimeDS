/*
    Copyright 2016-2026 melonDS team

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

#ifndef GPU3D_VULKAN_H
#define GPU3D_VULKAN_H

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <array>
#include <memory>
#include <string>
#include <vector>

#include "GPU3D.h"
#include "GPU3D_TexcacheVulkan.h"
#include "GPU3D_Vulkan_ShaderModules.h"
#include "VulkanCommon.h"
#include "VulkanDescriptors.h"
#include "VulkanDevice.h"
#include "VulkanMemory.h"
#include "VulkanSync.h"

namespace melonDS
{

class VulkanContext;

// Vulkan 3D renderer: a clean-room port of the OpenGL compute rasterizer
// (GPU3D_Compute.cpp), i.e. the GPU version of the software rasterizer, with
// the same tile-binned pipeline and the same fixed-point math. The CPU-side
// span setup below is a 1:1 transcription of that file; every Vulkan API
// decision comes from the Khronos specification.
//
// It is paired with the software 2D renderer (see GPU_Vulkan.h). A
// native-resolution copy of the 3D output remains available for display
// capture through GetLine(), while the structured 2D metadata the software
// engines record is recomposited with the internal-resolution 3D image for the
// visible output (phases 8-9).
class VulkanRenderer3D : public Renderer3D
{
public:
    // Returns nullptr when Vulkan is unavailable or device setup failed, so the
    // caller can report a truthful failure instead of constructing a dead
    // renderer.
    static std::unique_ptr<VulkanRenderer3D> New(melonDS::GPU3D& gpu3D);

    ~VulkanRenderer3D() override;

    bool Init() override;
    void Reset() override;

    // Releases every GPU-visible object while the emulator core is still alive.
    void Stop();

    void SetRenderSettings(int scale, bool betterPolygons, bool hiresCoordinates);
    [[nodiscard]] int GetScaleFactor() const noexcept { return ScaleFactor; }

    void RenderFrame() override;
    u32* GetLine(int line) override;
    [[nodiscard]] bool UsesStructured2DMetadata() const noexcept override { return true; }

    // ---------------------------------------------------------------------
    // Structured software 2D + internal-resolution 3D.
    //
    // Called from VulkanRenderer::VBlank(), which is the one point in the DS
    // frame where this frame's structured 2D planes and this frame's 3D image
    // both exist. `planes` is
    // {screen0 below/above/control, screen1 below/above/control}, each
    // 256*192 words; `lineMeta` is one 192-word array per screen. `generation`
    // is SoftRenderer's own frame counter -- composing the same generation
    // twice is a no-op, and a stale generation is never composed.
    //
    // Deliberately *not* deferred: nothing here latches a previous frame's
    // result. That is what makes the composition immune to MPH flipping
    // POWCNT1 bit 15 every frame; the producer already resolved engine -> LCD
    // for this frame before publishing.
    bool ComposeStructuredOutput(
        const std::array<const u32*, 6>& planes,
        const std::array<const u32*, 2>& lineMeta,
        u64 generation);

    // The composed screen, or nullptr before the first successful compose.
    // Valid until the next ComposeStructuredOutput() call publishes a new front
    // buffer.
    [[nodiscard]] const u32* GetComposedScreen(u32 screen) const noexcept;

    // Internal resolution, not 256x192. This is the mechanism by which high
    // resolution survives to present: the display path never sees a
    // native-resolution intermediate.
    [[nodiscard]] u32 GetComposedWidth() const noexcept { return static_cast<u32>(ScreenWidth); }
    [[nodiscard]] u32 GetComposedHeight() const noexcept { return static_cast<u32>(ScreenHeight); }

    [[nodiscard]] bool HasRuntimeFailure() const noexcept { return RuntimeFailed; }
    [[nodiscard]] const std::string& GetRuntimeFailureReason() const noexcept
    {
        return RuntimeFailureReason;
    }

    // The frontend compiles pipelines incrementally so ROM startup does not
    // stall. Nothing is created before SetRenderSettings() has supplied the
    // internal resolution the specialization constants are built from.
    bool NeedsShaderCompile() override
    {
        return !RuntimeFailed && ScaleFactor > 0 && ShaderStepIdx < ShaderStepCount;
    }
    void ShaderCompileStep(int& current, int& count) override;

private:
    explicit VulkanRenderer3D(melonDS::GPU3D& gpu3D);

    static constexpr int MaxVariants = 256;
    static constexpr int MaxYSpanSetups = 6144 * 2;
    static constexpr int BinStride = 2048 / 32;
    static constexpr int CoarseBinStride = BinStride / 32;
    static constexpr int CoarseTileCountX = 8;
    static constexpr int MaxRenderPolygons = 2048;

    // Pipelines 0..32 are the compute rasterizer and match
    // ComputeRenderer3D::ShaderCompileStep() index for index; 33 (Resolve) and
    // 34 (Compositor) are the presentation stages, which the OpenGL compute
    // renderer does not have because it hands FinalFB to the GL 2D engine.
    static constexpr int ShaderStepCount =
        static_cast<int>(VulkanShaders::Pipeline_Count);

    // Frames the CPU may run ahead of the GPU *for this renderer*.
    //
    // One, and that is a correctness requirement rather than a tuning choice.
    // The compute rasterizer works out of a single shared set of intermediate
    // buffers -- XSpanSetups, the three tile buffers, BinResult, WorkDescs,
    // ResultBuffer and FinalFB. A second in-flight frame would write those
    // while the previous frame still read them (a WAR/WAW race), and giving
    // each frame its own copy is not an option: the tile buffers alone are
    // three quarters of a gigabyte at 16x.
    //
    // This costs nothing in practice. The wait happens at the *start* of frame
    // N for frame N-1, and a whole DS frame of software 2D work has run on the
    // emulation thread in between, so CPU and GPU still overlap.
    static constexpr u32 RendererFramesInFlight = 1;

    // Two set-0 allocations per frame slot. They differ only in what binding 13
    // points at -- the native capture buffer for the rasterizer's Resolve stage,
    // the two-screen composed buffer for the compositor -- but the compositor
    // records into a separate command buffer, so it cannot share the set the
    // rasterizer's submission may still be reading.
    static constexpr u32 RasterizerSetSlot = 0;
    static constexpr u32 CompositorSetSlot = 1;
    static constexpr u32 RasterizerSetsPerFrame = 2;

    // -----------------------------------------------------------------------
    // GPU-visible struct layouts.
    //
    // Every one of these is byte-identical to the std430 block the matching
    // GLSL source declares (src/GPU3D_Vulkan_shaders/*.glsl). std430 gives
    // scalar members a base alignment equal to their size, so a run of 4-byte
    // scalars packs tightly and the C++ struct matches with no padding. GLSL
    // `bool` block members are 32-bit, which is why Linear / IsDummy are u32
    // here even though the GLSL spells them `bool`.
    // -----------------------------------------------------------------------

    // YSpanSetupBuffer.glsl :: YSpanSetup
    struct SpanSetupY
    {
        s32 Z0, Z1, W0, W1;
        s32 ColorR0, ColorG0, ColorB0;
        s32 ColorR1, ColorG1, ColorB1;
        s32 TexcoordU0, TexcoordV0;
        s32 TexcoordU1, TexcoordV1;

        s32 I0, I1;
        u32 Linear;             // GLSL `bool Linear`
        s32 IRecip;
        s32 W0n, W0d, W1d;

        s32 Increment;

        s32 X0, X1, Y0, Y1;
        s32 XMin, XMax;
        s32 DxInitial;

        s32 XCovIncr;
        u32 IsDummy;            // GLSL `bool IsDummy`
    };

    // XSpanSetupBuffer.glsl :: XSpanSetup. The GLSL names slots 2 and 3
    // InsideStart / InsideEnd; the CPU side never writes them (InterpSpans
    // produces the whole record), so they keep the GLSL names here.
    struct SpanSetupX
    {
        s32 X0, X1;
        s32 InsideStart, InsideEnd, EdgeCovL, EdgeCovR;
        s32 XRecip;
        u32 Flags;
        s32 Z0, Z1, W0, W1;
        s32 ColorR0, ColorG0, ColorB0;
        s32 ColorR1, ColorG1, ColorB1;
        s32 TexcoordU0, TexcoordV0;
        s32 TexcoordU1, TexcoordV1;
        s32 CovLInitial, CovRInitial;
    };

    // Consumed as a VK_FORMAT_R16G16B16A16_UINT uniform texel buffer
    // (Common.glsl set 0 binding 10, fetched with texelFetch in InterpSpans).
    struct SetupIndices
    {
        u16 PolyIdx, SpanIdxL, SpanIdxR, Y;
    };

    // PolygonBuffer.glsl :: Polygon
    struct RenderPolygon
    {
        u32 FirstXSpan;
        s32 YTop, YBot;

        s32 XMin, XMax;
        s32 XMinY, XMaxY;

        u32 Variant;
        u32 Attr;

        float TextureLayer;
    };

    static_assert(sizeof(SpanSetupY) == 31 * 4, "SpanSetupY must match the std430 YSpanSetup layout");
    static_assert(alignof(SpanSetupY) == 4, "SpanSetupY must not gain alignment padding");
    static_assert(offsetof(SpanSetupY, Linear) == 16 * 4, "YSpanSetup.Linear is the 17th 4-byte slot");
    static_assert(offsetof(SpanSetupY, IsDummy) == 30 * 4, "YSpanSetup.IsDummy is the 31st 4-byte slot");

    static_assert(sizeof(SpanSetupX) == 24 * 4, "SpanSetupX must match the std430 XSpanSetup layout");
    static_assert(alignof(SpanSetupX) == 4, "SpanSetupX must not gain alignment padding");
    static_assert(offsetof(SpanSetupX, Flags) == 7 * 4, "XSpanSetup.Flags is the 8th 4-byte slot");

    static_assert(sizeof(RenderPolygon) == 10 * 4, "RenderPolygon must match the std430 Polygon layout");
    static_assert(alignof(RenderPolygon) == 4, "RenderPolygon must not gain alignment padding");
    static_assert(offsetof(RenderPolygon, TextureLayer) == 9 * 4, "Polygon.TextureLayer is the 10th slot");

    static_assert(sizeof(SetupIndices) == 8, "SetupIndices must match one R16G16B16A16_UINT texel");

    // BinningBuffer.glsl :: BinResultBuffer header, std430. The trailing
    // BinningMaskAndOffset[] runtime array starts immediately after this.
    struct BinResultHeader
    {
        u32 VariantWorkCount[MaxVariants * 4];      // uvec4[256], offset 0
        u32 SortedWorkOffset[MaxVariants];          // offset 4096
        u32 VariantWorkRealCount[MaxVariants];      // offset 5120 (MelonPrimeDS #2047 split)
        u32 SortWorkWorkCount[4];                   // uvec4, offset 6144
    };
    static_assert(sizeof(BinResultHeader) == 6160, "BinResultHeader must match the std430 offsets");
    static_assert(offsetof(BinResultHeader, SortedWorkOffset) == 4096, "SortedWorkOffset offset");
    static_assert(offsetof(BinResultHeader, VariantWorkRealCount) == 5120, "VariantWorkRealCount offset");
    static_assert(offsetof(BinResultHeader, SortWorkWorkCount) == 6144, "SortWorkWorkCount must stay uvec4-aligned");

    // Common.glsl :: MetaUniform, std140.
    //
    // std140 pads every array element out to 16 bytes, which is exactly what
    // the u32 ToonTable[4*34] mirror produces: 34 uvec4 elements. The three
    // scalar runs after it are 4-byte aligned and the trailing vec2 lands on an
    // 8-byte boundary (584), so the block is 592 bytes with no hidden padding.
    struct MetaUniform
    {
        u32 NumPolygons;                // 0
        u32 NumVariants;                // 4
        u32 AlphaRef;                   // 8
        u32 DispCnt;                    // 12

        u32 ToonTable[4 * 34];          // 16 .. 560

        u32 ClearColor;                 // 560
        u32 ClearDepth;                 // 564
        u32 ClearAttr;                  // 568

        u32 FogOffset;                  // 572
        u32 FogShift;                   // 576
        u32 FogColor;                   // 580

        float ClearBitmapOffset[2];     // 584 .. 592
    };
    static_assert(sizeof(MetaUniform) == 592, "MetaUniform must match the std140 block layout");
    static_assert(offsetof(MetaUniform, ToonTable) == 16, "ToonTable must start on a uvec4 boundary");
    static_assert(offsetof(MetaUniform, ClearColor) == 560, "ClearColor follows ToonTable[34]");
    static_assert(offsetof(MetaUniform, ClearBitmapOffset) == 584, "ClearBitmapOffset must be 8-byte aligned");

    // One rasterise pipeline per texture/blend combination, exactly like the
    // OpenGL compute renderer's shader table.
    enum RasteriseKind
    {
        RasteriseKind_NoTexture = 0,
        RasteriseKind_NoTextureToon,
        RasteriseKind_NoTextureHighlight,
        RasteriseKind_UseTextureDecal,
        RasteriseKind_UseTextureModulate,
        RasteriseKind_UseTextureToon,
        RasteriseKind_UseTextureHighlight,
        RasteriseKind_ShadowMask,
        RasteriseKind_Count,
    };
    static_assert(RasteriseKind_Count * 2 == 16, "16 rasterise pipelines, 8 kinds x 2 depth modes");

    struct Variant
    {
        u32 Texture = 0;        // texture heap handle, 0 = untextured
        u32 WrapS = 0;
        u32 WrapT = 0;
        u8 BlendMode = 0;
        u16 Width = 0;
        u16 Height = 0;

        bool operator==(const Variant& other) const noexcept
        {
            return Texture == other.Texture
                && WrapS == other.WrapS
                && WrapT == other.WrapT
                && BlendMode == other.BlendMode;
        }
    };

    // --- lifecycle ---------------------------------------------------------
    bool CreateFixedResources();
    bool CreateScaleDependentResources();
    void ReleaseScaleDependentResources();
    void ReleasePipelines();
    void SetRuntimeFailure(std::string reason);

    bool CreatePipelineCache();
    void SavePipelineCache();
    bool BuildPipeline(u32 pipelineIndex);

    // --- per-frame ---------------------------------------------------------
    void RecordInitialTransitions(VkCommandBuffer cmd);
    void UpdateClearBitmap(VkCommandBuffer cmd, Vk::StagingRing& staging);
    // `slot` selects which set-0 allocation is written: slot 0 is the
    // rasterizer's (bound for the whole RenderFrame() command buffer, including
    // the Resolve stage), slot 1 the compositor's. `presentationOutput` is the
    // buffer bound at binding 13, which is the only thing that differs.
    bool WriteRasterizerDescriptorSet(u32 frameIndex, u32 slot, VkBuffer presentationOutput);
    VkDescriptorSet AcquireTextureSet(u32 frameIndex, VkImageView textureView, VkSampler sampler);
    void FillMetaUniform(MetaUniform& meta, u32 numVariants, u32 numPolygons) const;

    // Records a compute->compute dependency over `buffers`. Kept explicit
    // rather than folded into a global VkMemoryBarrier so every dependency in
    // the frame names the resource it is about.
    void BufferBarrier(
        VkCommandBuffer cmd,
        const VkBuffer* buffers, u32 count,
        VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
        VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) const;

    // CPU-side span setup, transcribed from the OpenGL compute renderer.
    void SetupAttrs(SpanSetupY* span, Polygon* poly, int from, int to) const;
    void SetupYSpan(RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int from, int to, int side, s32 positions[10][2]) const;
    void SetupYSpanDummy(RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int vertex, int side, s32 positions[10][2]) const;

    // Returns the number of variants collected, filling YSpanSetups /
    // YSpanIndices / RenderPolygons. `numPolygons` can be lower than
    // GPU3D.RenderNumPolygons when the span budget ran out.
    u32 BuildPolygons(int& numYSpans, int& numSetupIndices, u32& numPolygons);

    void EnsureFrameReadback();

    // --- device objects ----------------------------------------------------
    VulkanContext* Context = nullptr;
    VulkanDevice Device;
    Vk::FrameRing Frames;
    // The compositor records into its own command buffer and its own fence
    // rather than sharing the rasterizer's. It has to: the structured 2D planes
    // are only complete after all 192 scanlines have been drawn, which is long
    // after RenderFrame() closed and submitted its command buffer, and reusing
    // the rasterizer's slot would reset the fence GetLine()'s capture readback
    // is still waiting on. One frame in flight, for the same reason the
    // rasterizer keeps one.
    Vk::FrameRing ComposeFrames;
    Vk::DescriptorLayouts Layouts;
    Vk::DescriptorPool Descriptors;
    Vk::StagingRing FrameStaging;

    VulkanTextureHeap TextureHeap;
    VulkanSamplerCache Samplers;
    TexcacheVulkan Texcache;

    VkPipelineCache PipelineCache = VK_NULL_HANDLE;
    std::array<VkPipeline, ShaderStepCount> Pipelines{};

    // Fixed-size resources, independent of the internal resolution.
    Vk::Buffer MetaUniformBuffer;           // FramesInFlight slices
    Vk::Buffer PolygonBuffer;
    Vk::Buffer YSpanSetupBuffer;
    Vk::Image ClearBitmapImage[2];          // R32_UINT, 256x256, matching GL's GL_R32UI
    Vk::Image DummyTextureImage;            // usampler2DArray placeholder
    Vk::Image DummyCaptureImage;            // sampler2DArray placeholder
    // 256x192 packed r6g6b6a5, produced by the Resolve stage and read by
    // GetLine(). Native resolution regardless of the internal resolution:
    // display capture writes back into real VRAM as 15-bit DS words, so its
    // semantics have to stay DS-native even while presentation does not.
    Vk::Buffer NativeResolveBuffer;
    Vk::ReadbackBuffer NativeReadback;
    VkDeviceSize MetaUniformStride = 0;

    // The structured 2D frame, staged once per VBlank and copied into device
    // memory for the compositor. Native-resolution and therefore fixed size:
    // the software 2D engines always work at 256x192, whatever the 3D internal
    // resolution is.
    Vk::Buffer StructuredStagingBuffer;      // host-visible, persistently mapped
    Vk::Buffer StructuredInputBuffer;        // device-local, set 0 binding 12

    // Resolution-dependent resources.
    Vk::Buffer XSpanSetupBuffer;
    Vk::Buffer SetupIndicesBuffer;          // + texel buffer view
    Vk::Buffer TileBuffers[3];              // color / depth / attr
    Vk::Buffer ResultBuffer;
    Vk::Buffer BinResultBuffer;             // storage + indirect dispatch args
    Vk::Buffer WorkDescBuffer;
    Vk::Image FinalFB;                      // internal-resolution RGBA8 storage image

    // Compositor output: the two screens stacked, BGRA8, at the *internal*
    // resolution. Resolution-dependent, so it is created and destroyed with the
    // rest of the scale-sized set.
    Vk::Buffer ComposeOutputBuffer;
    Vk::ReadbackBuffer ComposeReadback;

    // --- CPU-side scratch, mirroring the OpenGL compute renderer -----------
    std::array<Variant, MaxVariants> Variants{};
    std::vector<SetupIndices> YSpanIndices;
    std::unique_ptr<SpanSetupY[]> YSpanSetups;
    std::unique_ptr<RenderPolygon[]> RenderPolygons;
    std::unique_ptr<u32[]> ClearBitmap[2];
    u8 ClearBitmapDirty = 0x3;

    // --- geometry ----------------------------------------------------------
    int TileSize = 8;
    int CoarseTileCountY = 4;
    int CoarseTileArea = 32;
    int CoarseTileW = 64;
    int CoarseTileH = 32;
    int ClearCoarseBinMaskLocalSize = 64;
    int TilesPerLine = 32;
    int TileLines = 24;
    int MaxWorkTiles = 0;
    int MaxYSpanIndices = 0;
    u32 TileGeometryBucket = 0;

    int ScaleFactor = -1;
    int ScreenWidth = 256;
    int ScreenHeight = 192;
    bool BetterPolygons = false;
    bool HiresCoordinates = false;

    // --- state -------------------------------------------------------------
    int ShaderStepIdx = 0;
    bool RuntimeFailed = false;
    std::string RuntimeFailureReason;
    bool Initialized = false;
    // FinalFB is recreated on every resolution change and starts UNDEFINED;
    // the placeholder and clear-bitmap images are created once for the whole
    // renderer lifetime, so the two initialisations are tracked separately
    // rather than re-issuing a transition whose source stage would be a lie.
    bool NeedsFinalFBTransition = true;
    bool PlaceholdersInitialized = false;

    u32 TextureSetCursor = 0;
    VkImageView BoundTextureView = VK_NULL_HANDLE;
    VkSampler BoundSampler = VK_NULL_HANDLE;
    VkDescriptorSet BoundTextureSet = VK_NULL_HANDLE;

    bool FrameInFlight = false;
    bool FrameReadbackValid = false;
    VkFence PendingFence = VK_NULL_HANDLE;
    // Whether FinalFB holds a rendered frame at all. Its contents are undefined
    // until the first RenderFrame() submission completes, and the compositor
    // must not sample undefined memory and call it 3D.
    bool FinalFBHasContent = false;

    // --- composed output ---------------------------------------------------
    // Double-buffered because GetComposedScreen() is read by the presentation
    // path while VBlank() composes on the emulation thread. The front buffer
    // only changes once the new frame has been fully copied out.
    std::array<std::vector<u32>, 2> ComposedColorBuffer;
    u32 ComposedFrontBuffer = 0;
    bool ComposedOutputValid = false;
    u64 ComposedGeneration = 0;

    alignas(64) std::array<u32, 256 * 192> ColorBuffer{};
    alignas(8) u32 ScrolledLine[256]{};
};

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
#endif // GPU3D_VULKAN_H
