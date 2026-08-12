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

#include "GPU3D_Vulkan.h"

#if defined(MELONPRIME_DS) && defined(MELONPRIME_ENABLE_VULKAN)

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstring>
#include <functional>
#include <mutex>

#include "GPU.h"
#include "GPU3D_RasterEdge.h"
#include "GPU3D_RasterDifferential.h"
#include "MelonPrimeStructuredComposition.h"
#include "VulkanContext.h"
#include "VulkanFeatureProbe.h"
#include "VulkanPerf.h"
#include "VulkanPresentedFrame.h"

namespace melonDS
{

namespace
{

constexpr u32 DivRoundUp(u32 value, u32 divisor) noexcept
{
    return (value + divisor - 1) / divisor;
}

// Headroom the per-frame staging ring keeps for texcache uploads on top of the
// span/polygon/clear-bitmap traffic it must always be able to hold. Exceeding
// it is not fatal -- VulkanTextureHeap::Upload() falls back to a dedicated
// scratch buffer -- so this is a "never hit in practice" figure, not a limit.
constexpr VkDeviceSize TextureUploadBudget = 8ull * 1024 * 1024;

// The clear bitmap is a 256x256 R32_UINT image in both halves (colour and
// depth+fog), matching ClearBitmapTex[] in GPU3D_Compute.cpp::Init().
constexpr u32 ClearBitmapDimension = 256;
constexpr VkDeviceSize ClearBitmapBytes =
    static_cast<VkDeviceSize>(ClearBitmapDimension) * ClearBitmapDimension * 4;

// On-disk pipeline cache framing. The Vulkan specification only allows
// pInitialData that came out of vkGetPipelineCacheData, so the payload is
// gated on an exact match of the device identity and the driver's own
// pipelineCacheUUID rather than handed to the driver and hoped for.
constexpr u32 PipelineCacheMagic = 0x4356504Du;     // 'MPVC'
constexpr u32 PipelineCacheVersion = 1;
constexpr const char* PipelineCacheFileName = "melonPrimeDS_vulkan_pipeline_cache.bin";

struct PipelineCacheFileHeader
{
    u32 Magic;
    u32 Version;
    u32 VendorId;
    u32 DeviceId;
    u32 DriverVersion;
    u32 PayloadBytes;
    u8 CacheUUID[VK_UUID_SIZE];
};

// Maps a DS blend mode plus "has texture" onto the rasterise pipeline table,
// exactly like the two shader arrays in ComputeRenderer3D::RenderFrame().
// Blend mode 2 (toon/highlight) is decided by DISP3DCNT bit 1 at dispatch time.
constexpr int NoTextureKinds[5] = {
    0 /* RasteriseKind_NoTexture */,
    0 /* RasteriseKind_NoTexture */,
    -1 /* toon or highlight */,
    0 /* RasteriseKind_NoTexture */,
    7 /* RasteriseKind_ShadowMask */,
};
constexpr int UseTextureKinds[5] = {
    4 /* RasteriseKind_UseTextureModulate */,
    3 /* RasteriseKind_UseTextureDecal */,
    -1 /* toon or highlight */,
    3 /* RasteriseKind_UseTextureDecal */,
    7 /* RasteriseKind_ShadowMask */,
};

// Bytes of the native-resolution capture image the Resolve stage produces. One
// packed r6g6b6a5 word per DS pixel; the resolution is fixed at 256x192 no
// matter what the internal resolution is.
constexpr VkDeviceSize NativeResolveBytes = 256ull * 192ull * 4ull;

// The structured 2D frame the software renderer publishes, as the compositor
// consumes it: fourteen 256x192 planes (four per screen, four capture-source
// planes and two source-B planes), followed by two 192-entry line-metadata
// arrays and 192 four-word capture commands. The layout is mirrored by
// PresentationBuffers.glsl and StructuredComposition's plane numbering.
constexpr u32 StructuredPixelCount = 256u * 192u;
constexpr u32 StructuredPlaneCount = 14u;
constexpr u32 StructuredLineMetaCount = 2u * 192u;
constexpr u32 StructuredCaptureCommandCount = 192u * 4u;
constexpr u32 StructuredInputWords =
    StructuredPlaneCount * StructuredPixelCount
    + StructuredLineMetaCount
    + StructuredCaptureCommandCount;
constexpr VkDeviceSize StructuredInputBytes =
    static_cast<VkDeviceSize>(StructuredInputWords) * sizeof(u32);

} // namespace

struct VulkanRenderer3D::OutputState
{
    struct Slot
    {
        Vk::Buffer StructuredStaging;
        Vk::Buffer StructuredInput;
        Vk::Buffer Composed;
        VulkanPresentedFrame Frame;
        std::atomic<u32> PresenterRefs{0};
    };

    bool Create(const VulkanDevice& device, u32 width, u32 height)
    {
        Device = device;
        const VkDeviceSize screenBytes =
            static_cast<VkDeviceSize>(width) * height * sizeof(u32);

        for (u32 i = 0; i < Slots.size(); ++i)
        {
            Slot& slot = Slots[i];
            if (!slot.StructuredStaging.Create(Device,
                    StructuredInputBytes,
                    VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                    VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    "MelonPrime Vulkan structured staging slot"))
                return false;
            if (!slot.StructuredStaging.Map())
                return false;
            if (!slot.StructuredInput.Create(Device,
                    StructuredInputBytes,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                    "MelonPrime Vulkan structured input slot"))
                return false;
            if (!slot.Composed.Create(Device,
                    screenBytes * 2u,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
                    "MelonPrime Vulkan composed output slot"))
                return false;

            slot.Frame.Buffer = slot.Composed.GetHandle();
            slot.Frame.TopOffset = 0;
            slot.Frame.BottomOffset = screenBytes;
            slot.Frame.Width = width;
            slot.Frame.Height = height;
        }
        return true;
    }

    VulkanDevice Device;
    std::array<Slot, CompositorFramesInFlight> Slots;
    std::mutex Mutex;
    int PublishedSlot = -1;
    u64 NextSerial = 1;
};


// ---------------------------------------------------------------------------
// Construction / lifetime
// ---------------------------------------------------------------------------

std::unique_ptr<VulkanRenderer3D> VulkanRenderer3D::New(melonDS::GPU3D& gpu3D)
{
    VulkanContext& context = VulkanContext::Get();

    // Presentation is requested here even though this class never presents: the
    // surface extensions live on the *instance*, and the screen panel creates
    // its surface from the same instance later.
    if (!context.Acquire(true))
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] renderer creation failed: %s\n",
            context.GetFailureReason().empty()
                ? "the Vulkan runtime is unavailable"
                : context.GetFailureReason().c_str());
        return nullptr;
    }

    if (!context.HasSelectedDevice())
    {
        // Headless selection: present support cannot be evaluated without a
        // surface and is reported as "not checked". The presenter re-runs this
        // with a real surface before it creates a swapchain.
        if (!context.SelectPhysicalDevice(VK_NULL_HANDLE))
        {
            Platform::Log(Platform::LogLevel::Error,
                "[Vulkan] renderer creation failed: %s\n",
                context.GetFailureReason().empty()
                    ? "no physical device satisfies the compute rasterizer's requirements"
                    : context.GetFailureReason().c_str());
            context.Release();
            return nullptr;
        }
    }

    std::unique_ptr<VulkanRenderer3D> renderer(new VulkanRenderer3D(gpu3D));
    renderer->Context = &context;
    return renderer;
}

VulkanRenderer3D::VulkanRenderer3D(melonDS::GPU3D& gpu3D)
    // TextureHeap is declared before Texcache, so its address is already valid
    // here; the loader only dereferences it after Init() populated the heap.
    : Renderer3D(gpu3D), Texcache(gpu3D.GPU, TexcacheVulkanLoader(&TextureHeap))
{
    ClearBitmap[0] = std::make_unique<u32[]>(ClearBitmapDimension * ClearBitmapDimension);
    ClearBitmap[1] = std::make_unique<u32[]>(ClearBitmapDimension * ClearBitmapDimension);
    YSpanSetups = std::make_unique<SpanSetupY[]>(MaxYSpanSetups);
    RenderPolygons = std::make_unique<RenderPolygon[]>(MaxRenderPolygons);
    Pipelines.fill(VK_NULL_HANDLE);
}

VulkanRenderer3D::~VulkanRenderer3D()
{
    Stop();

    if (Context)
    {
        Context->Release();
        Context = nullptr;
    }
}

bool VulkanRenderer3D::Init()
{
    if (!Context || !Context->IsReady() || !Context->HasSelectedDevice())
        return false;

    // Request presentation-related capabilities even when the renderer is the
    // first shared-device client. The presenter can then attach its surface to
    // this device instead of having to replace a live renderer device.
    const VulkanLowLatencyRequest presentationCapabilities{true, true};
    if (!Device.Create(*Context, "Vulkan", presentationCapabilities))
    {
        Platform::Log(Platform::LogLevel::Error,
            "[Vulkan] renderer init failed stage=3D-device actual=Software reason=%s\n",
            Device.GetFailureReason().c_str());
        return false;
    }

    if (!Frames.Create(Device, Device.GetMainQueueFamily(), RendererFramesInFlight))
        return false;

    // Same queue family, separate command pool / command buffer / fence. Both
    // rings submit to the same queue, so the compositor's barriers can depend on
    // the rasterizer's earlier submission through submission order.
    if (!ComposeFrames.Create(
            Device, Device.GetMainQueueFamily(), CompositorFramesInFlight))
        return false;

    if (!Layouts.Create(Device.Fns(), Device.GetHandle()))
        return false;

    Vk::DescriptorPoolSizing sizing;
    sizing.FramesInFlight = DescriptorFramesInFlight;
    sizing.RasterizerSetsPerFrame = RasterizerSetsPerFrame;
    // Slot 0 carries the untextured binding used by every stage that does not
    // sample a DS texture (DepthBlend still needs the clear-bitmap samplers in
    // this set), slots 1..MaxVariants one per distinct texture binding in a
    // frame. A set cannot be rewritten while the frame that referenced it is
    // still pending, so each distinct binding needs its own.
    sizing.TextureSetsPerFrame = MaxVariants + 1;
    if (!Descriptors.Create(Device.Fns(), Device.GetHandle(), Layouts, sizing))
        return false;

    if (!Samplers.Create(Device))
        return false;

    TextureHeap.Init(&Device, &Frames);

    if (!CreatePipelineCache())
        return false;

    if (!CreateFixedResources())
        return false;

    ClearBitmapDirty = 0x3;
    NeedsFinalFBTransition = true;
    PlaceholdersInitialized = false;
    Initialized = true;

    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] 3D renderer initialized on \"%s\" (up to %dx internal resolution)\n",
        Device.GetProfile().DeviceName.c_str(), Device.GetMaxScaleFactor());
    return true;
}

void VulkanRenderer3D::Stop()
{
    if (Device.IsValid())
    {
        // Permitted WaitIdle site: teardown. Every destroy below assumes no
        // command buffer still references the object.
        Frames.WaitIdle();
        ComposeFrames.WaitIdle();

        SavePipelineCache();

        // Retires the cached texture images through the deferred queue, which
        // Frames.Destroy() then drains -- so this has to happen first.
        Texcache.Reset();

        ReleasePipelines();

        if (PipelineCache != VK_NULL_HANDLE)
        {
            Device.Fns().DestroyPipelineCache(Device.GetHandle(), PipelineCache, nullptr);
            PipelineCache = VK_NULL_HANDLE;
        }
    }

    ReleaseScaleDependentResources();

    NativeReadback.Destroy();
    NativeResolveBuffer.Destroy();
    DummyCaptureImage.Destroy();
    DummyTextureImage.Destroy();
    ClearBitmapImage[0].Destroy();
    ClearBitmapImage[1].Destroy();
    YSpanSetupBuffer.Destroy();
    PolygonBuffer.Destroy();
    MetaUniformBuffer.Destroy();
    FrameStaging.Destroy();

    Descriptors.Destroy();
    Layouts.Destroy();
    Samplers.Destroy();

    TextureHeap.Shutdown();

    ComposeFrames.Destroy();
    Frames.Destroy();
    Device.Destroy();

    FrameInFlight = false;
    FrameReadbackValid = false;
    PendingFence = VK_NULL_HANDLE;
    FinalFBHasContent = false;
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    ComposedOutput.reset();
    Initialized = false;
}

void VulkanRenderer3D::Reset()
{
    if (!Initialized)
        return;

    // No WaitIdle here: a reset can land while a submission is still in flight,
    // and the texcache images it drops are exactly what the deferred destroy
    // queue exists for -- they are retired against the current frame number and
    // collected once that frame's fence signals.
    Texcache.Reset();
    ClearBitmapDirty = 0x3;
    FrameInFlight = false;
    FrameReadbackValid = false;
    PendingFence = VK_NULL_HANDLE;
    // FinalFB still holds the last ROM's final frame. Nothing has invalidated
    // it, but nothing has re-rendered it either, so the compositor must go back
    // to treating it as "no 3D" until the next RenderFrame() lands.
    FinalFBHasContent = false;
    ComposedOutputValid = false;
    ComposedGeneration = 0;
    ColorBuffer.fill(0);
}

void VulkanRenderer3D::SetRuntimeFailure(std::string reason)
{
    if (RuntimeFailed)
        return;

    RuntimeFailed = true;
    RuntimeFailureReason = reason.empty() ? "unspecified Vulkan renderer failure" : std::move(reason);
    Platform::Log(Platform::LogLevel::Error,
        "[Vulkan] runtime failure: %s\n", RuntimeFailureReason.c_str());
}


// ---------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------

bool VulkanRenderer3D::CreateFixedResources()
{
    const VkDeviceSize uniformAlignment =
        std::max<VkDeviceSize>(16, Device.GetLimits().minUniformBufferOffsetAlignment);
    MetaUniformStride = Vk::AlignUp(sizeof(MetaUniform), uniformAlignment);

    // Host-visible and persistently mapped: 592 bytes per frame is far below
    // the cost of staging it, and the host write is made visible to the device
    // by the queue submission's implicit host-write domain operation.
    if (!MetaUniformBuffer.Create(Device,
            MetaUniformStride * DescriptorFramesInFlight,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            "MelonPrime Vulkan MetaUniform"))
        return false;
    if (!MetaUniformBuffer.Map())
        return false;

    if (!PolygonBuffer.Create(Device,
            sizeof(RenderPolygon) * MaxRenderPolygons,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan PolygonBuffer"))
        return false;

    if (!YSpanSetupBuffer.Create(Device,
            sizeof(SpanSetupY) * MaxYSpanSetups,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan YSpanSetups"))
        return false;

    for (int i = 0; i < 2; i++)
    {
        Vk::Image::CreateInfo info;
        info.Format = VK_FORMAT_R32_UINT;
        info.Width = ClearBitmapDimension;
        info.Height = ClearBitmapDimension;
        info.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        info.DebugName = i == 0
            ? "MelonPrime Vulkan clear bitmap colour"
            : "MelonPrime Vulkan clear bitmap depth";
        if (!ClearBitmapImage[i].Create(Device, info))
            return false;
    }

    {
        // Bound at set 1 binding 0 for untextured variants. The shader never
        // samples it there, but a descriptor that is statically reachable must
        // still be backed by a valid view, and it is cleared once so nothing
        // ever reads uninitialised device memory.
        Vk::Image::CreateInfo info;
        info.Format = VK_FORMAT_R8G8B8A8_UINT;
        info.Width = 1;
        info.Height = 1;
        info.ArrayLayers = 1;
        info.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.ViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        info.DebugName = "MelonPrime Vulkan dummy texture array";
        if (!DummyTextureImage.Create(Device, info))
            return false;
    }

    {
        // Capture128Texture / Capture256Texture (set 1 bindings 1 and 2).
        //
        // OpenGL binds the 2D engine's display-capture targets here. Vulkan
        // instead resolves capture textures through the persistent sidecar,
        // keyed by the packed capture reference in the raster push constants.
        // These legacy bindings still have to contain valid float array views
        // because Rasterise.comp keeps the shared descriptor layout stable.
        Vk::Image::CreateInfo info;
        info.Format = VK_FORMAT_R8G8B8A8_UNORM;
        info.Width = 1;
        info.Height = 1;
        info.ArrayLayers = 1;
        info.Usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        info.ViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        info.DebugName = "MelonPrime Vulkan dummy capture array";
        if (!DummyCaptureImage.Create(Device, info))
            return false;
    }

    // Destination of the Resolve compute stage. Device-local rather than
    // host-visible: the stage writes it with scattered stores from 49152
    // invocations, and a write-combined host mapping would make that an order of
    // magnitude slower than the extra buffer-to-buffer copy costs.
    if (!NativeResolveBuffer.Create(Device,
            NativeResolveBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan native resolve"))
        return false;

    if (!NativeReadback.Create(Device, NativeResolveBytes, "MelonPrime Vulkan native readback"))
        return false;

    return true;
}

bool VulkanRenderer3D::CreateScaleDependentResources()
{
    ReleaseScaleDependentResources();

    const VkDeviceSize screenPixels =
        static_cast<VkDeviceSize>(ScreenWidth) * static_cast<VkDeviceSize>(ScreenHeight);
    const VkDeviceSize workTiles = static_cast<VkDeviceSize>(MaxWorkTiles);
    const VkDeviceSize tileGrid =
        static_cast<VkDeviceSize>(TilesPerLine) * static_cast<VkDeviceSize>(TileLines);

    for (int i = 0; i < 3; i++)
    {
        static const char* const names[3] = {
            "MelonPrime Vulkan ColorTiles",
            "MelonPrime Vulkan DepthTiles",
            "MelonPrime Vulkan AttrTiles",
        };
        if (!TileBuffers[i].Create(Device,
                4 * static_cast<VkDeviceSize>(TileSize) * TileSize * workTiles,
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, names[i]))
            return false;
    }

    if (!ResultBuffer.Create(Device,
            4 * 3 * 2 * screenPixels,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan ResultBuffer"))
        return false;

    const VkDeviceSize resultWinnerBytes = ScaleFactor == 1
        ? 4 * 2 * screenPixels
        : sizeof(u32);
    if (!ResultWinnerBuffer.Create(Device,
            resultWinnerBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan ResultWinner"))
        return false;

    const VkDeviceSize binResultSize =
        sizeof(BinResultHeader)
        + tileGrid * static_cast<VkDeviceSize>(CoarseBinStride) * 4    // BinnedMaskCoarse
        + tileGrid * static_cast<VkDeviceSize>(BinStride) * 4          // BinnedMask
        + tileGrid * static_cast<VkDeviceSize>(BinStride) * 4;         // WorkOffsets

    // INDIRECT_BUFFER as well as STORAGE_BUFFER: unlike D3D12 there is no
    // exclusive resource state, so the same buffer carries the binning results
    // and the per-variant VkDispatchIndirectCommand triples the rasterise
    // stage dispatches from. Only a memory dependency is needed between the
    // two uses, not a copy into a separate arguments buffer.
    if (!BinResultBuffer.Create(Device,
            binResultSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan BinResult"))
        return false;

    if (!WorkDescBuffer.Create(Device,
            workTiles * 2 * 4 * 2,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan WorkDescs"))
        return false;

    if (!XSpanSetupBuffer.Create(Device,
            sizeof(SpanSetupX) * static_cast<VkDeviceSize>(MaxYSpanIndices),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan XSpanSetups"))
        return false;

    if (!SetupIndicesBuffer.Create(Device,
            sizeof(SetupIndices) * static_cast<VkDeviceSize>(MaxYSpanIndices),
            VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan SetupIndices"))
        return false;
    if (!SetupIndicesBuffer.CreateView(
            Vk::SetupIndicesFormat, 0, VK_WHOLE_SIZE, "MelonPrime Vulkan SetupIndices"))
        return false;

    {
        Vk::Image::CreateInfo info;
        info.Format = Vk::FinalFramebufferFormat;
        info.Width = static_cast<u32>(ScreenWidth);
        info.Height = static_cast<u32>(ScreenHeight);
        // STORAGE for FinalPass.comp's imageStore, TRANSFER_SRC for the
        // native-resolution downscale GetLine() reads (and, in phase 8-9, for
        // the compositor).
        info.Usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        info.ViewType = VK_IMAGE_VIEW_TYPE_2D;
        info.DebugName = "MelonPrime Vulkan FinalFB";
        if (!FinalFB.Create(Device, info))
            return false;
    }

    auto output = std::make_shared<OutputState>();
    if (!output->Create(
            Device, static_cast<u32>(ScreenWidth), static_cast<u32>(ScreenHeight)))
        return false;
    ComposedOutput = std::move(output);
    ComposedOutputValid = false;
    ComposedGeneration = 0;

    const VkDeviceSize captureSidecarBytes = static_cast<VkDeviceSize>(8u)
        * 256u * 256u * static_cast<u32>(ScaleFactor) * static_cast<u32>(ScaleFactor)
        * sizeof(u32);
    if (!CaptureSidecarBuffer.Create(Device,
            captureSidecarBytes,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan high-resolution capture sidecar"))
        return false;

    // DepthBlend is run once per bounded polygon batch. Preserve the two-bit
    // shadow stencil and the previous-shadow-mask flag between batches so a
    // boundary is observationally identical to one unbounded pass.
    if (!BlendStateBuffer.Create(Device,
            screenPixels * 4,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0,
            "MelonPrime Vulkan depth-blend continuation state"))
        return false;

    // The ring has to hold one frame's worth of every CPU-side upload at once:
    // the two clear-bitmap halves, the Y span records, the per-scanline index
    // table, the polygon records, plus texcache traffic.
    const VkDeviceSize stagingCapacity = Vk::AlignUp(
        2 * ClearBitmapBytes
        + sizeof(SpanSetupY) * MaxYSpanSetups
        + sizeof(SetupIndices) * static_cast<VkDeviceSize>(MaxYSpanIndices)
        + sizeof(RenderPolygon) * MaxRenderPolygons
        + TextureUploadBudget,
        1024ull * 1024ull);

    FrameStaging.Destroy();
    if (!FrameStaging.Create(Device, stagingCapacity, "MelonPrime Vulkan staging ring"))
        return false;

    // The new FinalFB starts UNDEFINED and has to be moved into GENERAL by
    // the next frame's command buffer.
    NeedsFinalFBTransition = true;
    return true;
}

void VulkanRenderer3D::ReleaseScaleDependentResources()
{
    // Called from SetRenderSettings() after a WaitIdle and from Stop(), so
    // immediate destruction is safe: nothing in flight can reference these.
    ComposedOutput.reset();
    CaptureSidecarBuffer.Destroy();
    FinalFB.Destroy();
    SetupIndicesBuffer.Destroy();
    XSpanSetupBuffer.Destroy();
    WorkDescBuffer.Destroy();
    BlendStateBuffer.Destroy();
    BinResultBuffer.Destroy();
    ResultWinnerBuffer.Destroy();
    ResultBuffer.Destroy();
    for (auto& buffer : TileBuffers)
        buffer.Destroy();
}

void VulkanRenderer3D::ReleasePipelines()
{
    if (!Device.IsValid())
    {
        Pipelines.fill(VK_NULL_HANDLE);
        return;
    }

    const Vk::DeviceDispatch& fns = Device.Fns();
    for (VkPipeline& pipeline : Pipelines)
    {
        if (pipeline != VK_NULL_HANDLE)
        {
            fns.DestroyPipeline(Device.GetHandle(), pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
        }
    }
}


// ---------------------------------------------------------------------------
// Render settings
// ---------------------------------------------------------------------------

void VulkanRenderer3D::SetRenderSettings(int scale, bool hiresCoordinates)
{
    if (!Initialized)
        return;

    if (scale == ScaleFactor)
    {
        // Like the OpenGL compute renderer, the high-resolution-coordinates
        // toggle must not tear down GPU resources: MelonPrimeDS applies it live
        // during a match.
        HiresCoordinates = hiresCoordinates;
        return;
    }

    if (scale < 1 || scale > Vk::MaxSupportedScaleFactor)
    {
        SetRuntimeFailure(
            "internal resolution " + std::to_string(scale) + "x is outside the supported 1x-"
            + std::to_string(Vk::MaxSupportedScaleFactor) + "x range");
        return;
    }

    // Scale refusal, not clamping. VulkanFeatureProbe walked the scale factors
    // against this device's real VkPhysicalDeviceLimits and device-local memory
    // budget (maxStorageBufferRange, maxTexelBufferElements,
    // maxComputeWorkGroupInvocations / Size / Count, maxImageDimension2D) and
    // recorded the highest one it can actually run. Silently dropping to a
    // lower resolution than the user asked for would misreport what is on
    // screen, so this fails loudly instead.
    const int maxScale = Device.GetMaxScaleFactor();
    if (scale > maxScale)
    {
        SetRuntimeFailure(
            "internal resolution " + std::to_string(scale) + "x exceeds what \""
            + Device.GetProfile().DeviceName + "\" supports (maximum " + std::to_string(maxScale)
            + "x, see the Vulkan device probe log for the limit that decided it)");
        return;
    }

    // Permitted WaitIdle site: a resolution change destroys every
    // resolution-sized resource, and there is no cheaper correct way to know
    // no command buffer still references them. Both rings, because the
    // compositor references FinalFB and its own output buffer.
    Frames.WaitIdle();
    ComposeFrames.WaitIdle();

    ScaleFactor = scale;
    ScreenWidth = 256 * ScaleFactor;
    ScreenHeight = 192 * ScaleFactor;
    HiresCoordinates = hiresCoordinates;

    // Same tile-geometry derivation as ComputeRenderer3D::SetRenderSettings():
    // the tile size doubles at 5x and again at 9x, and the coarse tile grows a
    // row at 9x.
    const int range = (ScaleFactor >= 5) + (ScaleFactor >= 9);
    TileSize = 8 << range;
    CoarseTileCountY = 4 + ((range >> 1) << 1);
    ClearCoarseBinMaskLocalSize = 64 - ((range >> 1) << 4);
    CoarseTileArea = CoarseTileCountX * CoarseTileCountY;
    CoarseTileW = CoarseTileCountX * TileSize;
    CoarseTileH = CoarseTileCountY * TileSize;

    const int tileShift = 3 + range;
    TilesPerLine = ScreenWidth >> tileShift;
    TileLines = ScreenHeight >> tileShift;
    MaxWorkTiles = (TilesPerLine * TileLines) << 4;

    // Those four constants select workgroup sizes, so they had to be literals
    // at SPIR-V generation time; the generator emitted one module set per
    // bucket and this picks the matching one.
    TileGeometryBucket = VulkanShaders::TileGeometryBucketForScale(static_cast<u32>(ScaleFactor));

    // A valid DS polygon can cover every output scanline. Size this from the
    // actual worst case instead of OpenGL's 64-lines-per-polygon heuristic so
    // span setup never silently drops later polygons. InterpSpans is already
    // dispatched in device-sized chunks.
    MaxYSpanIndices = ScreenHeight * MaxRenderPolygons;

    YSpanIndices.assign(static_cast<size_t>(MaxYSpanIndices), SetupIndices{});

    ReleasePipelines();
    ShaderStepIdx = 0;

    if (!CreateScaleDependentResources())
    {
        SetRuntimeFailure(
            "failed to allocate render targets for " + std::to_string(ScaleFactor)
            + "x internal resolution");
        return;
    }

    ClearBitmapDirty = 0x3;
    FrameInFlight = false;
    FrameReadbackValid = false;
    PendingFence = VK_NULL_HANDLE;
    // The old FinalFB was destroyed; the new one starts UNDEFINED.
    FinalFBHasContent = false;

    const Vk::ResolutionBudget budget = Vk::ResolutionBudget::ForScaleFactor(ScaleFactor);
    const VkDeviceSize composedScreenBytes =
        static_cast<VkDeviceSize>(ScreenWidth) * ScreenHeight * sizeof(u32) * 2u;
    const VkDeviceSize captureSidecarBytes = 8ull * 256ull * 256ull
        * static_cast<VkDeviceSize>(ScaleFactor) * ScaleFactor * sizeof(u32);
    // ResolutionBudget includes one compositor output because the feature
    // probe must gate its storage-buffer range. Remove it here so this runtime
    // breakdown does not count that allocation both as raster and compositor.
    const VkDeviceSize rasterDeviceBytes =
        budget.TotalDeviceBytes - composedScreenBytes - captureSidecarBytes;
    const VkDeviceSize compositorDeviceBytes =
        (composedScreenBytes + StructuredInputBytes) * CompositorFramesInFlight;
    Platform::Log(Platform::LogLevel::Info,
        "[Vulkan] internal resolution %dx -> 3D output %dx%d, tiles %dx%d (%dpx), "
        "tile-geometry bucket %u, capture resolve 256x192; memory raster=%.1f MiB "
        "capture-sidecar=%.1f MiB "
        "compositor-device=%.1f MiB compositor-host=%.1f MiB\n",
        ScaleFactor, ScreenWidth, ScreenHeight, TilesPerLine, TileLines, TileSize,
        TileGeometryBucket,
        static_cast<double>(rasterDeviceBytes) / (1024.0 * 1024.0),
        static_cast<double>(captureSidecarBytes) / (1024.0 * 1024.0),
        static_cast<double>(compositorDeviceBytes) / (1024.0 * 1024.0),
        static_cast<double>(StructuredInputBytes * CompositorFramesInFlight) / (1024.0 * 1024.0));
}


// ---------------------------------------------------------------------------
// Pipelines
// ---------------------------------------------------------------------------

bool VulkanRenderer3D::CreatePipelineCache()
{
    const Vk::DeviceDispatch& fns = Device.Fns();
    const VkPhysicalDeviceProperties& properties = Device.GetProfile().Properties;

    std::vector<u8> payload;

    if (Platform::FileHandle* file = Platform::OpenLocalFile(PipelineCacheFileName, Platform::FileMode::Read))
    {
        PipelineCacheFileHeader header{};
        const bool headerRead =
            Platform::FileRead(&header, sizeof(header), 1, file) == 1
            && header.Magic == PipelineCacheMagic
            && header.Version == PipelineCacheVersion
            && header.VendorId == properties.vendorID
            && header.DeviceId == properties.deviceID
            && header.DriverVersion == properties.driverVersion
            && std::memcmp(header.CacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE) == 0;

        if (headerRead && header.PayloadBytes > 0
            && Platform::FileLength(file) == sizeof(header) + header.PayloadBytes)
        {
            payload.resize(header.PayloadBytes);
            if (Platform::FileRead(payload.data(), header.PayloadBytes, 1, file) != 1)
                payload.clear();
        }
        Platform::CloseFile(file);
    }

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    info.initialDataSize = payload.size();
    info.pInitialData = payload.empty() ? nullptr : payload.data();

    VkResult res = fns.CreatePipelineCache(Device.GetHandle(), &info, nullptr, &PipelineCache);
    if (res != VK_SUCCESS && !payload.empty())
    {
        // A driver is allowed to reject data it does not recognise. Losing the
        // cache only costs compile time, so retry empty rather than fail.
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] the stored pipeline cache was rejected (%s); starting empty\n",
            Vk::FormatResult(res).c_str());
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        res = fns.CreatePipelineCache(Device.GetHandle(), &info, nullptr, &PipelineCache);
    }

    if (!MELONPRIME_VK_CHECK("vkCreatePipelineCache", res))
    {
        PipelineCache = VK_NULL_HANDLE;
        // Pipeline creation accepts VK_NULL_HANDLE for the cache, so this is
        // recoverable; report it and continue uncached.
        Platform::Log(Platform::LogLevel::Warn,
            "[Vulkan] continuing without a pipeline cache\n");
    }

    return true;
}

void VulkanRenderer3D::SavePipelineCache()
{
    if (PipelineCache == VK_NULL_HANDLE || !Device.IsValid())
        return;

    const Vk::DeviceDispatch& fns = Device.Fns();

    size_t size = 0;
    if (fns.GetPipelineCacheData(Device.GetHandle(), PipelineCache, &size, nullptr) != VK_SUCCESS
        || size == 0)
        return;

    std::vector<u8> payload(size);
    if (fns.GetPipelineCacheData(Device.GetHandle(), PipelineCache, &size, payload.data()) != VK_SUCCESS)
        return;
    payload.resize(size);

    Platform::FileHandle* file =
        Platform::OpenLocalFile(PipelineCacheFileName, Platform::FileMode::Write);
    if (!file)
        return;

    const VkPhysicalDeviceProperties& properties = Device.GetProfile().Properties;

    PipelineCacheFileHeader header{};
    header.Magic = PipelineCacheMagic;
    header.Version = PipelineCacheVersion;
    header.VendorId = properties.vendorID;
    header.DeviceId = properties.deviceID;
    header.DriverVersion = properties.driverVersion;
    header.PayloadBytes = static_cast<u32>(payload.size());
    std::memcpy(header.CacheUUID, properties.pipelineCacheUUID, VK_UUID_SIZE);

    Platform::FileWrite(&header, sizeof(header), 1, file);
    Platform::FileWrite(payload.data(), payload.size(), 1, file);
    Platform::CloseFile(file);
}

bool VulkanRenderer3D::BuildPipeline(u32 pipelineIndex)
{
    if (pipelineIndex >= Pipelines.size())
        return false;

    const Vk::DeviceDispatch& fns = Device.Fns();
    VkDevice device = Device.GetHandle();

    const VulkanShaders::ShaderModule& blob =
        VulkanShaders::Modules[TileGeometryBucket][pipelineIndex];
    if (!blob.Words || blob.WordCount == 0)
    {
        SetRuntimeFailure(
            std::string("no SPIR-V module for pipeline ")
            + VulkanShaders::PipelineNames[pipelineIndex]);
        return false;
    }

    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = static_cast<size_t>(blob.WordCount) * sizeof(u32);
    moduleInfo.pCode = blob.Words;

    VkShaderModule module = VK_NULL_HANDLE;
    if (!MELONPRIME_VK_CHECK("vkCreateShaderModule",
            fns.CreateShaderModule(device, &moduleInfo, nullptr, &module)))
    {
        SetRuntimeFailure(
            std::string("could not create the shader module for ")
            + VulkanShaders::PipelineNames[pipelineIndex]);
        return false;
    }

    // Specialization constants 0/1/2, declared as `const int` in Common.glsl.
    // Everything the shaders derive from them (FramebufferStride, TilesPerLine,
    // the BinningMaskAndOffset and ResultValue section offsets) folds through
    // OpSpecConstantOp at pipeline creation, so a resolution change costs a
    // pipeline rebuild but nothing at runtime.
    struct SpecializationData
    {
        s32 ScreenWidth;
        s32 ScreenHeight;
        s32 MaxWorkTiles;
    } specData{ ScreenWidth, ScreenHeight, MaxWorkTiles };

    const VkSpecializationMapEntry entries[3] = {
        { VulkanShaders::SpecConstantId_ScreenWidth,  offsetof(SpecializationData, ScreenWidth),  sizeof(s32) },
        { VulkanShaders::SpecConstantId_ScreenHeight, offsetof(SpecializationData, ScreenHeight), sizeof(s32) },
        { VulkanShaders::SpecConstantId_MaxWorkTiles, offsetof(SpecializationData, MaxWorkTiles), sizeof(s32) },
    };

    VkSpecializationInfo specInfo{};
    specInfo.mapEntryCount = 3;
    specInfo.pMapEntries = entries;
    specInfo.dataSize = sizeof(specData);
    specInfo.pData = &specData;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = module;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.stage.pSpecializationInfo = &specInfo;
    pipelineInfo.layout = Layouts.GetPipelineLayout();

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult res = fns.CreateComputePipelines(
        device, PipelineCache, 1, &pipelineInfo, nullptr, &pipeline);

    // The module is only needed while the pipeline is being created; the
    // pipeline keeps whatever it needs from it.
    fns.DestroyShaderModule(device, module, nullptr);

    if (!MELONPRIME_VK_CHECK("vkCreateComputePipelines", res))
    {
        SetRuntimeFailure(
            std::string("could not create the compute pipeline for ")
            + VulkanShaders::PipelineNames[pipelineIndex]);
        return false;
    }

    Device.SetDebugName(VK_OBJECT_TYPE_PIPELINE, pipeline, VulkanShaders::PipelineNames[pipelineIndex]);

    if (Pipelines[pipelineIndex] != VK_NULL_HANDLE)
        fns.DestroyPipeline(device, Pipelines[pipelineIndex], nullptr);
    Pipelines[pipelineIndex] = pipeline;
    return true;
}

void VulkanRenderer3D::ShaderCompileStep(int& current, int& count)
{
    count = ShaderStepCount;
    current = std::min(ShaderStepIdx, ShaderStepCount - 1);

    if (RuntimeFailed || ScaleFactor <= 0 || ShaderStepIdx >= ShaderStepCount)
        return;

    const int step = ShaderStepIdx;
    ShaderStepIdx++;
    current = step;

    if (!BuildPipeline(static_cast<u32>(step)))
    {
        // BuildPipeline() already recorded the failure; stop stepping so the
        // frontend does not spin through 32 more doomed creations.
        ShaderStepIdx = ShaderStepCount;
    }
}


// ---------------------------------------------------------------------------
// CPU-side span setup -- transcribed from ComputeRenderer3D
// ---------------------------------------------------------------------------

void VulkanRenderer3D::SetupAttrs(SpanSetupY* span, Polygon* poly, int from, int to) const
{
    span->Z0 = poly->FinalZ[from];
    span->W0 = poly->FinalW[from];
    span->Z1 = poly->FinalZ[to];
    span->W1 = poly->FinalW[to];
    span->ColorR0 = poly->Vertices[from]->FinalColor[0];
    span->ColorG0 = poly->Vertices[from]->FinalColor[1];
    span->ColorB0 = poly->Vertices[from]->FinalColor[2];
    span->ColorR1 = poly->Vertices[to]->FinalColor[0];
    span->ColorG1 = poly->Vertices[to]->FinalColor[1];
    span->ColorB1 = poly->Vertices[to]->FinalColor[2];
    span->TexcoordU0 = poly->Vertices[from]->TexCoords[0];
    span->TexcoordV0 = poly->Vertices[from]->TexCoords[1];
    span->TexcoordU1 = poly->Vertices[to]->TexCoords[0];
    span->TexcoordV1 = poly->Vertices[to]->TexCoords[1];
}

void VulkanRenderer3D::SetupYSpanDummy(
    RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int vertex, int side,
    s32 positions[10][2]) const
{
    const s32 x0 = positions[vertex][0];
    span->DxInitial = 0;

    span->X0 = span->X1 = x0;
    span->XMin = x0;
    span->XMax = x0;
    span->Y0 = span->Y1 = positions[vertex][1];

    const s32 boundsXMin = RasterEdge::ConservativeRightVerticalMin(x0, side != 0);
    if (boundsXMin < rp->XMin)
    {
        rp->XMin = boundsXMin;
        rp->XMinY = span->Y0;
    }
    if (span->XMax > rp->XMax)
    {
        rp->XMax = span->XMax;
        rp->XMaxY = span->Y0;
    }

    span->Increment = 0;

    span->I0 = span->I1 = span->IRecip = 0;
    span->Linear = 1;

    span->XCovIncr = 0;

    span->IsDummy = 1;

    SetupAttrs(span, poly, vertex, vertex);
}

void VulkanRenderer3D::SetupYSpan(
    RenderPolygon* rp, SpanSetupY* span, Polygon* poly, int from, int to, int side,
    s32 positions[10][2]) const
{
    span->X0 = positions[from][0];
    span->X1 = positions[to][0];
    span->Y0 = positions[from][1];
    span->Y1 = positions[to][1];

    SetupAttrs(span, poly, from, to);

    s32 minXY, maxXY;
    bool negative = false;
    if (span->X1 > span->X0)
    {
        span->XMin = span->X0;
        span->XMax = span->X1 - 1;

        minXY = span->Y0;
        maxXY = span->Y1;
    }
    else if (span->X1 < span->X0)
    {
        span->XMin = span->X1;
        span->XMax = span->X0 - 1;
        negative = true;

        minXY = span->Y1;
        maxXY = span->Y0;
    }
    else
    {
        span->XMin = span->X0;
        span->XMax = span->XMin;

        // doesn't matter for a completely vertical slope
        minXY = span->Y0;
        maxXY = span->Y0;
    }

    const s32 boundsXMin = RasterEdge::ConservativeRightVerticalMin(
        span->XMin, side && span->X0 == span->X1);
    if (boundsXMin < rp->XMin)
    {
        rp->XMin = boundsXMin;
        rp->XMinY = minXY;
    }
    if (span->XMax > rp->XMax)
    {
        rp->XMax = span->XMax;
        rp->XMaxY = maxXY;
    }

    span->IsDummy = 0;

    const s32 xlen = span->XMax + 1 - span->XMin;
    const s32 ylen = span->Y1 - span->Y0;
    span->Increment = RasterEdge::CalculateSlopeIncrement(
        span->X0, span->X1, span->XMin, span->XMax, span->Y0, span->Y1);

    const bool xMajor = (span->Increment > 0x40000);

    if (side)
    {
        // right
        if (xMajor)
            span->DxInitial = negative ? (0x20000 + 0x40000) : (span->Increment - 0x20000);
        else if (span->Increment != 0)
            span->DxInitial = negative ? 0x40000 : 0;
        else
            span->DxInitial = 0;
    }
    else
    {
        // left
        if (xMajor)
            span->DxInitial = negative ? ((span->Increment - 0x20000) + 0x40000) : 0x20000;
        else if (span->Increment != 0)
            span->DxInitial = negative ? 0x40000 : 0;
        else
            span->DxInitial = 0;
    }

    if (xMajor)
    {
        // used for calculating AA coverage
        span->XCovIncr = (ylen << 10) / xlen;
    }

    const s32 interpolationOffset = RasterEdge::InterpolationOriginOffset(
        span->Increment, side != 0, negative);
    span->I0 = span->Y0 - interpolationOffset;
    span->I1 = span->Y1 - interpolationOffset;

    if (span->I0 != span->I1)
        span->IRecip = (1 << 30) / (span->I1 - span->I0);
    else
        span->IRecip = 0;

    span->Linear = ((span->W0 == span->W1) && !(span->W0 & 0x7E) && !(span->W1 & 0x7E)) ? 1u : 0u;

    if ((span->W0 & 0x1) && !(span->W1 & 0x1))
    {
        span->W0n = (span->W0 - 1) >> 1;
        span->W0d = (span->W0 + 1) >> 1;
        span->W1d = span->W1 >> 1;
    }
    else
    {
        span->W0n = span->W0 >> 1;
        span->W0d = span->W0 >> 1;
        span->W1d = span->W1 >> 1;
    }
}

u32 VulkanRenderer3D::BuildPolygons(int& numYSpans, int& numSetupIndices, u32& numPolygons)
{
    numYSpans = 0;
    numSetupIndices = 0;
    numPolygons = 0;

    // Unlike classic OpenGL and the native Metal raster paths, this compute
    // rasterizer never splits an N-sided DS polygon into GPU triangles. It
    // walks the polygon's left/right edges and interpolates one X span per
    // scanline, matching GPU3D_Compute. The Better Polygons center-fan option
    // exists only to reduce errors introduced by triangle splitting, so it is
    // deliberately not a Vulkan setting here.

    // Games spam small textures, so same-sized textures share an array texture
    // and polygons that agree on texture + blend mode + wrap mode share a
    // rasterise dispatch. Fewer variants means bigger batches.
    u32 numVariants = 0;
    u32 prevVariant = 0;
    u32 prevTexLayer = 0;
    Variant* variants = Variants.data();
    VariantLookup.Reset();
    u32 captureLastVariant[16]{};

    int captureInfo[16];
    GPU.GetCaptureInfo_Texture(captureInfo);

    const bool enableTextureMaps = (GPU3D.RenderDispCnt & (1 << 0)) != 0;
    Polygon* previousPolygon = nullptr;

    for (u32 sourceIndex = 0; sourceIndex < GPU3D.RenderNumPolygons; sourceIndex++)
    {
        Polygon* polygon = GPU3D.RenderPolygonRAM[sourceIndex];
        if (polygon->Degenerate)
            continue;

        // Software omits degenerate polygons before rasterisation. Keep the
        // GPU polygon/index buffers compact so every setup and work descriptor
        // still addresses a valid contiguous record.
        const u32 i = numPolygons;

        const u32 nverts = polygon->NumVertices;
        u32 vtop = polygon->VTop, vbot = polygon->VBottom;

        u32 curVL = vtop, curVR = vtop;
        u32 nextVL, nextVR;

        RenderPolygons[i].FirstXSpan = static_cast<u32>(numSetupIndices);
        RenderPolygons[i].Attr = polygon->Attr;
        RenderPolygons[i].FacingView = polygon->FacingView ? 1u : 0u;

        bool foundVariant = false;
        if (previousPolygon)
        {
            // If the whole texture attribute matches, the texture layer matches
            // too, so the previous polygon's variant can be reused directly.
            foundVariant = previousPolygon->TexParam == polygon->TexParam
                && previousPolygon->TexPalette == polygon->TexPalette
                && (previousPolygon->Attr & 0x30) == (polygon->Attr & 0x30)
                && previousPolygon->IsShadowMask == polygon->IsShadowMask;
        }

        if (!foundVariant)
        {
            Variant variant;
            variant.BlendMode = polygon->IsShadowMask ? 4 : ((polygon->Attr >> 4) & 0x3);
            variant.Texture = 0;
            variant.WrapS = 0;
            variant.WrapT = 0;
            variant.CaptureReference = 0;
            variant.CaptureYOffset = 0;
            variant.CaptureType = 0;

            u32* textureLastVariant = nullptr;
            const u32 textype = (polygon->TexParam >> 26) & 0x7;
            if (enableTextureMaps && textype)
            {
                const u32 texaddr = polygon->TexParam & 0xFFFFu;
                const u32 texwidth = TextureWidth(polygon->TexParam);
                const u32 texheight = TextureHeight(polygon->TexParam);
                int captureBlock = -1;
                if (textype == 7u && (texwidth == 128u || texwidth == 256u))
                {
                    u32 startBlock = (texaddr << 3u) >> 15u;
                    const u32 endBlock =
                        ((texaddr << 3u) + texwidth * texheight * 2u + 0x7FFFu) >> 15u;
                    for (u32 block = startBlock; block < endBlock && block < 16u; ++block)
                    {
                        if (captureInfo[block] != -1)
                            captureBlock = captureInfo[block];
                    }
                }

                if (captureBlock != -1)
                {
                    const u32 bank = static_cast<u32>(captureBlock) >> 2u;
                    const u32 yOffset = texwidth == 128u
                        ? ((texaddr >> 5u) & 0x7Fu)
                        : ((texaddr >> 6u) & 0xFFu);
                    const u32 layerBase = texwidth == 128u
                        ? (static_cast<u32>(captureBlock) & 3u) * 16384u
                        : 0u;
                    const u32 queryAddress = layerBase + yOffset * texwidth;
                    u32 reference = GPU.GetRenderer().GetCaptureTextureReference(bank, queryAddress);
                    if (reference != 0u)
                    {
                        variant.CaptureType = texwidth == 128u ? 1u : 2u;
                        variant.CaptureYOffset = static_cast<s32>(yOffset);
                        variant.CaptureReference =
                            (reference & ~StructuredComposition::kCaptureReferenceAddressMask)
                            | layerBase;
                        prevTexLayer = texwidth == 128u
                            ? static_cast<u32>(captureBlock)
                            : bank;
                        textureLastVariant = &captureLastVariant[captureBlock];
                    }
                }

                if (variant.CaptureType == 0u)
                {
                    Texcache.GetTexture(polygon->TexParam, polygon->TexPalette,
                        variant.Texture, prevTexLayer, textureLastVariant);
                }

                const bool wrapS = (polygon->TexParam >> 16) & 1;
                const bool wrapT = (polygon->TexParam >> 17) & 1;
                const bool mirrorS = (polygon->TexParam >> 18) & 1;
                const bool mirrorT = (polygon->TexParam >> 19) & 1;
                variant.WrapS = wrapS ? (mirrorS ? 2u : 1u) : 0u;
                variant.WrapT = wrapT ? (mirrorT ? 2u : 1u) : 0u;

                if (textureLastVariant && *textureLastVariant < numVariants
                    && variants[*textureLastVariant] == variant)
                {
                    foundVariant = true;
                    prevVariant = *textureLastVariant;
                }
            }

            if (!foundVariant)
            {
                const u32 variantHash = HashVariant(variant);
                u32 indexedVariant = 0;
                const bool indexedFound = VariantLookup.Find(variantHash,
                    [&](u32 index) noexcept {
                        return index < numVariants && variants[index] == variant;
                    }, indexedVariant);
#if defined(MELONPRIME_ENABLE_DEVELOPER_FEATURES)
                if (RasterDifferential::Enabled())
                {
                    bool legacyFound = false;
                    u32 legacyIndex = 0;
                    for (u32 candidate = numVariants; candidate != 0; --candidate)
                    {
                        if (variants[candidate - 1] == variant)
                        {
                            legacyFound = true;
                            legacyIndex = candidate - 1;
                            break;
                        }
                    }
                    if (indexedFound != legacyFound ||
                        (indexedFound && indexedVariant != legacyIndex))
                    {
                        SetRuntimeFailure("variant index disagreed with legacy insertion order");
                    }
                }
#endif
                if (indexedFound)
                {
                    foundVariant = true;
                    prevVariant = indexedVariant;
                }

                if (!foundVariant && numVariants < MaxVariants)
                {
                    prevVariant = numVariants;
                    variants[numVariants] = variant;
                    variants[numVariants].Width = static_cast<u16>(TextureWidth(polygon->TexParam));
                    variants[numVariants].Height = static_cast<u16>(TextureHeight(polygon->TexParam));
                    const bool inserted = VariantLookup.Insert(
                        variantHash, numVariants,
                        [&](u32 index) noexcept { return HashVariant(variants[index]); });
                    assert(inserted);
                    (void)inserted;
                    numVariants++;
                }

                if (textureLastVariant)
                    *textureLastVariant = prevVariant;
            }
        }
        RenderPolygons[i].Variant = prevVariant;
        RenderPolygons[i].TextureLayer = static_cast<float>(prevTexLayer);

        if (polygon->FacingView)
        {
            nextVL = curVL + 1;
            if (nextVL >= nverts) nextVL = 0;
            nextVR = curVR - 1;
            if (static_cast<s32>(nextVR) < 0) nextVR = nverts - 1;
        }
        else
        {
            nextVL = curVL - 1;
            if (static_cast<s32>(nextVL) < 0) nextVL = nverts - 1;
            nextVR = curVR + 1;
            if (nextVR >= nverts) nextVR = 0;
        }

        s32 scaledPositions[10][2];
        s32 ytop = ScreenHeight, ybot = 0;
        for (u32 v = 0; v < polygon->NumVertices; v++)
        {
            if (HiresCoordinates && ScaleFactor > 1)
            {
                scaledPositions[v][0] = (polygon->Vertices[v]->HiresPosition[0] * ScaleFactor) >> 4;
                scaledPositions[v][1] = (polygon->Vertices[v]->HiresPosition[1] * ScaleFactor) >> 4;
            }
            else
            {
                scaledPositions[v][0] = polygon->Vertices[v]->FinalPosition[0] * ScaleFactor;
                scaledPositions[v][1] = polygon->Vertices[v]->FinalPosition[1] * ScaleFactor;
            }
            ytop = std::min(scaledPositions[v][1], ytop);
            ybot = std::max(scaledPositions[v][1], ybot);
        }
        RenderPolygons[i].YTop = ytop;
        RenderPolygons[i].YBot = ybot;
        RenderPolygons[i].XMin = ScreenWidth;
        RenderPolygons[i].XMax = 0;

        if (ybot == ytop)
        {
            vtop = 0; vbot = 0;

            RenderPolygons[i].YBot++;

            u32 j = 1;
            if (scaledPositions[j][0] < scaledPositions[vtop][0]) vtop = j;
            if (scaledPositions[j][0] > scaledPositions[vbot][0]) vbot = j;

            j = nverts - 1;
            if (scaledPositions[j][0] < scaledPositions[vtop][0]) vtop = j;
            if (scaledPositions[j][0] > scaledPositions[vbot][0]) vbot = j;

            if (numYSpans + 2 > MaxYSpanSetups || numSetupIndices >= MaxYSpanIndices)
                break;

            const u32 curSpanL = static_cast<u32>(numYSpans);
            SetupYSpanDummy(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, vtop, 0, scaledPositions);
            const u32 curSpanR = static_cast<u32>(numYSpans);
            SetupYSpanDummy(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, vbot, 1, scaledPositions);

            YSpanIndices[numSetupIndices].PolyIdx = static_cast<u16>(i);
            YSpanIndices[numSetupIndices].SpanIdxL = static_cast<u16>(curSpanL);
            YSpanIndices[numSetupIndices].SpanIdxR = static_cast<u16>(curSpanR);
            YSpanIndices[numSetupIndices].Y = static_cast<u16>(ytop);
            numSetupIndices++;
        }
        else
        {
            if (numYSpans + 2 > MaxYSpanSetups)
                break;

            u32 curSpanL = static_cast<u32>(numYSpans);
            SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVL, nextVL, 0, scaledPositions);
            u32 curSpanR = static_cast<u32>(numYSpans);
            SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVR, nextVR, 1, scaledPositions);

            for (s32 y = ytop; y < ybot; y++)
            {
                if (y >= scaledPositions[nextVL][1] && curVL != polygon->VBottom)
                {
                    while (y >= scaledPositions[nextVL][1] && curVL != polygon->VBottom)
                    {
                        curVL = nextVL;
                        if (polygon->FacingView)
                        {
                            nextVL = curVL + 1;
                            if (nextVL >= nverts)
                                nextVL = 0;
                        }
                        else
                        {
                            nextVL = curVL - 1;
                            if (static_cast<s32>(nextVL) < 0)
                                nextVL = nverts - 1;
                        }
                    }

                    if (numYSpans >= MaxYSpanSetups)
                        break;
                    curSpanL = static_cast<u32>(numYSpans);
                    SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVL, nextVL, 0, scaledPositions);
                }
                if (y >= scaledPositions[nextVR][1] && curVR != polygon->VBottom)
                {
                    while (y >= scaledPositions[nextVR][1] && curVR != polygon->VBottom)
                    {
                        curVR = nextVR;
                        if (polygon->FacingView)
                        {
                            nextVR = curVR - 1;
                            if (static_cast<s32>(nextVR) < 0)
                                nextVR = nverts - 1;
                        }
                        else
                        {
                            nextVR = curVR + 1;
                            if (nextVR >= nverts)
                                nextVR = 0;
                        }
                    }

                    if (numYSpans >= MaxYSpanSetups)
                        break;
                    curSpanR = static_cast<u32>(numYSpans);
                    SetupYSpan(&RenderPolygons[i], &YSpanSetups[numYSpans++], polygon, curVR, nextVR, 1, scaledPositions);
                }

                if (numSetupIndices >= MaxYSpanIndices)
                    break;

                YSpanIndices[numSetupIndices].PolyIdx = static_cast<u16>(i);
                YSpanIndices[numSetupIndices].SpanIdxL = static_cast<u16>(curSpanL);
                YSpanIndices[numSetupIndices].SpanIdxR = static_cast<u16>(curSpanR);
                YSpanIndices[numSetupIndices].Y = static_cast<u16>(y);
                numSetupIndices++;
            }
        }

        // Counts are committed only after the complete polygon is built. The
        // arrays cover the valid DS worst case; the guards above remain a
        // malformed-input defence and never expose partial GPU records.
        numPolygons = i + 1;
        previousPolygon = polygon;
    }

    return numVariants;
}

u32 VulkanRenderer3D::BuildPolygonBatches(u32 numPolygons)
{
    if (numPolygons == 0)
    {
        PolygonBatches[0] = { 0, 0 };
        return 1;
    }

    // A polygon can only be binned into tiles intersecting its completed
    // screen-space bounds. Summing those rectangles is conservative (the
    // precise convex test can only remove tiles), so the resulting batch is
    // mathematically guaranteed to fit the fixed GPU working set.
    const u64 capacity = static_cast<u64>(MaxWorkTiles);
    u32 first = 0;
    u32 count = 0;
    u32 batchCount = 0;
    u64 batchTiles = 0;

    for (u32 i = 0; i < numPolygons; ++i)
    {
        const RenderPolygon& polygon = RenderPolygons[i];
        const s32 minX = std::clamp(polygon.XMin, 0, ScreenWidth - 1);
        const s32 maxX = std::clamp(polygon.XMax, 0, ScreenWidth - 1);
        const s32 minY = std::clamp(polygon.YTop, 0, ScreenHeight - 1);
        const s32 maxY = static_cast<s32>(std::clamp<s64>(
            static_cast<s64>(polygon.YBot) - 1, 0, ScreenHeight - 1));

        u64 polygonTiles = 0;
        if (minX <= maxX && minY <= maxY)
        {
            const u64 tileColumns = static_cast<u64>(maxX / TileSize - minX / TileSize + 1);
            const u64 tileRows = static_cast<u64>(maxY / TileSize - minY / TileSize + 1);
            polygonTiles = tileColumns * tileRows;
        }

        if (count != 0 && batchTiles + polygonTiles > capacity)
        {
            PolygonBatches[batchCount++] = { first, count };
            first = i;
            count = 0;
            batchTiles = 0;
        }

        // MaxWorkTiles is at least one complete screen of tiles, so a single
        // polygon always fits even when it covers the entire display.
        assert(polygonTiles <= capacity);
        batchTiles += polygonTiles;
        ++count;
    }

    if (count != 0)
        PolygonBatches[batchCount++] = { first, count };
    return batchCount;
}


// ---------------------------------------------------------------------------
// Per-frame helpers
// ---------------------------------------------------------------------------

void VulkanRenderer3D::BufferBarrier(
    VkCommandBuffer cmd,
    const VkBuffer* buffers, u32 count,
    VkPipelineStageFlags srcStage, VkAccessFlags srcAccess,
    VkPipelineStageFlags dstStage, VkAccessFlags dstAccess) const
{
    if (count == 0)
        return;

    VkBufferMemoryBarrier barriers[8]{};
    if (count > 8) count = 8;

    for (u32 i = 0; i < count; i++)
    {
        barriers[i].sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        barriers[i].srcAccessMask = srcAccess;
        barriers[i].dstAccessMask = dstAccess;
        // Single queue family throughout: the device was created with one
        // universal queue wherever possible, so no ownership transfer is
        // needed and both indices stay IGNORED.
        barriers[i].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barriers[i].buffer = buffers[i];
        barriers[i].offset = 0;
        barriers[i].size = VK_WHOLE_SIZE;
    }

    Device.Fns().CmdPipelineBarrier(
        cmd, srcStage, dstStage, 0, 0, nullptr, count, barriers, 0, nullptr);
}

void VulkanRenderer3D::RecordInitialTransitions(VkCommandBuffer cmd)
{
    const Vk::DeviceDispatch& fns = Device.Fns();

    if (NeedsFinalFBTransition)
    {
        // FinalFB lives in GENERAL for its whole lifetime. It alternates
        // between being written as a storage image (which requires GENERAL)
        // and being read as a transfer source (which GENERAL also permits), so
        // re-transitioning it every frame would buy nothing. The image was just
        // created, so UNDEFINED is its real old layout and there is no prior
        // access to make available.
        FinalFB.RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);
        NeedsFinalFBTransition = false;
    }

    if (PlaceholdersInitialized)
        return;

    // The two placeholder arrays are never sampled in a path that matters, but
    // they are statically reachable descriptors, so they get defined contents
    // and the layout their descriptor advertises. Done once for the renderer's
    // lifetime: these images are not resolution-dependent, so re-running this
    // would need a source stage that describes the previous frame's reads
    // rather than TOP_OF_PIPE.
    const VkClearColorValue zero{};
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    Vk::Image* placeholders[2] = { &DummyTextureImage, &DummyCaptureImage };
    for (Vk::Image* image : placeholders)
    {
        image->RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        fns.CmdClearColorImage(cmd, image->GetHandle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        image->RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // The clear-bitmap images are only written when DISP3DCNT bit 14 is set,
    // but DepthBlend's descriptors reference them unconditionally, so they are
    // cleared and moved into the sampled layout once here rather than being
    // left UNDEFINED until the first bitmap-clear frame.
    for (Vk::Image& image : ClearBitmapImage)
    {
        image.RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
        fns.CmdClearColorImage(cmd, image.GetHandle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &zero, 1, &range);
        image.RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    PlaceholdersInitialized = true;
}

void VulkanRenderer3D::UpdateClearBitmap(VkCommandBuffer cmd, Vk::StagingRing& staging)
{
    if (!(GPU3D.RenderDispCnt & (1 << 14)))
        return;

    const Vk::DeviceDispatch& fns = Device.Fns();

    for (int slot = 0; slot < 2; slot++)
    {
        if (!(ClearBitmapDirty & (1 << slot)))
            continue;

        if (slot == 0)
        {
            const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x40000]);
            for (int i = 0; i < 256 * 256; i++)
            {
                const u16 color = vram[i];
                u32 r = (color << 1) & 0x3E; if (r) r++;
                u32 g = (color >> 4) & 0x3E; if (g) g++;
                u32 b = (color >> 9) & 0x3E; if (b) b++;
                const u32 a = (color & 0x8000) ? 31 : 0;

                ClearBitmap[0][i] = r | (g << 8) | (b << 16) | (a << 24);
            }
        }
        else
        {
            const u16* vram = reinterpret_cast<const u16*>(&GPU.VRAMFlat_Texture[0x60000]);
            for (int i = 0; i < 256 * 256; i++)
            {
                const u16 val = vram[i];
                const u32 depth = ((val & 0x7FFF) * 0x200) + 0x1FF;
                const u32 fog = static_cast<u32>(val & 0x8000) << 9;

                ClearBitmap[1][i] = depth | fog;
            }
        }

        VkDeviceSize offset = 0;
        const VkDeviceSize alignment =
            std::max<VkDeviceSize>(4, Device.GetLimits().optimalBufferCopyOffsetAlignment);
        if (!staging.Upload(ClearBitmap[slot].get(), ClearBitmapBytes, alignment, offset))
        {
            Platform::Log(Platform::LogLevel::Warn,
                "[Vulkan] the staging ring could not hold the clear bitmap this frame\n");
            continue;
        }

        // Back to TRANSFER_DST: the previous frame's DepthBlend sampled it, so
        // the dependency is compute-read -> transfer-write (a WAR hazard, which
        // needs only execution ordering plus the destination access mask).
        ClearBitmapImage[slot].RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

        VkBufferImageCopy copy{};
        copy.bufferOffset = offset;
        copy.bufferRowLength = 0;
        copy.bufferImageHeight = 0;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageOffset = { 0, 0, 0 };
        copy.imageExtent = { ClearBitmapDimension, ClearBitmapDimension, 1 };

        fns.CmdCopyBufferToImage(cmd, staging.GetHandle(), ClearBitmapImage[slot].GetHandle(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

        ClearBitmapImage[slot].RecordLayoutTransition(cmd,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    ClearBitmapDirty = 0;
}

bool VulkanRenderer3D::WriteRasterizerDescriptorSet(
    u32 frameIndex, u32 slot, VkBuffer presentationOutput, VkBuffer structuredInput)
{
    VulkanPerf::ScopedCpuTimer descriptorTimer(VulkanPerf::CpuMetric::DescriptorUpdate);
    VkDescriptorSet set = Descriptors.GetRasterizerSet(frameIndex, slot);
    if (set == VK_NULL_HANDLE || presentationOutput == VK_NULL_HANDLE
        || structuredInput == VK_NULL_HANDLE)
        return false;

    Vk::DescriptorWriter writer;
    writer.Reset();

    const bool ok =
        writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::MetaUniform),
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MetaUniformBuffer.GetHandle(),
            MetaUniformStride * frameIndex, sizeof(MetaUniform))
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::PolygonBuffer),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PolygonBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::XSpanSetups),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, XSpanSetupBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::YSpanSetups),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, YSpanSetupBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::ColorTiles),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TileBuffers[0].GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::DepthTiles),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TileBuffers[1].GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::AttrTiles),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, TileBuffers[2].GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::ResultBuffer),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResultBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::BinResultBuffer),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BinResultBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::WorkDescBuffer),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, WorkDescBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteTexelBuffer(set, static_cast<u32>(Vk::RasterizerBinding::SetupIndices),
            SetupIndicesBuffer.GetView())
        && writer.WriteImage(set, static_cast<u32>(Vk::RasterizerBinding::FinalFB),
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, FinalFB.GetView(),
            VK_IMAGE_LAYOUT_GENERAL)
        // Every binding in the layout is written even when the pipeline about
        // to run does not use it (Resolve never touches StructuredInput). A
        // descriptor a pipeline does not statically reference may legally be
        // left unwritten, but "legally" depends on the shader's final SPIR-V
        // rather than on this file, so all seventeen are always valid.
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::StructuredInput),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, structuredInput, 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::PresentationOut),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, presentationOutput, 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::CaptureSidecar),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, CaptureSidecarBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::BlendState),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BlendStateBuffer.GetHandle(), 0, VK_WHOLE_SIZE)
        && writer.WriteBuffer(set, static_cast<u32>(Vk::RasterizerBinding::ResultWinner),
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, ResultWinnerBuffer.GetHandle(), 0, VK_WHOLE_SIZE);

    if (!ok)
        return false;

    writer.Flush(Device.Fns(), Device.GetHandle());
    VulkanPerf::AddCounter(VulkanPerf::Counter::DescriptorWriteCount,
        static_cast<u64>(Vk::RasterizerBinding::Count));
    return true;
}

VkDescriptorSet VulkanRenderer3D::AcquireTextureSet(
    u32 frameIndex, VkImageView textureView, VkSampler sampler)
{
    if (textureView == BoundTextureView && sampler == BoundSampler
        && BoundTextureSet != VK_NULL_HANDLE)
    {
        return BoundTextureSet;
    }

    constexpr u32 cacheMask = TextureSetCacheCapacity - 1;
    const std::size_t viewHash = std::hash<VkImageView>{}(textureView);
    const std::size_t samplerHash = std::hash<VkSampler>{}(sampler);
    u32 cacheIndex = static_cast<u32>(
        (viewHash ^ (samplerHash + 0x9E3779B9u + (viewHash << 6u) + (viewHash >> 2u)))
        & cacheMask);
    TextureSetCacheEntry* insertion = nullptr;
    for (u32 probe = 0; probe < TextureSetCacheCapacity; ++probe)
    {
        TextureSetCacheEntry& entry = TextureSetCache[cacheIndex];
        if (entry.Epoch != TextureSetCacheEpoch)
        {
            insertion = &entry;
            break;
        }
        if (entry.View == textureView && entry.Sampler == sampler)
        {
            BoundTextureView = textureView;
            BoundSampler = sampler;
            BoundTextureSet = entry.Set;
            return entry.Set;
        }
        cacheIndex = (cacheIndex + 1u) & cacheMask;
    }

    if (TextureSetCursor >= Descriptors.GetSizing().TextureSetsPerFrame
        || !insertion)
        return VK_NULL_HANDLE;

    VkDescriptorSet set = Descriptors.GetTextureSet(frameIndex, TextureSetCursor);
    if (set == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    TextureSetCursor++;

    Vk::DescriptorWriter writer;
    writer.Reset();
    VulkanPerf::ScopedCpuTimer descriptorTimer(VulkanPerf::CpuMetric::DescriptorUpdate);

    const VkSampler repeatSampler = Samplers.GetRepeat();
    const bool ok =
        writer.WriteImage(set, static_cast<u32>(Vk::TextureBinding::CurrentTexture),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sampler, textureView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        && writer.WriteImage(set, static_cast<u32>(Vk::TextureBinding::Capture128Texture),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, repeatSampler, DummyCaptureImage.GetView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        && writer.WriteImage(set, static_cast<u32>(Vk::TextureBinding::Capture256Texture),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, repeatSampler, DummyCaptureImage.GetView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        && writer.WriteImage(set, static_cast<u32>(Vk::TextureBinding::ClearBitmapColor),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, repeatSampler, ClearBitmapImage[0].GetView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
        && writer.WriteImage(set, static_cast<u32>(Vk::TextureBinding::ClearBitmapDepth),
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, repeatSampler, ClearBitmapImage[1].GetView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    if (!ok)
        return VK_NULL_HANDLE;

    writer.Flush(Device.Fns(), Device.GetHandle());
    VulkanPerf::AddCounter(VulkanPerf::Counter::DescriptorWriteCount,
        static_cast<u64>(Vk::TextureBinding::Count));

    BoundTextureView = textureView;
    BoundSampler = sampler;
    BoundTextureSet = set;
    *insertion = { textureView, sampler, set, TextureSetCacheEpoch };
    return set;
}

void VulkanRenderer3D::FillMetaUniform(MetaUniform& meta, u32 numVariants, u32 numPolygons) const
{
    meta = MetaUniform{};

    meta.DispCnt = GPU3D.RenderDispCnt;
    meta.NumPolygons = numPolygons;
    meta.NumVariants = numVariants;
    meta.AlphaRef = GPU3D.RenderAlphaRef;

    {
        u32 r = (GPU3D.RenderClearAttr1 << 1) & 0x3E; if (r) r++;
        u32 g = (GPU3D.RenderClearAttr1 >> 4) & 0x3E; if (g) g++;
        u32 b = (GPU3D.RenderClearAttr1 >> 9) & 0x3E; if (b) b++;
        const u32 a = (GPU3D.RenderClearAttr1 >> 16) & 0x1F;

        meta.ClearColor = r | (g << 8) | (b << 16) | (a << 24);
        meta.ClearDepth = ((GPU3D.RenderClearAttr2 & 0x7FFF) * 0x200) + 0x1FF;
        meta.ClearAttr = GPU3D.RenderClearAttr1 & 0x3F008000;

        const u8 xoff = (GPU3D.RenderClearAttr2 >> 16) & 0xFF;
        const u8 yoff = (GPU3D.RenderClearAttr2 >> 24) & 0xFF;
        meta.ClearBitmapOffset[0] = static_cast<float>(xoff) / 256.0f;
        meta.ClearBitmapOffset[1] = static_cast<float>(yoff) / 256.0f;
    }

    for (u32 i = 0; i < 32; i++)
    {
        const u32 color = GPU3D.RenderToonTable[i];
        u32 r = (color << 1) & 0x3E; if (r) r++;
        u32 g = (color >> 4) & 0x3E; if (g) g++;
        u32 b = (color >> 9) & 0x3E; if (b) b++;

        meta.ToonTable[i * 4 + 0] = r | (g << 8) | (b << 16);
    }
    for (u32 i = 0; i < 34; i++)
        meta.ToonTable[i * 4 + 1] = GPU3D.RenderFogDensityTable[i];
    for (u32 i = 0; i < 8; i++)
    {
        const u32 color = GPU3D.RenderEdgeTable[i];
        u32 r = (color << 1) & 0x3E; if (r) r++;
        u32 g = (color >> 4) & 0x3E; if (g) g++;
        u32 b = (color >> 9) & 0x3E; if (b) b++;

        meta.ToonTable[i * 4 + 2] = r | (g << 8) | (b << 16);
    }

    meta.FogOffset = GPU3D.RenderFogOffset;
    meta.FogShift = GPU3D.RenderFogShift;
    {
        u32 fogR = (GPU3D.RenderFogColor << 1) & 0x3E; if (fogR) fogR++;
        u32 fogG = (GPU3D.RenderFogColor >> 4) & 0x3E; if (fogG) fogG++;
        u32 fogB = (GPU3D.RenderFogColor >> 9) & 0x3E; if (fogB) fogB++;
        const u32 fogA = (GPU3D.RenderFogColor >> 16) & 0x1F;
        meta.FogColor = fogR | (fogG << 8) | (fogB << 16) | (fogA << 24);
    }
}


// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

void VulkanRenderer3D::RenderFrame()
{
    if (RuntimeFailed || !Initialized)
        return;
    if (ScaleFactor <= 0 || ShaderStepIdx < ShaderStepCount)
        return;     // pipelines are still being compiled
    if (!FinalFB.IsValid() || !BinResultBuffer.IsValid() || !ResultBuffer.IsValid()
        || !ResultWinnerBuffer.IsValid())
    {
        SetRuntimeFailure("required frame resources are unavailable");
        return;
    }

    const Vk::DeviceDispatch& fns = Device.Fns();
    VulkanPerf::SetScale(static_cast<u32>(ScaleFactor));

    Vk::FrameContext* frame = nullptr;
    {
        VulkanPerf::ScopedCpuTimer waitTimer(VulkanPerf::CpuMetric::RasterBeginWait);
        frame = Frames.BeginFrame();
    }
    if (!frame)
    {
        SetRuntimeFailure("could not begin a frame command buffer");
        return;
    }

    VkCommandBuffer cmd = frame->CommandBuffer;
    const u32 frameIndex = Frames.GetFrameIndex();

    // BeginFrame() waited on this slot's fence, so last frame's staging space
    // and descriptor sets are free again.
    FrameStaging.Reset();
    TextureSetCursor = 0;
    TextureSetCacheEpoch++;
    if (TextureSetCacheEpoch == 0)
    {
        for (TextureSetCacheEntry& entry : TextureSetCache)
            entry.Epoch = 0;
        TextureSetCacheEpoch = 1;
    }
    BoundTextureView = VK_NULL_HANDLE;
    BoundSampler = VK_NULL_HANDLE;
    BoundTextureSet = VK_NULL_HANDLE;
    TextureHeap.BeginFrame(cmd, &FrameStaging);

    if (NeedsFinalFBTransition || !PlaceholdersInitialized)
        RecordInitialTransitions(cmd);

    u8 texcacheClearBitmapDirty = 0;
    bool textureCacheChanged = false;
    {
        VulkanPerf::ScopedCpuTimer texcacheTimer(VulkanPerf::CpuMetric::TexcacheUpdate);
        textureCacheChanged = Texcache.Update(texcacheClearBitmapDirty);
    }
    ClearBitmapDirty |= texcacheClearBitmapDirty;

    const bool canReuseIdenticalFrame =
        !textureCacheChanged
        && GPU3D.RenderFrameIdentical
        && FinalFBHasContent
        && !NeedsFinalFBTransition
        && PlaceholdersInitialized;
    if (canReuseIdenticalFrame)
    {
        // Keep the frame-ring fence progression intact while skipping every
        // 3D upload/dispatch. The compositor still runs at VBlank with the
        // current structured 2D planes and samples the unchanged FinalFB.
        PendingFence = frame->InFlightFence;
        bool identicalSubmitted = false;
        {
            VulkanPerf::ScopedCpuTimer submitTimer(VulkanPerf::CpuMetric::QueueSubmit);
            identicalSubmitted = Frames.SubmitFrame(Device.GetMainQueue());
        }
        if (identicalSubmitted)
        {
            FrameInFlight = true;
            return;
        }
        PendingFence = VK_NULL_HANDLE;
        SetRuntimeFailure("identical-frame submission failed");
        return;
    }

    // ComposeStructuredOutput may have populated retained capture samples in
    // the previous queue submission. Make those writes visible before a
    // capture-derived direct-color texture reads the same persistent buffer.
    const VkBuffer captureSidecar = CaptureSidecarBuffer.GetHandle();
    BufferBarrier(cmd, &captureSidecar, 1,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    UpdateClearBitmap(cmd, FrameStaging);

    // Polygon/span setup runs on the CPU exactly like the OpenGL compute
    // renderer; the texcache uploads it triggers are recorded into this same
    // command buffer, which is why it happens while the buffer is open.
    int numYSpans = 0;
    int numSetupIndices = 0;
    u32 numPolygons = 0;
    u32 numVariants = 0;
    {
        VulkanPerf::ScopedCpuTimer polygonTimer(VulkanPerf::CpuMetric::BuildPolygons);
        numVariants = BuildPolygons(numYSpans, numSetupIndices, numPolygons);
    }
    VulkanPerf::RecordGeometry(
        numPolygons, numVariants, static_cast<u32>(std::max(numYSpans, 0)),
        static_cast<u32>(std::max(numSetupIndices, 0)));
    TextureHeap.FlushUploadBarriers();

    // The three counts move together: a span record is only useful with an
    // index that points at it and a polygon that owns it. If the budget ran out
    // before any complete triple was produced, the frame degenerates to the
    // clear + DepthBlend + FinalPass path.
    if (numSetupIndices <= 0 || numPolygons == 0)
    {
        numYSpans = 0;
        numSetupIndices = 0;
        // MetaUniform.NumPolygons has to agree: nothing was uploaded into
        // PolygonBuffer, so no stage may believe there are polygons to read.
        numPolygons = 0;
    }

    if (numYSpans > 0)
    {
        VkDeviceSize spanOffset = 0, indexOffset = 0, polygonOffset = 0;
        const VkDeviceSize spanBytes = sizeof(SpanSetupY) * static_cast<VkDeviceSize>(numYSpans);
        const VkDeviceSize indexBytes = sizeof(SetupIndices) * static_cast<VkDeviceSize>(numSetupIndices);
        const VkDeviceSize polygonBytes = sizeof(RenderPolygon) * static_cast<VkDeviceSize>(numPolygons);

        const bool staged =
            FrameStaging.Upload(YSpanSetups.get(), spanBytes, 16, spanOffset)
            && FrameStaging.Upload(YSpanIndices.data(), indexBytes, 16, indexOffset)
            && FrameStaging.Upload(RenderPolygons.get(), polygonBytes, 16, polygonOffset);

        if (!staged)
        {
            Frames.SubmitFrame(Device.GetMainQueue());
            SetRuntimeFailure("the staging ring could not hold this frame's span data");
            return;
        }

        VkBufferCopy copy{};

        copy = { spanOffset, 0, spanBytes };
        fns.CmdCopyBuffer(cmd, FrameStaging.GetHandle(), YSpanSetupBuffer.GetHandle(), 1, &copy);
        copy = { indexOffset, 0, indexBytes };
        fns.CmdCopyBuffer(cmd, FrameStaging.GetHandle(), SetupIndicesBuffer.GetHandle(), 1, &copy);
        copy = { polygonOffset, 0, polygonBytes };
        fns.CmdCopyBuffer(cmd, FrameStaging.GetHandle(), PolygonBuffer.GetHandle(), 1, &copy);

        // Transfer writes -> compute reads. This is the only dependency the
        // three uploads have; nothing reads them before the first dispatch.
        const VkBuffer uploaded[3] = {
            YSpanSetupBuffer.GetHandle(),
            SetupIndicesBuffer.GetHandle(),
            PolygonBuffer.GetHandle(),
        };
        BufferBarrier(cmd, uploaded, 3,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // MetaUniform: written straight into the persistently mapped host-visible
    // slice for this frame slot. No barrier is required -- vkQueueSubmit
    // performs an implicit host-write availability operation for memory written
    // before the submission, and Buffer::WriteMapped() flushes non-coherent
    // allocations for us.
    {
        MetaUniform meta;
        FillMetaUniform(meta, numVariants, numPolygons);
        if (!MetaUniformBuffer.WriteMapped(MetaUniformStride * frameIndex, &meta, sizeof(meta)))
        {
            Frames.SubmitFrame(Device.GetMainQueue());
            SetRuntimeFailure("could not update the frame uniform block");
            return;
        }
    }

    if (!ComposedOutput || !WriteRasterizerDescriptorSet(
            frameIndex, RasterizerSetSlot, NativeResolveBuffer.GetHandle(),
            ComposedOutput->Slots[0].StructuredInput.GetHandle()))
    {
        Frames.SubmitFrame(Device.GetMainQueue());
        SetRuntimeFailure("could not write the rasterizer descriptor set");
        return;
    }

    VkDescriptorSet rasterizerSet = Descriptors.GetRasterizerSet(frameIndex, RasterizerSetSlot);
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Layouts.GetPipelineLayout(),
        Vk::RasterizerSetIndex, 1, &rasterizerSet, 0, nullptr);

    // Base texture set: the untextured binding, which is also what DepthBlend
    // needs (it only reads the clear-bitmap samplers out of set 1).
    VkDescriptorSet baseTextureSet =
        AcquireTextureSet(frameIndex, DummyTextureImage.GetView(), Samplers.Get(0, 0));
    if (baseTextureSet == VK_NULL_HANDLE)
    {
        Frames.SubmitFrame(Device.GetMainQueue());
        SetRuntimeFailure("could not write the base texture descriptor set");
        return;
    }
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Layouts.GetPipelineLayout(),
        Vk::TextureSetIndex, 1, &baseTextureSet, 0, nullptr);
    VkDescriptorSet currentTextureSet = baseTextureSet;

    Vk::RasterizerPushConstants push{};
    push.TexWidth = 8;
    push.TexHeight = 8;
    fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);

    const VkBuffer binResult = BinResultBuffer.GetHandle();
    const VkBuffer workDesc = WorkDescBuffer.GetHandle();
    const VkBuffer xSpans = XSpanSetupBuffer.GetHandle();
    const VkBuffer blendState = BlendStateBuffer.GetHandle();

    const u32 polygonBatchCount = BuildPolygonBatches(numPolygons);

    // Descriptor sets are immutable for the frame. Allocate each variant once
    // and reuse it in every work-tile batch; otherwise a pathological frame
    // would turn bounded raster memory into unbounded descriptor consumption.
    for (u32 i = 0; i < numVariants; ++i)
    {
        const Variant& variant = Variants[i];
        const VulkanTextureHeap::Entry* texture = TextureHeap.Lookup(variant.Texture);
        VkImageView view = texture ? texture->View : DummyTextureImage.GetView();
        VariantTextureSets[i] = AcquireTextureSet(
            frameIndex, view, Samplers.Get(variant.WrapS, variant.WrapT));
        if (VariantTextureSets[i] == VK_NULL_HANDLE)
        {
            Frames.SubmitFrame(Device.GetMainQueue());
            SetRuntimeFailure("ran out of per-frame texture descriptor sets");
            return;
        }
    }

    const bool wbuffer = numYSpans > 0 && GPU3D.RenderPolygonRAM[0]->WBuffer;

    for (u32 batchIndex = 0; batchIndex < polygonBatchCount; ++batchIndex)
    {
        const PolygonBatch& batch = PolygonBatches[batchIndex];

        // Each batch reuses the bounded bin/tile working set. The result and
        // shadow continuation buffers carry the exact pixel state forward.
        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[VulkanShaders::Pipeline_ClearCoarseBinMask]);
        fns.CmdDispatch(cmd,
            static_cast<u32>(TilesPerLine * TileLines / ClearCoarseBinMaskLocalSize), 1, 1);
        BufferBarrier(cmd, &binResult, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        if (batch.PolygonCount > 0)
        {
        // 2. reset the per-variant indirect work counts.
        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[VulkanShaders::Pipeline_ClearIndirectWorkCount]);
        fns.CmdDispatch(cmd, DivRoundUp(numVariants, 32), 1, 1);
        BufferBarrier(cmd, &binResult, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        // X spans are frame-global and are generated only before the first
        // polygon batch.
        if (batchIndex == 0)
        {
            fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                Pipelines[wbuffer ? VulkanShaders::Pipeline_InterpSpansW : VulkanShaders::Pipeline_InterpSpansZ]);
            const u32 setupIndexCount = static_cast<u32>(numSetupIndices);
            // Some portability implementations (notably MoltenVK) advertise
            // an X group count whose product with the shader's 32 lanes does
            // not fit in u32. It wrapped to zero on the affected Mac, so
            // `base` never advanced and ROM startup recorded push constants
            // forever. Widen first, then cap to this frame's actual work so
            // the loop always progresses.
            const u32 maxPerDispatch = static_cast<u32>(std::min<u64>(
                setupIndexCount,
                static_cast<u64>(Device.GetLimits().maxComputeWorkGroupCount[0]) * 32ull));
            for (u32 base = 0; base < setupIndexCount;)
            {
                const u32 count = std::min(setupIndexCount - base, maxPerDispatch);
                Vk::RasterizerPushConstants spanPush{};
                spanPush.TexIsCapture = base;
                spanPush.CaptureYOffset = static_cast<s32>(count);
                fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                    0, Vk::PushConstantSize, &spanPush);
                fns.CmdDispatch(cmd, DivRoundUp(count, 32), 1, 1);
                base += count;
            }
            BufferBarrier(cmd, &xSpans, 1,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        }

        // 4. bin polygons into coarse and fine tiles.
        Vk::RasterizerPushConstants batchPush{};
        batchPush.CurVariant = batch.FirstPolygon;
        batchPush.TexWidth = batch.PolygonCount;
        fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, Vk::PushConstantSize, &batchPush);
        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[VulkanShaders::Pipeline_BinCombined]);
        fns.CmdDispatch(cmd,
            DivRoundUp(batch.PolygonCount, 32),
            static_cast<u32>(ScreenWidth / CoarseTileW),
            static_cast<u32>(ScreenHeight / CoarseTileH));
        {
            const VkBuffer binned[2] = { binResult, workDesc };
            BufferBarrier(cmd, binned, 2,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }

        // 5. turn the per-variant counts into dispatch arguments and offsets.
        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[VulkanShaders::Pipeline_CalculateWorkOffsets]);
        fns.CmdDispatch(cmd, DivRoundUp(numVariants, 32), 1, 1);

        // The same buffer is now both a storage buffer and the source of every
        // VkDispatchIndirectCommand below, so the dependency names both
        // consumers: compute reads/writes and the indirect-command fetch, which
        // happens in the DRAW_INDIRECT stage.
        BufferBarrier(cmd, &binResult, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT
                | VK_ACCESS_INDIRECT_COMMAND_READ_BIT);

        // 6. sort the work list by variant.
        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[VulkanShaders::Pipeline_SortWork]);
        fns.CmdDispatchIndirect(cmd, binResult, offsetof(BinResultHeader, SortWorkWorkCount));
        BufferBarrier(cmd, &workDesc, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

        // 7. rasterise, one indirect dispatch per variant.
        //
        // No barrier inside the loop: every work item owns its own slice of the
        // tile buffers (tileOffset is derived from the sorted work index), so
        // the dispatches are independent, exactly as in the OpenGL renderer.
        {
            const bool highlightMode = (GPU3D.RenderDispCnt & (1 << 1)) != 0;
            VkPipeline prevPipeline = VK_NULL_HANDLE;

            for (u32 i = 0; i < numVariants; i++)
            {
                const Variant& variant = Variants[i];
                // Retained display captures bypass Texcache, but they are
                // still sampled by the textured raster pipeline.
                const bool hasTexture = variant.Texture != 0 || variant.CaptureType != 0;
                const int blendMode = std::min<int>(variant.BlendMode, 4);

                int kind;
                if (blendMode == 2)
                {
                    if (hasTexture)
                        kind = highlightMode ? RasteriseKind_UseTextureHighlight : RasteriseKind_UseTextureToon;
                    else
                        kind = highlightMode ? RasteriseKind_NoTextureHighlight : RasteriseKind_NoTextureToon;
                }
                else
                {
                    kind = hasTexture ? UseTextureKinds[blendMode] : NoTextureKinds[blendMode];
                }

                VkPipeline pipeline = Pipelines[
                    VulkanShaders::Pipeline_RasteriseNoTextureZ + kind * 2 + (wbuffer ? 1 : 0)];
                if (pipeline == VK_NULL_HANDLE)
                    continue;

                if (pipeline != prevPipeline)
                {
                    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
                    prevPipeline = pipeline;
                }

                VkDescriptorSet textureSet = VariantTextureSets[i];
                if (textureSet != currentTextureSet)
                {
                    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                        Layouts.GetPipelineLayout(), Vk::TextureSetIndex, 1, &textureSet, 0, nullptr);
                    currentTextureSet = textureSet;
                }

                Vk::RasterizerPushConstants variantPush{};
                variantPush.CurVariant = i;
                // The shader divides by these, and only inside the UseTexture
                // branch; a variant without a texture still gets a non-zero
                // pair so the push-constant block never carries a divisor of 0.
                variantPush.TexWidth = variant.Width ? variant.Width : 8;
                variantPush.TexHeight = variant.Height ? variant.Height : 8;
                variantPush.TexWrapS = variant.WrapS;
                variantPush.TexWrapT = variant.WrapT;
                variantPush.TexIsCapture = variant.CaptureType;
                variantPush.CaptureYOffset = variant.CaptureYOffset;
                variantPush.Padding = variant.CaptureReference;
                fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                    0, Vk::PushConstantSize, &variantPush);

                // MelonPrimeDS #2047: CalculateWorkOffsets wrote
                // (1, ceil(n/32768), min(n, 32768)) here instead of the raw
                // count, because NVIDIA caps the Y and Z group dimensions at
                // 65535. The shader rebuilds the linear index from Y*chunk + Z
                // and drops the overdispatched tail against
                // VariantWorkRealCount.
                fns.CmdDispatchIndirect(cmd, binResult,
                    offsetof(BinResultHeader, VariantWorkCount) + i * 16);
            }

        }

        const VkBuffer tiles[3] = {
            TileBuffers[0].GetHandle(), TileBuffers[1].GetHandle(), TileBuffers[2].GetHandle(),
        };
        BufferBarrier(cmd, tiles, 3,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

        // 8. depth test / blend the binned tiles into the result buffer.
    //
    // DepthBlend reads set 1's clear-bitmap samplers, so the base set is
    // rebound in case the rasterise loop left a texture bound. (Every texture
    // set carries the same clear-bitmap descriptors, so this is about keeping
    // the bound state predictable rather than about correctness.)
        if (currentTextureSet != baseTextureSet)
        {
            fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Layouts.GetPipelineLayout(),
                Vk::TextureSetIndex, 1, &baseTextureSet, 0, nullptr);
            currentTextureSet = baseTextureSet;
        }
        Vk::RasterizerPushConstants blendPush{};
        blendPush.CurVariant = batch.FirstPolygon;
        blendPush.TexHeight = batchIndex != 0 ? 1u : 0u;
        fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, Vk::PushConstantSize, &blendPush);

        fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
            Pipelines[wbuffer ? VulkanShaders::Pipeline_DepthBlendW : VulkanShaders::Pipeline_DepthBlendZ]);
        fns.CmdDispatch(cmd,
            static_cast<u32>(ScreenWidth / TileSize),
            static_cast<u32>(ScreenHeight / TileSize), 1);

        const VkBuffer continued[4] = {
            ResultBuffer.GetHandle(), blendState,
            TileBuffers[2].GetHandle(), ResultWinnerBuffer.GetHandle(),
        };
        BufferBarrier(cmd, continued, 4,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);

        // Software-exact coverage is defined on the native DS raster grid.
        // High-resolution targets retain the separate scaled-raster contract,
        // matching the Metal compute backend.
        if (ScaleFactor == 1
            && (GPU3D.RenderDispCnt & (1u << 4)) != 0u
            && numSetupIndices > 0)
        {
            Vk::RasterizerPushConstants coveragePush{};
            coveragePush.CurVariant = batch.FirstPolygon;
            coveragePush.TexWidth = batch.PolygonCount;
            coveragePush.TexHeight = static_cast<u32>(numSetupIndices);
            fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
                0, Vk::PushConstantSize, &coveragePush);
            fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                Pipelines[VulkanShaders::Pipeline_CorrectCoverage]);
            fns.CmdDispatch(cmd, DivRoundUp(static_cast<u32>(numSetupIndices), 64), 1, 1);

            const VkBuffer corrected = ResultBuffer.GetHandle();
            BufferBarrier(cmd, &corrected, 1,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        }
    }

    // 9. final pass: edge marking / fog / anti-aliasing resolve.
    //
    // Variant bits match the OpenGL renderer exactly: DISP3DCNT bit 5 is edge
    // marking (0x1), bit 7 is fog (0x2) and bit 4 is anti-aliasing (0x4).
    u32 finalPassVariant = 0;
    if (GPU3D.RenderDispCnt & (1 << 4)) finalPassVariant |= 0x4;
    if (GPU3D.RenderDispCnt & (1 << 7)) finalPassVariant |= 0x2;
    if (GPU3D.RenderDispCnt & (1 << 5)) finalPassVariant |= 0x1;

    // A compositor submission from the previous DS frame may still be reading
    // FinalFB. Queue order plus this WAR dependency lets the CPU continue
    // immediately while preventing FinalPass from overwriting those texels
    // until the read has completed.
    FinalFB.RecordLayoutTransition(cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT);

    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        Pipelines[VulkanShaders::Pipeline_FinalPass0 + finalPassVariant]);
    fns.CmdDispatch(cmd, DivRoundUp(static_cast<u32>(ScreenWidth), 32),
        static_cast<u32>(ScreenHeight), 1);

    // 10. resolve to the DS's native resolution for display capture (GetLine()).
    //
    // Presentation does *not* go through here -- ComposeStructuredOutput()
    // samples FinalFB at its full internal resolution. This path exists because
    // display capture writes its result back into real VRAM as 15-bit DS words,
    // so it has to be 256x192 exactly. Match OpenGL's CaptureDownscaleFS by
    // selecting the centre texel of each internal-resolution pixel block;
    // averaging here changes the emulated capture contents. See Resolve.comp.
    //
    // FinalFB stays in GENERAL -- it is a storage image on both sides of this
    // barrier -- so only the FinalPass writes have to be made available to the
    // Resolve reads. Equal layouts, hence an execution+memory dependency rather
    // than a transition.
    FinalFB.RecordLayoutTransition(cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    // No WAR barrier for NativeResolveBuffer against the previous frame's
    // copy-to-readback: this renderer runs one frame in flight, so BeginFrame()
    // already waited on the fence that covered that copy.
    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        Pipelines[VulkanShaders::Pipeline_Resolve]);
    fns.CmdDispatch(cmd, DivRoundUp(256u, 8u), DivRoundUp(192u, 8u), 1);

    {
        const VkBuffer resolved = NativeResolveBuffer.GetHandle();
        BufferBarrier(cmd, &resolved, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    }

    {
        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = NativeResolveBytes;
        fns.CmdCopyBuffer(cmd,
            NativeResolveBuffer.GetHandle(), NativeReadback.GetHandle(), 1, &copy);
    }

    // Host visibility is not implicit: the transfer write has to be made
    // available to the HOST_READ access the CPU performs after the fence.
    {
        const VkBuffer readback = NativeReadback.GetHandle();
        BufferBarrier(cmd, &readback, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_HOST_BIT, VK_ACCESS_HOST_READ_BIT);
    }

    if (!FrameStaging.FlushWritten())
    {
        Frames.SubmitFrame(Device.GetMainQueue());
        SetRuntimeFailure("could not flush the staging ring");
        return;
    }

    PendingFence = frame->InFlightFence;
    bool submitted = false;
    {
        VulkanPerf::ScopedCpuTimer submitTimer(VulkanPerf::CpuMetric::QueueSubmit);
        submitted = Frames.SubmitFrame(Device.GetMainQueue());
    }
    if (submitted)
    {
        FrameInFlight = true;
        FrameReadbackValid = false;
        // FinalFB now carries a real frame. The compositor gates on this so it
        // never samples the undefined contents a freshly created image has.
        FinalFBHasContent = true;
    }
    else
    {
        PendingFence = VK_NULL_HANDLE;
        SetRuntimeFailure("frame command submission failed");
    }
}

void VulkanRenderer3D::EnsureFrameReadback()
{
    if (FrameReadbackValid || !FrameInFlight || PendingFence == VK_NULL_HANDLE)
        return;

    const Vk::DeviceDispatch& fns = Device.Fns();

    // Deliberately deferred to the first GetLine() of the frame rather than the
    // end of RenderFrame(): the GPU overlaps with whatever the emulation thread
    // does in between. This waits on *this frame's* fence only -- never
    // vkDeviceWaitIdle, which would also stall the presenter.
    const VkResult res = fns.WaitForFences(
        Device.GetHandle(), 1, &PendingFence, VK_TRUE, 1000000000ull /* 1 s */);
    if (res != VK_SUCCESS)
    {
        SetRuntimeFailure("the frame did not complete in time for the capture readback: "
            + Vk::FormatResult(res));
        FrameInFlight = false;
        return;
    }

    // HOST_CACHED readback memory is usually non-coherent, so the CPU's view of
    // it has to be invalidated before the first read.
    if (!NativeReadback.Invalidate())
    {
        SetRuntimeFailure("could not invalidate the capture readback mapping");
        FrameInFlight = false;
        return;
    }

    const u8* src = NativeReadback.GetData();
    if (!src)
    {
        SetRuntimeFailure("the capture readback buffer is not mapped");
        FrameInFlight = false;
        return;
    }

    // Resolve.comp already emitted the DS's packed r6g6b6a5 word, which is what
    // the software capture path consumes, so this is a straight copy: the
    // UNORM8 -> 6-bit reconstruction FinalPass.comp's normalisation made
    // necessary happens on the GPU now.
    std::memcpy(ColorBuffer.data(), src, ColorBuffer.size() * sizeof(u32));

    FrameInFlight = false;
    FrameReadbackValid = true;
}

bool VulkanRenderer3D::ComposeStructuredOutput(
    const std::array<const u32*, 14>& planes,
    const std::array<const u32*, 2>& lineMeta,
    const u32* captureCommands,
    u64 generation)
{
    if (RuntimeFailed || !Initialized || ScaleFactor <= 0)
        return false;
    if (ShaderStepIdx < ShaderStepCount)
        return false;       // pipelines are still being compiled

    // The producer bumps its generation once per DS frame. Composing the same
    // one twice would repeat a whole composition dispatch for a result that
    // cannot have changed.
    if (ComposedOutputValid && ComposedGeneration == generation)
        return true;

    if (Pipelines[VulkanShaders::Pipeline_Compositor] == VK_NULL_HANDLE
        || Pipelines[VulkanShaders::Pipeline_CaptureSidecar] == VK_NULL_HANDLE
        || !ComposedOutput || !FinalFB.IsValid())
    {
        SetRuntimeFailure("required compositor resources are unavailable");
        return false;
    }

    for (const u32* plane : planes)
    {
        if (!plane)
            return false;
    }
    for (const u32* meta : lineMeta)
    {
        if (!meta)
            return false;
    }
    if (!captureCommands)
        return false;

    const u32 nextSlot = static_cast<u32>(
        (ComposeFrames.GetAbsoluteFrame() - 1u) % CompositorFramesInFlight);
    if (ComposedOutput->Slots[nextSlot].PresenterRefs.load(std::memory_order_acquire) != 0)
    {
        // The presenter is still using this slot. Dropping one composed frame
        // is preferable to blocking VBlank on presentation; the previously
        // published frame remains valid and is reused.
        VulkanPerf::AddCounter(VulkanPerf::Counter::CompositorDropCount);
        return false;
    }

    OutputState::Slot& outputSlot = ComposedOutput->Slots[nextSlot];
    u32* staging = static_cast<u32*>(outputSlot.StructuredStaging.GetMappedPointer());
    if (!staging)
    {
        SetRuntimeFailure("the structured staging buffer is not mapped");
        return false;
    }

    {
        VulkanPerf::ScopedCpuTimer packTimer(VulkanPerf::CpuMetric::ComposePack);
        // Pack exactly the layout PresentationBuffers.glsl documents: six
        // native-resolution planes, then the two per-screen line-metadata arrays.
        for (std::size_t i = 0; i < planes.size(); i++)
        {
            std::memcpy(
                staging + i * StructuredPixelCount,
                planes[i],
                static_cast<std::size_t>(StructuredPixelCount) * sizeof(u32));
        }
        u32* metaDestination = staging + (planes.size() * StructuredPixelCount);
        std::memcpy(metaDestination, lineMeta[0], 192u * sizeof(u32));
        std::memcpy(metaDestination + 192u, lineMeta[1], 192u * sizeof(u32));
        std::memcpy(
            metaDestination + 384u,
            captureCommands,
            StructuredCaptureCommandCount * sizeof(u32));
    }
    VulkanPerf::AddCounter(VulkanPerf::Counter::StructuredPackBytes, StructuredInputBytes);

    if (!outputSlot.StructuredStaging.FlushRange(0, StructuredInputBytes))
    {
        SetRuntimeFailure("could not flush the structured staging buffer");
        return false;
    }

    const Vk::DeviceDispatch& fns = Device.Fns();

    Vk::FrameContext* frame = ComposeFrames.TryBeginFrame();
    if (!frame)
    {
        VulkanPerf::AddCounter(VulkanPerf::Counter::CompositorDropCount);
        return false;
    }
    VkCommandBuffer cmd = frame->CommandBuffer;
    const u32 frameIndex = ComposeFrames.GetFrameIndex();
    if (frameIndex != nextSlot)
    {
        ComposeFrames.SubmitFrame(Device.GetMainQueue());
        SetRuntimeFailure("the compositor frame ring selected an unexpected slot");
        return false;
    }

    {
        VkBufferCopy copy{};
        copy.srcOffset = 0;
        copy.dstOffset = 0;
        copy.size = StructuredInputBytes;
        fns.CmdCopyBuffer(cmd,
            outputSlot.StructuredStaging.GetHandle(),
            outputSlot.StructuredInput.GetHandle(), 1, &copy);

        const VkBuffer structured = outputSlot.StructuredInput.GetHandle();
        BufferBarrier(cmd, &structured, 1,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
    }

    // FinalFB was written by FinalPass and read by Resolve in a *different*
    // submission. A pipeline barrier's first synchronization scope includes
    // everything already submitted to the same queue, so naming the producing
    // stage and access here is what makes those writes available to the
    // compositor's reads. The layout does not change -- FinalFB lives in
    // GENERAL for its whole lifetime -- so this is a dependency, not a
    // transition.
    FinalFB.RecordLayoutTransition(cmd,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);

    // Set-0 slot 1: identical to the rasterizer's set except that binding 13
    // points at the composed output instead of the native capture buffer.
    if (!WriteRasterizerDescriptorSet(
            frameIndex, CompositorSetSlot, outputSlot.Composed.GetHandle(),
            outputSlot.StructuredInput.GetHandle()))
    {
        ComposeFrames.SubmitFrame(Device.GetMainQueue());
        SetRuntimeFailure("could not write the compositor descriptor set");
        return false;
    }

    VkDescriptorSet compositorSet = Descriptors.GetRasterizerSet(frameIndex, CompositorSetSlot);
    fns.CmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, Layouts.GetPipelineLayout(),
        Vk::RasterizerSetIndex, 1, &compositorSet, 0, nullptr);
    // Set 1 is deliberately not bound: Compositor.comp declares no set-1
    // resource, and rebinding a texture set here could overwrite one the
    // rasterizer's still-pending submission is reading.

    Vk::RasterizerPushConstants push{};
    // Reused as "this frame's 3D image is real", matching the DX12 compositor.
    // Zero when GPU3D aborted the frame (RenderFrame() never ran) or when
    // nothing has been rendered into FinalFB yet; the shader then leaves every
    // 3D slot showing the 2D pixel underneath, which is what the software
    // renderer produces from an all-transparent 3D line.
    push.TexWidth = (GPU3D.AbortFrame || !FinalFBHasContent) ? 0u : 1u;
    fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
        0, Vk::PushConstantSize, &push);

    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        Pipelines[VulkanShaders::Pipeline_CaptureSidecar]);
    const VkBuffer captureSidecar = CaptureSidecarBuffer.GetHandle();
    for (u32 captureLine = 0; captureLine < 192u; ++captureLine)
    {
        if ((captureCommands[captureLine * 4u + 1u]
                & StructuredComposition::kCaptureCommandValid) == 0u)
            continue;
        push.TexHeight = captureLine;
        fns.CmdPushConstants(cmd, Layouts.GetPipelineLayout(), VK_SHADER_STAGE_COMPUTE_BIT,
            0, Vk::PushConstantSize, &push);
        fns.CmdDispatch(cmd,
            DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
            DivRoundUp(static_cast<u32>(ScaleFactor), 8u),
            1u);
        BufferBarrier(cmd, &captureSidecar, 1,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
    }

    fns.CmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
        Pipelines[VulkanShaders::Pipeline_Compositor]);
    // One dispatch covers both screens in the slot's device-local buffer.
    fns.CmdDispatch(cmd,
        DivRoundUp(static_cast<u32>(ScreenWidth), 8u),
        DivRoundUp(static_cast<u32>(ScreenHeight) * 2u, 8u),
        1);

    bool composeSubmitted = false;
    {
        VulkanPerf::ScopedCpuTimer submitTimer(VulkanPerf::CpuMetric::QueueSubmit);
        composeSubmitted = ComposeFrames.SubmitFrame(Device.GetMainQueue());
    }
    if (!composeSubmitted)
    {
        SetRuntimeFailure("compositor command submission failed");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(ComposedOutput->Mutex);
        outputSlot.Frame.Serial = ComposedOutput->NextSerial++;
        outputSlot.Frame.Generation = generation;
        ComposedOutput->PublishedSlot = static_cast<int>(nextSlot);
        ComposedGeneration = generation;
        ComposedOutputValid = true;
    }
    return true;
}

const u32* VulkanRenderer3D::GetComposedScreen(u32 screen) const noexcept
{
    (void)screen;
    return nullptr;
}

RendererOutput VulkanRenderer3D::GetComposedOutput() const
{
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    std::lock_guard<std::mutex> lock(state->Mutex);
    if (state->PublishedSlot < 0)
        return {};
    const VulkanPresentedFrame& frame = state->Slots[state->PublishedSlot].Frame;
    return RendererOutput::VulkanBuffer(
        const_cast<VulkanPresentedFrame*>(&frame), frame.Width, frame.Height, frame.Serial);
}

RendererOutputLease VulkanRenderer3D::AcquireComposedOutputLease()
{
    const std::shared_ptr<OutputState> state = ComposedOutput;
    if (!state || !ComposedOutputValid)
        return {};

    std::lock_guard<std::mutex> lock(state->Mutex);
    if (state->PublishedSlot < 0)
        return {};

    const int slotIndex = state->PublishedSlot;
    OutputState::Slot& slot = state->Slots[slotIndex];
    slot.PresenterRefs.fetch_add(1, std::memory_order_relaxed);

    auto release = +[](void* opaque) {
        auto* leasedSlot = static_cast<OutputState::Slot*>(opaque);
        const u32 previous = leasedSlot->PresenterRefs.fetch_sub(1, std::memory_order_release);
        assert(previous > 0);
    };

    return RendererOutputLease(
        RendererOutput::VulkanBuffer(
            &slot.Frame, slot.Frame.Width, slot.Frame.Height, slot.Frame.Serial),
        &slot,
        release,
        state);
}

u32* VulkanRenderer3D::GetLine(int line)
{
    if (RuntimeFailed || !Initialized || GPU3D.AbortFrame || line < 0 || line >= 192)
    {
        std::memset(ScrolledLine, 0, sizeof(ScrolledLine));
        return ScrolledLine;
    }

    EnsureFrameReadback();

    if (!FrameReadbackValid)
    {
        // Nothing has been rendered yet (pipelines still compiling, or the
        // first frame has not been submitted). A transparent line is the
        // truthful answer, and it is what the software 2D path blends against.
        std::memset(ScrolledLine, 0, sizeof(ScrolledLine));
        return ScrolledLine;
    }

    u32* rawline = &ColorBuffer[static_cast<size_t>(line) * 256];

    const u16 xpos = GPU3D.RenderXPos;
    if (xpos == 0)
        return rawline;

    // Same X-scroll handling as SoftRenderer3D::GetLine(). The source line is
    // exactly 256 pixels here (the readback is already at native resolution),
    // so the out-of-range half is transparent.
    int i = 0;
    if (xpos & 0x100)
    {
        int j = xpos;
        for (; j < 512; i++, j++)
            ScrolledLine[i] = 0;
        for (j = 0; i < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
    }
    else
    {
        int j = xpos;
        for (; j < 256; i++, j++)
            ScrolledLine[i] = rawline[j];
        for (; i < 256; i++)
            ScrolledLine[i] = 0;
    }

    return ScrolledLine;
}

} // namespace melonDS

#endif // MELONPRIME_DS && MELONPRIME_ENABLE_VULKAN
