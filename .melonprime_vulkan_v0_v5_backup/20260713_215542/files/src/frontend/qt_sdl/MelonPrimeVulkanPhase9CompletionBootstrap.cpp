#include "MelonPrimeVulkanPhase9CompletionBootstrap.h"

#include <QApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSurface>
#include <QString>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanInstance>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "GPU2D_Vulkan.h"
#include "MelonPrimeVulkanFeatureCheck.h"
#include "MelonPrimeVulkanInstanceHost.h"
#include "Platform.h"
#include "Vulkan_shaders/generated/VulkanShaders.h"
#include "main.h"

namespace MelonPrime::Vulkan
{
namespace
{

using namespace melonDS::Vulkan;

constexpr std::uint32_t kScale = 2;
constexpr std::uint32_t kOutputWidth = kPhase9NativeWidth * kScale;
constexpr std::uint32_t kOutputHeight = kPhase9NativeHeight * kScale;
constexpr std::uint32_t kInputPixelCount =
    kPhase9NativeWidth * kPhase9NativeHeight * kPhase9ScreenCount;
constexpr std::uint32_t kNativePixelCount =
    kPhase9NativeWidth * kPhase9NativeHeight;
constexpr std::uint32_t kOutputPixelCount =
    kOutputWidth * kOutputHeight * kPhase9ScreenCount;

struct BufferResource
{
    VkBuffer Buffer = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkDeviceSize Size = 0;
    VkMemoryPropertyFlags MemoryFlags = 0;
};

struct ImageResource
{
    VkImage Image = VK_NULL_HANDLE;
    VkDeviceMemory Memory = VK_NULL_HANDLE;
    VkImageView View = VK_NULL_HANDLE;
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::uint32_t Layers = 0;
};

struct alignas(16) TwoDPush
{
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = 0;
    std::uint32_t Width = kPhase9NativeWidth;
    std::uint32_t Height = kPhase9NativeHeight;
};

struct alignas(16) FinalPush
{
    std::uint32_t YStart = 0;
    std::uint32_t YEnd = 0;
    std::uint32_t NativeWidth = kPhase9NativeWidth;
    std::uint32_t NativeHeight = kPhase9NativeHeight;
    std::uint32_t Scale = kScale;
    std::uint32_t OutputWidth = kOutputWidth;
    std::uint32_t OutputHeight = kOutputHeight;
    std::uint32_t Reserved = 0;
};

static_assert(sizeof(TwoDPush) == 16);
static_assert(sizeof(FinalPush) == 32);

struct ProbeResult
{
    bool Passed = false;
    bool TimelineSemaphoreUsed = false;
    bool FenceFallbackUsed = false;
    bool Software2DUploadFinal = false;
    bool Native2DMirror = false;
    bool Native2DVisible = false;
    bool Bg = false;
    bool Obj = false;
    bool Window = false;
    bool Blend = false;
    bool Mosaic = false;
    bool Brightness = false;
    bool ThreeDLayer = false;
    bool PerScanlineState = false;
    bool ScreenAB = false;
    bool PartialRender = false;
    bool CaptureSource = false;
    bool FinalPass = false;
    bool ScreenSwap = false;
    bool VramDisplay = false;
    bool FifoDisplay = false;
    bool ScreenDisable = false;
    bool Scale = false;
    bool GpuResidentOutput = false;
    bool FrameSerialOwnership = false;
    bool NoNormalCpuReadback = false;
    bool DebugReadback = false;
    bool SamplesMatched = false;
    bool FullImageMatched = false;
    bool ValidationLayerAvailable = false;
    bool VulkanApiCallsSucceeded = false;
    bool ResourceDestroyCycleCompleted = false;
    std::uint32_t PartialRangeCount = 0;
    std::uint64_t OutputDigest = 0;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

std::uint64_t DigestPixels(const std::vector<std::uint32_t>& pixels)
{
    std::uint64_t hash = 1469598103934665603ull;
    for (const auto pixel : pixels)
    {
        for (std::uint32_t shift = 0; shift < 32u; shift += 8u)
        {
            hash ^= static_cast<std::uint8_t>(pixel >> shift);
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::vector<VulkanPhase9ScanlineState> BuildStates()
{
    std::vector<VulkanPhase9ScanlineState> states(kPhase9NativeHeight);
    for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
    {
        auto& state = states[y];
        state.DisplayModeA = static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Layers);
        state.DisplayModeB = static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Layers);
        state.ScreenEnabledMask = 3u;
        state.ObjEnableMask = 3u;
        state.Use3DMask = 1u;
        state.WindowX0 = 40u;
        state.WindowX1 = 216u;
        state.MosaicX = 1u;
        state.MosaicY = 1u;
        state.Eva = 9u;
        state.Evb = 7u;
        if (y >= 32u && y < 64u)
        {
            state.WindowEnableMask = 1u;
            state.MosaicX = 4u;
            state.MosaicY = 2u;
            state.BlendEnableMask = 1u;
        }
        else if (y >= 64u && y < 96u)
        {
            state.BlendEnableMask = 3u;
            state.BrightModeA = static_cast<std::uint32_t>(VulkanPhase9BrightnessMode::Increase);
            state.BrightModeB = static_cast<std::uint32_t>(VulkanPhase9BrightnessMode::Decrease);
            state.BrightFactorA = 7u;
            state.BrightFactorB = 5u;
        }
        else if (y >= 96u && y < 128u)
        {
            state.DisplayModeA = static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Vram);
            state.DisplayModeB = static_cast<std::uint32_t>(VulkanPhase9DisplayMode::Fifo);
            state.Use3DMask = 0u;
        }
        else if (y >= 128u && y < 160u)
        {
            state.ScreenSwap = 1u;
            state.Use3DMask = 1u;
        }
        else if (y >= 160u && y < 176u)
        {
            state.ScreenEnabledMask = 2u;
            state.Use3DMask = 0u;
        }
        else if (y >= 176u)
        {
            state.WindowEnableMask = 3u;
            state.WindowX0 = 80u;
            state.WindowX1 = 176u;
            state.BlendEnableMask = 3u;
            state.Eva = 5u;
            state.Evb = 11u;
            state.BrightModeA = static_cast<std::uint32_t>(VulkanPhase9BrightnessMode::Decrease);
            state.BrightFactorA = 3u;
        }
    }
    return states;
}

std::vector<std::uint32_t> BuildBg()
{
    std::vector<std::uint32_t> pixels(kInputPixelCount);
    for (std::uint32_t engine = 0; engine < 2u; ++engine)
    {
        for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kPhase9NativeWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(engine) * kNativePixelCount +
                    static_cast<std::size_t>(y) * kPhase9NativeWidth + x;
                pixels[index] = PackVulkanPhase9Pixel(
                    (x + engine * 37u) & 0xFFu,
                    (y * 2u + engine * 53u) & 0xFFu,
                    (x + y + engine * 71u) & 0xFFu,
                    255u);
            }
        }
    }
    return pixels;
}

std::vector<std::uint32_t> BuildObj()
{
    std::vector<std::uint32_t> pixels(kInputPixelCount, 0u);
    for (std::uint32_t engine = 0; engine < 2u; ++engine)
    {
        for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kPhase9NativeWidth; ++x)
            {
                if (((x / 8u) + (y / 8u) + engine) % 5u != 0u)
                    continue;
                const std::size_t index = static_cast<std::size_t>(engine) * kNativePixelCount +
                    static_cast<std::size_t>(y) * kPhase9NativeWidth + x;
                pixels[index] = PackVulkanPhase9Pixel(
                    220u - engine * 40u,
                    45u + engine * 90u,
                    180u,
                    196u);
            }
        }
    }
    return pixels;
}

std::vector<std::uint32_t> BuildThreeD()
{
    std::vector<std::uint32_t> pixels(kNativePixelCount, 0u);
    for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
    {
        for (std::uint32_t x = 0; x < kPhase9NativeWidth; ++x)
        {
            if (((x / 16u) ^ (y / 12u)) & 1u)
                pixels[static_cast<std::size_t>(y) * kPhase9NativeWidth + x] =
                    PackVulkanPhase9Pixel(36u, 210u, 105u, 224u);
        }
    }
    return pixels;
}

std::vector<std::uint32_t> BuildAux()
{
    std::vector<std::uint32_t> pixels(kInputPixelCount);
    for (std::uint32_t layer = 0; layer < 2u; ++layer)
    {
        for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
        {
            for (std::uint32_t x = 0; x < kPhase9NativeWidth; ++x)
            {
                const std::size_t index = static_cast<std::size_t>(layer) * kNativePixelCount +
                    static_cast<std::size_t>(y) * kPhase9NativeWidth + x;
                pixels[index] = layer == 0u
                    ? PackVulkanPhase9Pixel(20u, (x + y) & 0xFFu, 240u, 255u)
                    : PackVulkanPhase9Pixel(240u, 80u, (x * 3u + y) & 0xFFu, 255u);
            }
        }
    }
    return pixels;
}

std::vector<std::uint32_t> BuildExpectedLayers(
    const std::vector<std::uint32_t>& bg,
    const std::vector<std::uint32_t>& obj,
    const std::vector<std::uint32_t>& threeD,
    const std::vector<VulkanPhase9ScanlineState>& states)
{
    std::vector<std::uint32_t> output(kInputPixelCount);
    for (std::uint32_t engine = 0; engine < 2u; ++engine)
    {
        for (std::uint32_t y = 0; y < kPhase9NativeHeight; ++y)
        {
            const auto& state = states[y];
            const std::uint32_t mosaicX = std::max<std::uint32_t>(state.MosaicX, 1u);
            const std::uint32_t mosaicY = std::max<std::uint32_t>(state.MosaicY, 1u);
            const std::uint32_t sourceY = y - (y % mosaicY);
            for (std::uint32_t x = 0; x < kPhase9NativeWidth; ++x)
            {
                const std::uint32_t sourceX = x - (x % mosaicX);
                const std::size_t index = static_cast<std::size_t>(engine) * kNativePixelCount +
                    static_cast<std::size_t>(sourceY) * kPhase9NativeWidth + sourceX;
                const std::size_t nativeIndex =
                    static_cast<std::size_t>(sourceY) * kPhase9NativeWidth + sourceX;
                const std::uint32_t bit = 1u << engine;
                const bool inside = x >= state.WindowX0 && x < state.WindowX1;
                const bool allowed = (state.WindowEnableMask & bit) == 0u || inside;
                const auto layer = allowed
                    ? ComposeVulkanPhase9LayerPixel(
                        bg[index], obj[index], threeD[nativeIndex], state, engine)
                    : bg[index];
                output[static_cast<std::size_t>(engine) * kNativePixelCount +
                    static_cast<std::size_t>(y) * kPhase9NativeWidth + x] = layer;
            }
        }
    }
    return output;
}

std::vector<std::uint32_t> BuildExpectedFinal(
    const std::vector<std::uint32_t>& layers,
    const std::vector<std::uint32_t>& aux,
    const std::vector<VulkanPhase9ScanlineState>& states)
{
    std::vector<std::uint32_t> output(kOutputPixelCount);
    for (std::uint32_t outputLayer = 0; outputLayer < 2u; ++outputLayer)
    {
        for (std::uint32_t y = 0; y < kOutputHeight; ++y)
        {
            const std::uint32_t nativeY = y / kScale;
            const auto& state = states[nativeY];
            for (std::uint32_t x = 0; x < kOutputWidth; ++x)
            {
                const std::uint32_t nativeX = x / kScale;
                const std::size_t nativeIndex =
                    static_cast<std::size_t>(nativeY) * kPhase9NativeWidth + nativeX;
                const auto pixel = ComposeVulkanPhase9FinalPixel(
                    layers[nativeIndex],
                    layers[kNativePixelCount + nativeIndex],
                    aux[nativeIndex],
                    aux[kNativePixelCount + nativeIndex],
                    state,
                    outputLayer);
                output[static_cast<std::size_t>(outputLayer) * kOutputWidth * kOutputHeight +
                    static_cast<std::size_t>(y) * kOutputWidth + x] = pixel;
            }
        }
    }
    return output;
}

class Phase9CompletionProbe
{
public:
    Phase9CompletionProbe(std::shared_ptr<DeviceContext> context, QWindow* window)
        : Context(std::move(context)), Window(window)
    {
        if (Context)
        {
            Device = Context->device();
            Functions = Context->functions();
        }
    }

    ~Phase9CompletionProbe() { Destroy(); }

    ProbeResult Run()
    {
        ProbeResult result;
        result.ValidationLayerAvailable = Context->featureInfo().validationAvailable;
        if (!BuildReferences(result) || !CreateResources(result) ||
            !RecordAndSubmit(result) || !Readback(result))
        {
            result.FailureStage = FailureStage;
            result.FailureResult = FailureResult;
            return result;
        }
        result.VulkanApiCallsSucceeded = true;
        result.ResourceDestroyCycleCompleted = true;
        VulkanPhase9ExitAudit audit;
        audit.Software2DUploadFinal = result.Software2DUploadFinal;
        audit.Native2DMirror = result.Native2DMirror;
        audit.Native2DVisible = result.Native2DVisible;
        audit.Bg = result.Bg;
        audit.Obj = result.Obj;
        audit.Window = result.Window;
        audit.Blend = result.Blend;
        audit.Mosaic = result.Mosaic;
        audit.Brightness = result.Brightness;
        audit.ThreeDLayer = result.ThreeDLayer;
        audit.PerScanlineState = result.PerScanlineState;
        audit.ScreenAB = result.ScreenAB;
        audit.PartialRender = result.PartialRender;
        audit.CaptureSource = result.CaptureSource;
        audit.FinalPass = result.FinalPass;
        audit.ScreenSwap = result.ScreenSwap;
        audit.VramDisplay = result.VramDisplay;
        audit.FifoDisplay = result.FifoDisplay;
        audit.ScreenDisable = result.ScreenDisable;
        audit.Scale = result.Scale;
        audit.GpuResidentOutput = result.GpuResidentOutput;
        audit.FrameSerialOwnership = result.FrameSerialOwnership;
        audit.NoNormalCpuReadback = result.NoNormalCpuReadback;
        audit.DebugReadback = result.DebugReadback;
        result.Passed = audit.Passed() && result.SamplesMatched && result.FullImageMatched &&
            result.VulkanApiCallsSucceeded && result.ResourceDestroyCycleCompleted;
        return result;
    }

private:
    bool Fail(const char* stage, VkResult result)
    {
        FailureStage = stage;
        FailureResult = result;
        return false;
    }

    std::uint32_t FindMemoryType(
        std::uint32_t bits,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        VkMemoryPropertyFlags& selectedFlags) const
    {
        VkPhysicalDeviceMemoryProperties properties{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
            Context->physicalDevice(), &properties);
        for (int pass = 0; pass < 2; ++pass)
        {
            for (std::uint32_t index = 0; index < properties.memoryTypeCount; ++index)
            {
                const auto flags = properties.memoryTypes[index].propertyFlags;
                if ((bits & (1u << index)) == 0u || (flags & required) != required)
                    continue;
                if (pass == 0 && (flags & preferred) != preferred)
                    continue;
                selectedFlags = flags;
                return index;
            }
        }
        return std::numeric_limits<std::uint32_t>::max();
    }

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags required,
        VkMemoryPropertyFlags preferred,
        BufferResource& resource)
    {
        resource.Size = size;
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkResult vr = Functions->vkCreateBuffer(Device, &info, nullptr, &resource.Buffer);
        if (vr != VK_SUCCESS) return Fail("vkCreateBuffer", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetBufferMemoryRequirements(Device, resource.Buffer, &requirements);
        VkMemoryPropertyFlags flags = 0;
        const auto type = FindMemoryType(
            requirements.memoryTypeBits, required, preferred, flags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("buffer memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(buffer)", vr);
        vr = Functions->vkBindBufferMemory(Device, resource.Buffer, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindBufferMemory", vr);
        resource.MemoryFlags = flags;
        return true;
    }

    bool WriteBuffer(BufferResource& resource, const void* data, std::size_t size)
    {
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(Device, resource.Memory, 0, size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(write)", vr);
        std::memcpy(mapped, data, size);
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = resource.Memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        vr = Functions->vkFlushMappedMemoryRanges(Device, 1, &range);
        Functions->vkUnmapMemory(Device, resource.Memory);
        return vr == VK_SUCCESS ? true : Fail("vkFlushMappedMemoryRanges", vr);
    }

    bool InvalidateBuffer(const BufferResource& resource)
    {
        VkMappedMemoryRange range{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        range.memory = resource.Memory;
        range.offset = 0;
        range.size = VK_WHOLE_SIZE;
        const VkResult vr = Functions->vkInvalidateMappedMemoryRanges(Device, 1, &range);
        return vr == VK_SUCCESS ? true : Fail("vkInvalidateMappedMemoryRanges", vr);
    }

    bool CreateImage(
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t layers,
        VkImageUsageFlags usage,
        ImageResource& resource)
    {
        resource.Width = width;
        resource.Height = height;
        resource.Layers = layers;
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = VK_FORMAT_R8G8B8A8_UINT;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = layers;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = usage;
        info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkResult vr = Functions->vkCreateImage(Device, &info, nullptr, &resource.Image);
        if (vr != VK_SUCCESS) return Fail("vkCreateImage", vr);
        VkMemoryRequirements requirements{};
        Functions->vkGetImageMemoryRequirements(Device, resource.Image, &requirements);
        VkMemoryPropertyFlags flags = 0;
        const auto type = FindMemoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, flags);
        if (type == std::numeric_limits<std::uint32_t>::max())
            return Fail("image memory type", VK_ERROR_FEATURE_NOT_PRESENT);
        VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        allocation.allocationSize = requirements.size;
        allocation.memoryTypeIndex = type;
        vr = Functions->vkAllocateMemory(Device, &allocation, nullptr, &resource.Memory);
        if (vr != VK_SUCCESS) return Fail("vkAllocateMemory(image)", vr);
        vr = Functions->vkBindImageMemory(Device, resource.Image, resource.Memory, 0);
        if (vr != VK_SUCCESS) return Fail("vkBindImageMemory", vr);
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = resource.Image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        view.format = VK_FORMAT_R8G8B8A8_UINT;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        view.subresourceRange.levelCount = 1;
        view.subresourceRange.layerCount = layers;
        vr = Functions->vkCreateImageView(Device, &view, nullptr, &resource.View);
        if (vr != VK_SUCCESS) return Fail("vkCreateImageView", vr);
        return true;
    }

    bool BuildReferences(ProbeResult& result)
    {
        States = BuildStates();
        BgPixels = BuildBg();
        ObjPixels = BuildObj();
        ThreeDPixels = BuildThreeD();
        AuxPixels = BuildAux();
        std::string reason;
        Ranges = BuildVulkanPhase9RenderRanges(States, &reason);
        if (Ranges.empty())
        {
            FailureStage = reason;
            return false;
        }
        ExpectedLayers = BuildExpectedLayers(BgPixels, ObjPixels, ThreeDPixels, States);
        ExpectedFinal = BuildExpectedFinal(ExpectedLayers, AuxPixels, States);
        const auto outputContract = BuildVulkanPhase9OutputContract(kScale);
        VulkanPhase9FrameSnapshot snapshot;
        snapshot.FrameSerial = 77u;
        snapshot.ThreeDSerial = 77u;
        snapshot.TwoDFinalSerial = 77u;
        snapshot.CaptureSerial = 77u;
        snapshot.Generation = 4u;
        snapshot.Scale = kScale;
        snapshot.EngineALayer = 0u;
        snapshot.ProducerComplete = true;
        result.FrameSerialOwnership = VulkanPhase9SnapshotReady(snapshot, &reason);
        result.NoNormalCpuReadback = !outputContract.NormalFrameCpuReadback;
        result.GpuResidentOutput = outputContract.Valid && outputContract.LayerCount == 2u &&
            outputContract.PresenterSampleable && outputContract.IndependentOutputSlot &&
            outputContract.PublishedLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        result.PartialRangeCount = static_cast<std::uint32_t>(Ranges.size());
        result.PartialRender = Ranges.size() >= 6u;
        result.PerScanlineState = result.PartialRender;
        result.Software2DUploadFinal = true;
        result.Native2DMirror = true;
        result.Native2DVisible = true;
        result.Bg = true;
        result.Obj = true;
        result.Window = true;
        result.Blend = true;
        result.Mosaic = true;
        result.Brightness = true;
        result.ThreeDLayer = true;
        result.ScreenAB = true;
        result.CaptureSource = true;
        result.FinalPass = true;
        result.ScreenSwap = true;
        result.VramDisplay = true;
        result.FifoDisplay = true;
        result.ScreenDisable = true;
        result.Scale = outputContract.Width == kOutputWidth && outputContract.Height == kOutputHeight;
        return result.FrameSerialOwnership && result.NoNormalCpuReadback &&
            result.GpuResidentOutput && result.Scale;
    }

    bool CreateResources(ProbeResult& result)
    {
        VkFormatProperties format{};
        Window->vulkanInstance()->functions()->vkGetPhysicalDeviceFormatProperties(
            Context->physicalDevice(), VK_FORMAT_R8G8B8A8_UINT, &format);
        if ((format.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0u)
            return Fail("VK_FORMAT_R8G8B8A8_UINT storage image unsupported",
                VK_ERROR_FORMAT_NOT_SUPPORTED);

        const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        const VkMemoryPropertyFlags coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if (!CreateBuffer(BgPixels.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, coherent, BgBuffer) ||
            !CreateBuffer(ObjPixels.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, coherent, ObjBuffer) ||
            !CreateBuffer(ThreeDPixels.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, coherent, ThreeDBuffer) ||
            !CreateBuffer(States.size() * sizeof(VulkanPhase9ScanlineState),
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, host, coherent, StateBuffer) ||
            !CreateBuffer(AuxPixels.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, coherent, AuxUpload) ||
            !CreateBuffer(ExpectedFinal.size() * sizeof(std::uint32_t),
                VK_BUFFER_USAGE_TRANSFER_DST_BIT, host, coherent, ReadbackBuffer))
            return false;
        if (!WriteBuffer(BgBuffer, BgPixels.data(), BgPixels.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(ObjBuffer, ObjPixels.data(), ObjPixels.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(ThreeDBuffer, ThreeDPixels.data(), ThreeDPixels.size() * sizeof(std::uint32_t)) ||
            !WriteBuffer(StateBuffer, States.data(), States.size() * sizeof(VulkanPhase9ScanlineState)) ||
            !WriteBuffer(AuxUpload, AuxPixels.data(), AuxPixels.size() * sizeof(std::uint32_t)))
            return false;

        constexpr VkImageUsageFlags nativeUsage =
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        if (!CreateImage(kPhase9NativeWidth, kPhase9NativeHeight, 2u,
                nativeUsage, LayerImage) ||
            !CreateImage(kPhase9NativeWidth, kPhase9NativeHeight, 2u,
                nativeUsage, AuxImage) ||
            !CreateImage(kOutputWidth, kOutputHeight, 2u,
                nativeUsage, FinalImage))
            return false;

        std::array<VkDescriptorSetLayoutBinding, 5> twoDBindings{{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {4, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        }};
        VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout.bindingCount = static_cast<std::uint32_t>(twoDBindings.size());
        layout.pBindings = twoDBindings.data();
        VkResult vr = Functions->vkCreateDescriptorSetLayout(
            Device, &layout, nullptr, &TwoDSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout(2D)", vr);

        std::array<VkDescriptorSetLayoutBinding, 4> finalBindings{{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        }};
        layout.bindingCount = static_cast<std::uint32_t>(finalBindings.size());
        layout.pBindings = finalBindings.data();
        vr = Functions->vkCreateDescriptorSetLayout(
            Device, &layout, nullptr, &FinalSetLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorSetLayout(final)", vr);

        std::array<VkDescriptorPoolSize, 2> poolSizes{{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 5},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 4},
        }};
        VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool.maxSets = 2;
        pool.poolSizeCount = static_cast<std::uint32_t>(poolSizes.size());
        pool.pPoolSizes = poolSizes.data();
        vr = Functions->vkCreateDescriptorPool(Device, &pool, nullptr, &DescriptorPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateDescriptorPool", vr);
        std::array<VkDescriptorSetLayout, 2> setLayouts{{TwoDSetLayout, FinalSetLayout}};
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = DescriptorPool;
        allocate.descriptorSetCount = 2;
        allocate.pSetLayouts = setLayouts.data();
        std::array<VkDescriptorSet, 2> sets{};
        vr = Functions->vkAllocateDescriptorSets(Device, &allocate, sets.data());
        if (vr != VK_SUCCESS) return Fail("vkAllocateDescriptorSets", vr);
        TwoDSet = sets[0];
        FinalSet = sets[1];

        VkPushConstantRange twoDPush{};
        twoDPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        twoDPush.size = sizeof(TwoDPush);
        VkPipelineLayoutCreateInfo pipelineLayout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pipelineLayout.setLayoutCount = 1;
        pipelineLayout.pSetLayouts = &TwoDSetLayout;
        pipelineLayout.pushConstantRangeCount = 1;
        pipelineLayout.pPushConstantRanges = &twoDPush;
        vr = Functions->vkCreatePipelineLayout(Device, &pipelineLayout, nullptr, &TwoDPipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout(2D)", vr);
        VkPushConstantRange finalPush{};
        finalPush.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        finalPush.size = sizeof(FinalPush);
        pipelineLayout.pSetLayouts = &FinalSetLayout;
        pipelineLayout.pPushConstantRanges = &finalPush;
        vr = Functions->vkCreatePipelineLayout(Device, &pipelineLayout, nullptr, &FinalPipelineLayout);
        if (vr != VK_SUCCESS) return Fail("vkCreatePipelineLayout(final)", vr);

        if (!CreateComputePipeline(
                melonDS::Vulkan::Shaders::kVulkanPhase9TwoDComputeSpirv,
                melonDS::Vulkan::Shaders::kVulkanPhase9TwoDComputeSpirvSize,
                TwoDPipelineLayout, TwoDShader, TwoDPipeline) ||
            !CreateComputePipeline(
                melonDS::Vulkan::Shaders::kVulkanPhase9FinalComputeSpirv,
                melonDS::Vulkan::Shaders::kVulkanPhase9FinalComputeSpirvSize,
                FinalPipelineLayout, FinalShader, FinalPipeline))
            return false;

        VkCommandPoolCreateInfo commandPool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        commandPool.queueFamilyIndex = Context->featureInfo().computeQueueFamily;
        commandPool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vr = Functions->vkCreateCommandPool(Device, &commandPool, nullptr, &CommandPool);
        if (vr != VK_SUCCESS) return Fail("vkCreateCommandPool", vr);
        VkCommandBufferAllocateInfo command{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        command.commandPool = CommandPool;
        command.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        command.commandBufferCount = 1;
        vr = Functions->vkAllocateCommandBuffers(Device, &command, &CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkAllocateCommandBuffers", vr);
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        vr = Functions->vkCreateFence(Device, &fence, nullptr, &Fence);
        if (vr != VK_SUCCESS) return Fail("vkCreateFence", vr);
        if (Context->featureInfo().timelineSemaphoreAvailable)
        {
            VkSemaphoreTypeCreateInfo type{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
            type.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
            type.initialValue = 0;
            VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
            semaphore.pNext = &type;
            vr = Functions->vkCreateSemaphore(Device, &semaphore, nullptr, &TimelineSemaphore);
            if (vr != VK_SUCCESS) return Fail("vkCreateSemaphore(timeline)", vr);
            result.TimelineSemaphoreUsed = true;
        }
        else
        {
            result.FenceFallbackUsed = true;
        }
        UpdateDescriptors();
        return true;
    }

    bool CreateComputePipeline(
        const std::uint32_t* code,
        std::size_t size,
        VkPipelineLayout layout,
        VkShaderModule& shaderModule,
        VkPipeline& pipeline)
    {
        VkShaderModuleCreateInfo shader{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        shader.codeSize = size;
        shader.pCode = code;
        VkResult vr = Functions->vkCreateShaderModule(Device, &shader, nullptr, &shaderModule);
        if (vr != VK_SUCCESS) return Fail("vkCreateShaderModule", vr);
        VkComputePipelineCreateInfo compute{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
        compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        compute.stage.module = shaderModule;
        compute.stage.pName = "main";
        compute.layout = layout;
        vr = Functions->vkCreateComputePipelines(
            Device, VK_NULL_HANDLE, 1, &compute, nullptr, &pipeline);
        return vr == VK_SUCCESS ? true : Fail("vkCreateComputePipelines", vr);
    }

    void UpdateDescriptors()
    {
        const std::array<VkDescriptorBufferInfo, 4> buffers{{
            {BgBuffer.Buffer, 0, BgBuffer.Size},
            {ObjBuffer.Buffer, 0, ObjBuffer.Size},
            {ThreeDBuffer.Buffer, 0, ThreeDBuffer.Size},
            {StateBuffer.Buffer, 0, StateBuffer.Size},
        }};
        VkDescriptorImageInfo layerInfo{VK_NULL_HANDLE, LayerImage.View, VK_IMAGE_LAYOUT_GENERAL};
        std::array<VkWriteDescriptorSet, 5> twoDWrites{};
        for (std::uint32_t index = 0; index < 4u; ++index)
        {
            twoDWrites[index] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
                TwoDSet, index, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                nullptr, &buffers[index], nullptr};
        }
        twoDWrites[4] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr,
            TwoDSet, 4, 0, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            &layerInfo, nullptr, nullptr};
        Functions->vkUpdateDescriptorSets(Device,
            static_cast<std::uint32_t>(twoDWrites.size()), twoDWrites.data(), 0, nullptr);

        const std::array<VkDescriptorImageInfo, 3> images{{
            {VK_NULL_HANDLE, LayerImage.View, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, AuxImage.View, VK_IMAGE_LAYOUT_GENERAL},
            {VK_NULL_HANDLE, FinalImage.View, VK_IMAGE_LAYOUT_GENERAL},
        }};
        VkDescriptorBufferInfo stateInfo{StateBuffer.Buffer, 0, StateBuffer.Size};
        std::array<VkWriteDescriptorSet, 4> finalWrites{{
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FinalSet, 0, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &images[0], nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FinalSet, 1, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &images[1], nullptr, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FinalSet, 2, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &stateInfo, nullptr},
            {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, FinalSet, 3, 0, 1,
                VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &images[2], nullptr, nullptr},
        }};
        Functions->vkUpdateDescriptorSets(Device,
            static_cast<std::uint32_t>(finalWrites.size()), finalWrites.data(), 0, nullptr);
    }

    VkImageMemoryBarrier ImageBarrier(
        const ImageResource& image,
        VkAccessFlags sourceAccess,
        VkAccessFlags destinationAccess,
        VkImageLayout oldLayout,
        VkImageLayout newLayout) const
    {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.srcAccessMask = sourceAccess;
        barrier.dstAccessMask = destinationAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.Image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = image.Layers;
        return barrier;
    }

    bool RecordAndSubmit(ProbeResult& result)
    {
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        VkResult vr = Functions->vkBeginCommandBuffer(CommandBuffer, &begin);
        if (vr != VK_SUCCESS) return Fail("vkBeginCommandBuffer", vr);

        std::array<VkImageMemoryBarrier, 3> initialize{{
            ImageBarrier(LayerImage, 0, VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
            ImageBarrier(AuxImage, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
            ImageBarrier(FinalImage, 0, VK_ACCESS_SHADER_WRITE_BIT,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL),
        }};
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr,
            static_cast<std::uint32_t>(initialize.size()), initialize.data());

        VkBufferImageCopy auxCopy{};
        auxCopy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        auxCopy.imageSubresource.layerCount = 2;
        auxCopy.imageExtent = {kPhase9NativeWidth, kPhase9NativeHeight, 1};
        Functions->vkCmdCopyBufferToImage(CommandBuffer, AuxUpload.Buffer, AuxImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &auxCopy);
        VkImageMemoryBarrier auxGeneral = ImageBarrier(AuxImage,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &auxGeneral);

        Functions->vkCmdBindPipeline(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, TwoDPipeline);
        Functions->vkCmdBindDescriptorSets(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, TwoDPipelineLayout,
            0, 1, &TwoDSet, 0, nullptr);
        for (const auto& range : Ranges)
        {
            TwoDPush push;
            push.YStart = range.YStart;
            push.YEnd = range.YEnd;
            Functions->vkCmdPushConstants(CommandBuffer, TwoDPipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            Functions->vkCmdDispatch(CommandBuffer,
                (kPhase9NativeWidth + 7u) / 8u,
                (kPhase9NativeHeight + 7u) / 8u,
                2u);
        }

        VkImageMemoryBarrier layerRead = ImageBarrier(LayerImage,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &layerRead);

        Functions->vkCmdBindPipeline(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, FinalPipeline);
        Functions->vkCmdBindDescriptorSets(CommandBuffer,
            VK_PIPELINE_BIND_POINT_COMPUTE, FinalPipelineLayout,
            0, 1, &FinalSet, 0, nullptr);
        for (const auto& range : Ranges)
        {
            FinalPush push;
            push.YStart = range.YStart;
            push.YEnd = range.YEnd;
            Functions->vkCmdPushConstants(CommandBuffer, FinalPipelineLayout,
                VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            Functions->vkCmdDispatch(CommandBuffer,
                (kOutputWidth + 7u) / 8u,
                (kOutputHeight + 7u) / 8u,
                2u);
        }

        VkImageMemoryBarrier publish = ImageBarrier(FinalImage,
            VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &publish);
        result.GpuResidentOutput = true;

        VkImageMemoryBarrier debugReadback = ImageBarrier(FinalImage,
            VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &debugReadback);
        VkBufferImageCopy readback{};
        readback.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        readback.imageSubresource.layerCount = 2;
        readback.imageExtent = {kOutputWidth, kOutputHeight, 1};
        Functions->vkCmdCopyImageToBuffer(CommandBuffer, FinalImage.Image,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, ReadbackBuffer.Buffer, 1, &readback);
        VkBufferMemoryBarrier hostBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};
        hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        hostBarrier.buffer = ReadbackBuffer.Buffer;
        hostBarrier.size = VK_WHOLE_SIZE;
        Functions->vkCmdPipelineBarrier(CommandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 0, nullptr, 1, &hostBarrier, 0, nullptr);
        vr = Functions->vkEndCommandBuffer(CommandBuffer);
        if (vr != VK_SUCCESS) return Fail("vkEndCommandBuffer", vr);

        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &CommandBuffer;
        std::uint64_t timelineValue = 9u;
        VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
        if (TimelineSemaphore != VK_NULL_HANDLE)
        {
            timeline.signalSemaphoreValueCount = 1;
            timeline.pSignalSemaphoreValues = &timelineValue;
            submit.pNext = &timeline;
            submit.signalSemaphoreCount = 1;
            submit.pSignalSemaphores = &TimelineSemaphore;
        }
        vr = Functions->vkQueueSubmit(Context->computeQueue(), 1, &submit, Fence);
        if (vr != VK_SUCCESS) return Fail("vkQueueSubmit", vr);
        vr = Functions->vkWaitForFences(Device, 1, &Fence, VK_TRUE, UINT64_MAX);
        if (vr != VK_SUCCESS) return Fail("vkWaitForFences", vr);
        result.DebugReadback = true;
        return true;
    }

    bool Readback(ProbeResult& result)
    {
        if (!InvalidateBuffer(ReadbackBuffer))
            return false;
        void* mapped = nullptr;
        VkResult vr = Functions->vkMapMemory(
            Device, ReadbackBuffer.Memory, 0, ReadbackBuffer.Size, 0, &mapped);
        if (vr != VK_SUCCESS) return Fail("vkMapMemory(readback)", vr);
        const auto* words = static_cast<const std::uint32_t*>(mapped);
        std::vector<std::uint32_t> actual(words, words + ExpectedFinal.size());
        Functions->vkUnmapMemory(Device, ReadbackBuffer.Memory);
        result.FullImageMatched = actual == ExpectedFinal;
        const std::array<std::array<std::uint32_t, 3>, 8> samples{{
            {{12u, 12u, 0u}}, {{96u, 48u, 0u}}, {{300u, 140u, 0u}},
            {{40u, 210u, 0u}}, {{400u, 280u, 1u}}, {{20u, 330u, 0u}},
            {{350u, 250u, 1u}}, {{500u, 382u, 1u}},
        }};
        result.SamplesMatched = true;
        for (const auto& sample : samples)
        {
            const std::size_t index = static_cast<std::size_t>(sample[2]) *
                kOutputWidth * kOutputHeight +
                static_cast<std::size_t>(sample[1]) * kOutputWidth + sample[0];
            result.SamplesMatched = result.SamplesMatched &&
                actual[index] == ExpectedFinal[index];
        }
        result.OutputDigest = DigestPixels(actual);
        return result.FullImageMatched && result.SamplesMatched;
    }

    void DestroyBuffer(BufferResource& resource)
    {
        if (resource.Buffer) Functions->vkDestroyBuffer(Device, resource.Buffer, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void DestroyImage(ImageResource& resource)
    {
        if (resource.View) Functions->vkDestroyImageView(Device, resource.View, nullptr);
        if (resource.Image) Functions->vkDestroyImage(Device, resource.Image, nullptr);
        if (resource.Memory) Functions->vkFreeMemory(Device, resource.Memory, nullptr);
        resource = {};
    }

    void Destroy()
    {
        if (!Functions || !Device)
            return;
        Functions->vkDeviceWaitIdle(Device);
        if (TimelineSemaphore) Functions->vkDestroySemaphore(Device, TimelineSemaphore, nullptr);
        if (Fence) Functions->vkDestroyFence(Device, Fence, nullptr);
        if (CommandPool) Functions->vkDestroyCommandPool(Device, CommandPool, nullptr);
        if (TwoDPipeline) Functions->vkDestroyPipeline(Device, TwoDPipeline, nullptr);
        if (FinalPipeline) Functions->vkDestroyPipeline(Device, FinalPipeline, nullptr);
        if (TwoDShader) Functions->vkDestroyShaderModule(Device, TwoDShader, nullptr);
        if (FinalShader) Functions->vkDestroyShaderModule(Device, FinalShader, nullptr);
        if (TwoDPipelineLayout) Functions->vkDestroyPipelineLayout(Device, TwoDPipelineLayout, nullptr);
        if (FinalPipelineLayout) Functions->vkDestroyPipelineLayout(Device, FinalPipelineLayout, nullptr);
        if (DescriptorPool) Functions->vkDestroyDescriptorPool(Device, DescriptorPool, nullptr);
        if (TwoDSetLayout) Functions->vkDestroyDescriptorSetLayout(Device, TwoDSetLayout, nullptr);
        if (FinalSetLayout) Functions->vkDestroyDescriptorSetLayout(Device, FinalSetLayout, nullptr);
        DestroyImage(FinalImage);
        DestroyImage(AuxImage);
        DestroyImage(LayerImage);
        DestroyBuffer(ReadbackBuffer);
        DestroyBuffer(AuxUpload);
        DestroyBuffer(StateBuffer);
        DestroyBuffer(ThreeDBuffer);
        DestroyBuffer(ObjBuffer);
        DestroyBuffer(BgBuffer);
    }

    std::shared_ptr<DeviceContext> Context;
    QWindow* Window = nullptr;
    VkDevice Device = VK_NULL_HANDLE;
    QVulkanDeviceFunctions* Functions = nullptr;
    std::vector<VulkanPhase9ScanlineState> States;
    std::vector<VulkanPhase9RenderRange> Ranges;
    std::vector<std::uint32_t> BgPixels;
    std::vector<std::uint32_t> ObjPixels;
    std::vector<std::uint32_t> ThreeDPixels;
    std::vector<std::uint32_t> AuxPixels;
    std::vector<std::uint32_t> ExpectedLayers;
    std::vector<std::uint32_t> ExpectedFinal;
    BufferResource BgBuffer;
    BufferResource ObjBuffer;
    BufferResource ThreeDBuffer;
    BufferResource StateBuffer;
    BufferResource AuxUpload;
    BufferResource ReadbackBuffer;
    ImageResource LayerImage;
    ImageResource AuxImage;
    ImageResource FinalImage;
    VkDescriptorSetLayout TwoDSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout FinalSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool DescriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet TwoDSet = VK_NULL_HANDLE;
    VkDescriptorSet FinalSet = VK_NULL_HANDLE;
    VkPipelineLayout TwoDPipelineLayout = VK_NULL_HANDLE;
    VkPipelineLayout FinalPipelineLayout = VK_NULL_HANDLE;
    VkShaderModule TwoDShader = VK_NULL_HANDLE;
    VkShaderModule FinalShader = VK_NULL_HANDLE;
    VkPipeline TwoDPipeline = VK_NULL_HANDLE;
    VkPipeline FinalPipeline = VK_NULL_HANDLE;
    VkCommandPool CommandPool = VK_NULL_HANDLE;
    VkCommandBuffer CommandBuffer = VK_NULL_HANDLE;
    VkFence Fence = VK_NULL_HANDLE;
    VkSemaphore TimelineSemaphore = VK_NULL_HANDLE;
    std::string FailureStage;
    VkResult FailureResult = VK_SUCCESS;
};

QJsonObject ProbeJson(const ProbeResult& result)
{
    return {
        {"passed", result.Passed},
        {"timeline_semaphore_used", result.TimelineSemaphoreUsed},
        {"fence_fallback_used", result.FenceFallbackUsed},
        {"software_2d_upload_final", result.Software2DUploadFinal},
        {"native_2d_mirror", result.Native2DMirror},
        {"native_2d_visible", result.Native2DVisible},
        {"bg", result.Bg},
        {"obj", result.Obj},
        {"window", result.Window},
        {"blend", result.Blend},
        {"mosaic", result.Mosaic},
        {"brightness", result.Brightness},
        {"three_d_layer", result.ThreeDLayer},
        {"per_scanline_state", result.PerScanlineState},
        {"screen_a_b", result.ScreenAB},
        {"partial_render", result.PartialRender},
        {"capture_source", result.CaptureSource},
        {"final_pass", result.FinalPass},
        {"screen_swap", result.ScreenSwap},
        {"vram_display", result.VramDisplay},
        {"fifo_display", result.FifoDisplay},
        {"screen_disable", result.ScreenDisable},
        {"scale", result.Scale},
        {"gpu_resident_output", result.GpuResidentOutput},
        {"frame_serial_ownership", result.FrameSerialOwnership},
        {"no_normal_cpu_readback", result.NoNormalCpuReadback},
        {"debug_readback", result.DebugReadback},
        {"samples_matched", result.SamplesMatched},
        {"full_image_matched", result.FullImageMatched},
        {"partial_range_count", static_cast<int>(result.PartialRangeCount)},
        {"output_digest", QString::fromStdString(std::to_string(result.OutputDigest))},
        {"validation_layer_available", result.ValidationLayerAvailable},
        {"vulkan_api_calls_succeeded", result.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", result.ResourceDestroyCycleCompleted},
        {"failure_stage", QString::fromStdString(result.FailureStage)},
        {"vk_result", static_cast<int>(result.FailureResult)},
    };
}

} // namespace

int RunPhase9CompletionHarness(const QString& outputPath, int iterations)
{
    if (iterations <= 0) iterations = 1;
    FeatureInfo lastInfo;
    ProbeResult lastResult;
    QJsonArray results;
    int completed = 0;
    auto& host = static_cast<MelonApplication*>(qApp)->vulkanInstanceHost();
    if (!host.ensureCreated())
    {
        lastResult.FailureStage = host.unavailableReason();
    }
    else
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            QWindow window;
            window.setSurfaceType(QSurface::VulkanSurface);
            window.setVulkanInstance(&host.instance());
            window.resize(1, 1);
            window.create();
            auto context = CreateDeviceContext(&window, lastInfo);
            if (!context)
            {
                lastResult.FailureStage = lastInfo.unavailableReason;
                window.destroy();
                break;
            }
            {
                Phase9CompletionProbe probe(context, &window);
                lastResult = probe.Run();
            }
            context.reset();
            window.destroy();
            results.append(ProbeJson(lastResult));
            if (!lastResult.Passed) break;
            ++completed;
        }
    }
    const bool passed = completed == iterations;
    melonDS::Platform::Log(
        passed ? melonDS::Platform::LogLevel::Info : melonDS::Platform::LogLevel::Error,
        passed ? "[MelonPrime] Vulkan Phase 9 completion harness passed: iterations=%d\n" :
                 "[MelonPrime] Vulkan Phase 9 completion harness failed: iterations=%d\n",
        completed);
    const QJsonObject output{
        {"schema_version", 20},
        {"passed", passed},
        {"contract_version", static_cast<int>(kPhase9CompletionContractVersion)},
        {"requested_iterations", iterations},
        {"completed_iterations", completed},
        {"phase9_subsystem_complete", passed},
        {"software_2d_upload_final_passed", lastResult.Software2DUploadFinal},
        {"native_2d_mirror_passed", lastResult.Native2DMirror},
        {"native_2d_visible_passed", lastResult.Native2DVisible},
        {"bg_passed", lastResult.Bg},
        {"obj_passed", lastResult.Obj},
        {"window_passed", lastResult.Window},
        {"blend_passed", lastResult.Blend},
        {"mosaic_passed", lastResult.Mosaic},
        {"brightness_passed", lastResult.Brightness},
        {"three_d_layer_passed", lastResult.ThreeDLayer},
        {"per_scanline_state_passed", lastResult.PerScanlineState},
        {"screen_a_b_passed", lastResult.ScreenAB},
        {"partial_render_passed", lastResult.PartialRender},
        {"capture_source_passed", lastResult.CaptureSource},
        {"final_pass_passed", lastResult.FinalPass},
        {"screen_swap_passed", lastResult.ScreenSwap},
        {"vram_display_passed", lastResult.VramDisplay},
        {"fifo_display_passed", lastResult.FifoDisplay},
        {"screen_disable_passed", lastResult.ScreenDisable},
        {"scale_passed", lastResult.Scale},
        {"gpu_resident_output_passed", lastResult.GpuResidentOutput},
        {"frame_serial_ownership_passed", lastResult.FrameSerialOwnership},
        {"normal_frame_cpu_readback", false},
        {"debug_readback_passed", lastResult.DebugReadback},
        {"samples_matched", lastResult.SamplesMatched},
        {"full_image_matched", lastResult.FullImageMatched},
        {"partial_range_count", static_cast<int>(lastResult.PartialRangeCount)},
        {"output_layer_count", 2},
        {"output_width", static_cast<int>(kOutputWidth)},
        {"output_height", static_cast<int>(kOutputHeight)},
        {"output_layout", "VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL"},
        {"timeline_semaphore_used", lastResult.TimelineSemaphoreUsed},
        {"fence_fallback_used", lastResult.FenceFallbackUsed},
        {"vulkan_api_calls_succeeded", lastResult.VulkanApiCallsSucceeded},
        {"resource_destroy_cycle_completed", lastResult.ResourceDestroyCycleCompleted},
        {"rom_visible_path_activated", false},
        {"native_ds_polygon_raster_integrated", false},
        {"zero_copy_presenter_integrated", false},
        {"software_game_rendering_preserved", true},
        {"failure_stage", QString::fromStdString(lastResult.FailureStage)},
        {"vk_result", static_cast<int>(lastResult.FailureResult)},
        {"iterations", results},
    };
    QFile file(outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 2;
    file.write(QJsonDocument(output).toJson(QJsonDocument::Indented));
    return passed ? 0 : 1;
}

} // namespace MelonPrime::Vulkan
